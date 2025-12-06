use crate::analyzer::symbol::{NodeId, Symbol, SymbolKind, SymbolTable};
use crate::analyzer::common::AstFnDef;
use crate::project::Module;
use log::debug;
use std::collections::HashSet;

#[derive(Debug, Clone)]
pub struct CompletionItem {
    pub label: String, // 变量名
    pub kind: CompletionItemKind,
    pub detail: Option<String>, // 类型信息
    pub documentation: Option<String>,
    pub insert_text: String,       // 插入文本
    pub sort_text: Option<String>, // 排序权重
}

#[derive(Debug, Clone)]
pub enum CompletionItemKind {
    Variable,
    Parameter,
    Function,
    Constant,
}

pub struct CompletionProvider<'a> {
    symbol_table: &'a mut SymbolTable,
    module: &'a mut Module,
}

impl<'a> CompletionProvider<'a> {
    pub fn new(symbol_table: &'a mut SymbolTable, module: &'a mut Module) -> Self {
        Self { symbol_table, module }
    }

    /// 主要的自动完成入口函数
    pub fn get_completions(&self, position: usize, prefix: &str) -> Vec<CompletionItem> {
        dbg!("get_completions", position, &self.module.ident, self.module.scope_id, prefix);

        // 检查是否在类型成员访问上下文中 (variable.field)
        if let Some((var_name, member_prefix)) = extract_module_member_context(prefix, position) {
            // 先尝试作为变量类型成员访问
            let current_scope_id = self.find_innermost_scope(self.module.scope_id, position);
            if let Some(completions) = self.get_type_member_completions(&var_name, &member_prefix, current_scope_id) {
                debug!("Found type member completions for variable '{}'", var_name);
                return completions;
            }
            
            // 如果不是变量，尝试作为模块成员访问
            debug!("Detected module member access: {} and {}", var_name, member_prefix);
            return self.get_module_member_completions(&var_name, &member_prefix);
        }

        // 普通的变量补全
        let prefix = extract_prefix_at_position(prefix, position);
        debug!("Getting completions at position {} with prefix '{}'", position, prefix);

        // 1. 根据位置找到当前作用域
        let current_scope_id = self.find_innermost_scope(self.module.scope_id, position);
        debug!("Found scope_id {} by positon {}", current_scope_id, position);

        // 2. 收集所有可见的变量符号
        let mut completions = Vec::new();
        self.collect_variable_completions(current_scope_id, &prefix, &mut completions, position);

        // 3. 排序和过滤
        self.sort_and_filter_completions(&mut completions, &prefix);

        debug!("Found {} completions", completions.len());
        dbg!(&completions);

        completions
    }

    /// 获取模块成员的自动补全
    pub fn get_module_member_completions(&self, imported_as_name: &str, prefix: &str) -> Vec<CompletionItem> {
        debug!("Getting module member completions for module '{}' with prefix '{}'", imported_as_name, prefix);

        let mut completions = Vec::new();

        let deps = &self.module.dependencies;

        let import_stmt = deps.iter().find(|&dep| dep.as_name == imported_as_name);
        if import_stmt.is_none() {
            return completions;
        }

        let imported_module_ident = import_stmt.unwrap().module_ident.clone();
        debug!("Imported module is '{}' find by {}", imported_module_ident, imported_as_name);

        // 查找导入的模块作用域
        if let Some(&imported_scope_id) = self.symbol_table.module_scopes.get(&imported_module_ident) {
            let imported_scope = self.symbol_table.find_scope(imported_scope_id);

            // 遍历导入模块的所有符号
            for &symbol_id in &imported_scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    // 只显示公开的符号（这里假设所有符号都是公开的，你可以根据需要添加可见性检查）
                    if prefix.is_empty() || symbol.ident.starts_with(prefix) {
                        let completion_item = self.create_module_completion_member(symbol);
                        debug!("Adding module member completion: {}", completion_item.label);
                        completions.push(completion_item);
                    }
                }
            }
        } else {
            debug!("Module '{}' not found in symbol table", imported_module_ident);
        }

        // 排序和过滤
        self.sort_and_filter_completions(&mut completions, prefix);

        debug!("Found {} module member completions", completions.len());
        completions
    }

    /// 获取类型成员的自动补全 (for struct fields and methods)
    pub fn get_type_member_completions(&self, var_name: &str, prefix: &str, current_scope_id: NodeId) -> Option<Vec<CompletionItem>> {
        debug!("Getting type member completions for variable '{}' with prefix '{}'", var_name, prefix);

        // 1. Find the variable in the current scope
        let var_symbol = self.find_variable_in_scope(var_name, current_scope_id)?;
        
        // 2. Get the variable's type
        use crate::analyzer::common::TypeKind;
        let (var_type_kind, typedef_symbol_id) = match &var_symbol.kind {
            SymbolKind::Var(var_decl) => {
                let var = var_decl.lock().unwrap();
                let type_ = &var.type_;
                
                debug!("Variable '{}' has type: {:?}, symbol_id: {}", var_name, type_.kind, type_.symbol_id);
                
                (type_.kind.clone(), type_.symbol_id)
            }
            _ => {
                debug!("Symbol is not a variable");
                return None;
            }
        };

        let mut completions = Vec::new();

        // 3. Handle direct struct type (inlined)
        if let TypeKind::Struct(_, _, properties) = &var_type_kind {
            debug!("Variable has direct struct type with {} fields", properties.len());
            for prop in properties {
                if prefix.is_empty() || prop.name.starts_with(prefix) {
                    completions.push(CompletionItem {
                        label: prop.name.clone(),
                        kind: CompletionItemKind::Variable,
                        detail: Some(format!("field: {}", prop.type_)),
                        documentation: None,
                        insert_text: prop.name.clone(),
                        sort_text: Some(format!("{:08}", prop.start)),
                    });
                }
            }
        }

        // 4. Handle typedef reference
        if matches!(var_type_kind, TypeKind::Ident) && typedef_symbol_id != 0 {
            debug!("Looking up typedef with symbol_id: {}", typedef_symbol_id);
            
            if let Some(typedef_symbol) = self.symbol_table.get_symbol_ref(typedef_symbol_id) {
                if let SymbolKind::Type(typedef) = &typedef_symbol.kind {
                    let typedef = typedef.lock().unwrap();
                    debug!("Found typedef: {}, type_expr.kind: {:?}, methods: {}", 
                           typedef.ident, typedef.type_expr.kind, typedef.method_table.len());

                    // Add struct fields from typedef
                    if let TypeKind::Struct(_, _, properties) = &typedef.type_expr.kind {
                        debug!("Typedef struct has {} fields", properties.len());
                        for prop in properties {
                            if prefix.is_empty() || prop.name.starts_with(prefix) {
                                completions.push(CompletionItem {
                                    label: prop.name.clone(),
                                    kind: CompletionItemKind::Variable,
                                    detail: Some(format!("field: {}", prop.type_)),
                                    documentation: None,
                                    insert_text: prop.name.clone(),
                                    sort_text: Some(format!("{:08}", prop.start)),
                                });
                            }
                        }
                    }

                    // Add methods for typedef
                    for method in typedef.method_table.values() {
                        let fndef = method.lock().unwrap();
                        if prefix.is_empty() || fndef.fn_name.starts_with(prefix) {
                            let signature = self.format_function_signature(&fndef);
                            let insert_text = if self.has_parameters(&fndef) {
                                format!("{}($0)", fndef.fn_name)
                            } else {
                                format!("{}()", fndef.fn_name)
                            };
                            completions.push(CompletionItem {
                                label: fndef.fn_name.clone(),
                                kind: CompletionItemKind::Function,
                                detail: Some(format!("fn: {}", signature)),
                                documentation: None,
                                insert_text,
                                sort_text: Some(format!("{:08}", fndef.symbol_start)),
                            });
                        }
                    }
                } else {
                    debug!("Symbol {} is not a Type", typedef_symbol_id);
                }
            }
        }

        // 5. Look for methods by searching for functions with impl_type matching this type
        // This handles cases like: fn config.helloWorld()
        if typedef_symbol_id != 0 {
            self.collect_impl_methods(typedef_symbol_id, prefix, &mut completions);
        }

        self.sort_and_filter_completions(&mut completions, prefix);
        debug!("Found {} type member completions", completions.len());
        
        if completions.is_empty() {
            None
        } else {
            Some(completions)
        }
    }

    /// Collect methods implemented for a type (fn TypeName.method())
    fn collect_impl_methods(&self, typedef_symbol_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>) {
        // Search module scope for functions with matching impl_type
        let module_scope = self.symbol_table.find_scope(self.module.scope_id);
        for &symbol_id in &module_scope.symbols {
            if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                if let SymbolKind::Fn(fndef) = &symbol.kind {
                    let fndef = fndef.lock().unwrap();
                    // Check if this function's impl_type matches our typedef
                    if fndef.impl_type.symbol_id == typedef_symbol_id {
                        if prefix.is_empty() || fndef.fn_name.starts_with(prefix) {
                            debug!("Found impl method: {}", fndef.fn_name);
                            let signature = self.format_function_signature(&fndef);
                            let insert_text = if self.has_parameters(&fndef) {
                                format!("{}($0)", fndef.fn_name)
                            } else {
                                format!("{}()", fndef.fn_name)
                            };
                            completions.push(CompletionItem {
                                label: fndef.fn_name.clone(),
                                kind: CompletionItemKind::Function,
                                detail: Some(format!("fn: {}", signature)),
                                documentation: None,
                                insert_text,
                                sort_text: Some(format!("{:08}", fndef.symbol_start)),
                            });
                        }
                    }
                }
            }
        }
    }

    /// Find a variable in the current scope and parent scopes
    fn find_variable_in_scope(&self, var_name: &str, current_scope_id: NodeId) -> Option<&Symbol> {
        debug!("Searching for variable '{}' starting from scope {}", var_name, current_scope_id);
        let mut visited_scopes = HashSet::new();
        let mut current = current_scope_id;

        while current > 0 && !visited_scopes.contains(&current) {
            visited_scopes.insert(current);
            let scope = self.symbol_table.find_scope(current);
            debug!("Checking scope {} with {} symbols", current, scope.symbols.len());

            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    match &symbol.kind {
                        SymbolKind::Var(var_decl) => {
                            let var = var_decl.lock().unwrap();
                            debug!("Found var symbol: '{}' (looking for '{}')", var.ident, var_name);
                            if var.ident == var_name {
                                drop(var);
                                debug!("Variable '{}' found in scope {}", var_name, current);
                                return Some(symbol);
                            }
                        }
                        _ => {}
                    }
                }
            }

            current = scope.parent;
            if current == 0 {
                break;
            }
        }

        debug!("Variable '{}' not found in any scope", var_name);
        None
    }

    /// Check if function has parameters (excluding self)
    fn has_parameters(&self, fndef: &AstFnDef) -> bool {
        fndef.params.iter().any(|param| {
            let param_locked = param.lock().unwrap();
            param_locked.ident != "self"
        })
    }

    /// Format a function signature for display
    fn format_function_signature(&self, fndef: &AstFnDef) -> String {
        let mut params_str = String::new();
        let mut first = true;
        
        for param in fndef.params.iter() {
            let param_locked = param.lock().unwrap();
            
            // Skip 'self' parameter
            if param_locked.ident == "self" {
                continue;
            }
            
            if !first {
                params_str.push_str(", ");
            }
            first = false;
            
            // Use the ident field from Type which contains the original type name
            let type_str = if !param_locked.type_.ident.is_empty() {
                param_locked.type_.ident.clone()
            } else {
                param_locked.type_.to_string()
            };
            
            params_str.push_str(&format!("{} {}", type_str, param_locked.ident));
        }
        
        if fndef.rest_param && !params_str.is_empty() {
            params_str.push_str(", ...");
        }
        
        let return_type = fndef.return_type.to_string();
        format!("fn({}): {}", params_str, return_type)
    }

    /// 从模块作用域开始，根据位置找到包含该位置的最内层作用域
    fn find_innermost_scope(&self, scope_id: NodeId, position: usize) -> NodeId {
        let scope = self.symbol_table.find_scope(scope_id);
        debug!("[find_innermost_scope] scope_id {}, start {}, end {}", scope_id, scope.range.0, scope.range.1);

        // 检查当前作用域是否包含该位置(range.1 == 0 表示整个文件级别的作用域)
        if position >= scope.range.0 && (position < scope.range.1 || scope.range.1 == 0) {
            // 检查子作用域，找到最内层的作用域
            for &child_id in &scope.children {
                let child_scope = self.symbol_table.find_scope(child_id);

                debug!(
                    "[find_innermost_scope] child scope_id {}, start {}, end {}",
                    scope_id, child_scope.range.0, child_scope.range.1
                );
                if position >= child_scope.range.0 && position < child_scope.range.1 {
                    return self.find_innermost_scope(child_id, position);
                }
            }

            return scope_id;
        }

        scope_id // 如果不在范围内，返回当前作用域
    }

    /// 收集变量完成项
    fn collect_variable_completions(&self, current_scope_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>, position: usize) {
        let mut visited_scopes = HashSet::new();
        let mut current = current_scope_id;

        // 从当前作用域向上遍历
        while current > 0 && !visited_scopes.contains(&current) {
            visited_scopes.insert(current);

            let scope = self.symbol_table.find_scope(current);
            debug!("Searching scope {} with {} symbols", current, scope.symbols.len());

            // 遍历当前作用域的所有符号
            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    dbg!("Found symbol will check", symbol.ident.clone(), prefix, symbol.ident.starts_with(prefix));

                    // 只处理变量和常量
                    match &symbol.kind {
                        SymbolKind::Var(_) | SymbolKind::Const(_) => {
                            if (prefix.is_empty() || symbol.ident.starts_with(prefix)) && symbol.pos < position {
                                let completion_item = self.create_completion_item(symbol);
                                debug!("Adding completion: {}", completion_item.label);
                                completions.push(completion_item);
                            }
                        }
                        _ => {}
                    }
                }
            }
            current = scope.parent;

            // 如果到达了根作用域，停止遍历
            if current == 0 {
                break;
            }
        }
    }

    /// 创建完成项
    fn create_completion_item(&self, symbol: &Symbol) -> CompletionItem {
        let (kind, detail) = match &symbol.kind {
            SymbolKind::Var(var_decl) => {
                let detail = {
                    let var = var_decl.lock().unwrap();
                    format!("var: {}", var.type_)
                };
                (CompletionItemKind::Variable, Some(detail))
            }
            SymbolKind::Const(const_def) => {
                let detail = {
                    let const_val = const_def.lock().unwrap();
                    format!("const: {}", const_val.type_)
                };
                (CompletionItemKind::Constant, Some(detail))
            }
            _ => (CompletionItemKind::Variable, None),
        };

        CompletionItem {
            label: symbol.ident.clone(),
            kind,
            detail,
            documentation: None,
            insert_text: symbol.ident.clone(),
            sort_text: Some(format!("{:08}", symbol.pos)), // 按定义位置排序
        }
    }

    /// 创建模块成员完成项
    fn create_module_completion_member(&self, symbol: &Symbol) -> CompletionItem {
        let (ident, kind, detail) = match &symbol.kind {
            SymbolKind::Var(var) => {
                let var = var.lock().unwrap();
                let detail = format!("var: {}", var.type_);
                let display_ident = extract_last_ident_part(&var.ident.clone());
                (display_ident, CompletionItemKind::Variable, Some(detail))
            }
            SymbolKind::Const(constdef) => {
                let constdef = constdef.lock().unwrap();
                let detail = format!("const: {}", constdef.type_);
                let display_ident = extract_last_ident_part(&constdef.ident.clone());
                (display_ident, CompletionItemKind::Constant, Some(detail))
            }
            SymbolKind::Fn(fndef) => {
                let fndef = fndef.lock().unwrap();
                let detail =  format!("fn: {}", fndef.type_);
                (fndef.fn_name.clone(), CompletionItemKind::Function, Some(detail))
            }
            SymbolKind::Type(typedef) => {
                let typedef = typedef.lock().unwrap();
                let detail = format!("type definition");
                let display_ident = extract_last_ident_part(&typedef.ident);
                (display_ident, CompletionItemKind::Variable, Some(detail))
            }
        };

        CompletionItem {
            label: ident.clone(),
            kind,
            detail,
            documentation: None,
            insert_text: ident.clone(),
            sort_text: Some(format!("{:08}", symbol.pos)),
        }
    }

    /// 排序和过滤完成项
    fn sort_and_filter_completions(&self, completions: &mut Vec<CompletionItem>, prefix: &str) {
        // 去重 - 基于标签去重
        completions.sort_by(|a, b| a.label.cmp(&b.label));
        completions.dedup_by(|a, b| a.label == b.label);

        // 按匹配度和定义位置排序
        completions.sort_by(|a, b| {
            // 精确前缀匹配优先
            let a_exact = a.label.starts_with(prefix);
            let b_exact = b.label.starts_with(prefix);

            match (a_exact, b_exact) {
                (true, false) => std::cmp::Ordering::Less,
                (false, true) => std::cmp::Ordering::Greater,
                _ => {
                    // 按字母顺序排序
                    a.label.cmp(&b.label)
                }
            }
        });

        // 限制返回数量
        completions.truncate(50);
    }
}

/// 从文本中提取光标位置的前缀
pub fn extract_prefix_at_position(text: &str, position: usize) -> String {
    if position == 0 {
        return String::new();
    }

    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return String::new();
    }

    let mut start = position;

    // 向前查找标识符的开始位置
    while start > 0 {
        let ch = chars[start - 1];
        if ch.is_alphanumeric() || ch == '_' || ch == '.' {
            start -= 1;
        } else {
            break;
        }
    }

    // 提取前缀
    chars[start..position].iter().collect()
}

/// 从类似 "io.main.writer" 的标识符中提取最后一个部分 "writer"
fn extract_last_ident_part(ident: &str) -> String {
    if let Some(dot_pos) = ident.rfind('.') {
        ident[dot_pos + 1..].to_string()
    } else {
        ident.to_string()
    }
}

/// 检测是否在模块成员访问上下文中，返回 (模块名, 成员前缀)
pub fn extract_module_member_context(prefix: &str, _position: usize) -> Option<(String, String)> {
    if let Some(dot_pos) = prefix.rfind('.') {
        let module_name = prefix[..dot_pos].to_string();
        let member_prefix = prefix[dot_pos + 1..].to_string();
        
        if !module_name.is_empty() {
            return Some((module_name, member_prefix));
        }
    }
    
    None
}
