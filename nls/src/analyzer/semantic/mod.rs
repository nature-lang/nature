mod declarations;
mod expressions;

use tower_lsp::lsp_types::SemanticTokenType;

use crate::project::Module;
use crate::utils::{errors_push, format_global_ident, format_impl_ident};

use super::common::*;
use super::lexer::semantic_token_type_index;
use super::symbol::{NodeId, ScopeKind, SymbolKind, SymbolTable};
use std::sync::{Arc, Mutex};

#[derive(Debug)]
pub struct Semantic<'a> {
    pub(crate) symbol_table: &'a mut SymbolTable,
    pub(crate) errors: Vec<AnalyzerError>,
    pub(crate) module: &'a mut Module,
    pub(crate) stmts: Vec<Box<Stmt>>,
    pub(crate) imports: Vec<ImportStmt>,
    pub(crate) current_local_fn_list: Vec<Arc<Mutex<AstFnDef>>>,
    pub(crate) current_scope_id: NodeId,
}

impl<'a> Semantic<'a> {
    pub fn new(m: &'a mut Module, symbol_table: &'a mut SymbolTable) -> Self {
        Self {
            symbol_table,
            errors: Vec::new(),
            stmts: m.stmts.clone(),
            imports: m.dependencies.clone(),
            current_scope_id: m.scope_id, // m.scope_id 是 global scope id
            module: m,
            current_local_fn_list: Vec::new(),
        }
    }

    pub(crate) fn enter_scope(&mut self, kind: ScopeKind, start: usize, end: usize) {
        let scope_id = self.symbol_table.create_scope(kind, self.current_scope_id, start, end);
        self.current_scope_id = scope_id;
    }

    pub(crate) fn exit_scope(&mut self) {
        self.current_scope_id = self.symbol_table.exit_scope(self.current_scope_id);
    }

    fn analyze_special_type_rewrite(&mut self, t: &mut Type) -> bool {
        debug_assert!(t.import_as.is_empty());

        // void ptr rewrite
        if t.ident == "anyptr" {
            t.kind = TypeKind::Anyptr;
            t.ident = "".to_string();
            t.ident_kind = TypeIdentKind::Unknown;

            if t.args.len() > 0 {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("anyptr cannot contains arg"),
                        is_warning: false,
                    },
                );
                t.err = true;
            }

            return true;
        }

        // raw ptr rewrite
        if t.ident == "ptr".to_string() {
            // extract first args to type_
            if t.args.len() > 0 {
                let mut first_arg_type = t.args[0].clone();
                self.analyze_type(&mut first_arg_type);
                t.kind = TypeKind::Ptr(Box::new(first_arg_type));
            } else {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("ptr must contains one arg"),
                        is_warning: false,
                    },
                );
            }

            t.ident = "".to_string();
            t.ident_kind = TypeIdentKind::Unknown;
            return true;
        }

        // ref rewrite
        if t.ident == "ref".to_string() {
            if t.args.len() > 0 {
                let mut first_arg_type = t.args[0].clone();
                self.analyze_type(&mut first_arg_type);
                t.kind = TypeKind::Ref(Box::new(first_arg_type));
            } else {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("ref must contains one arg"),
                        is_warning: false,
                    },
                );
            }

            t.ident = "".to_string();
            t.ident_kind = TypeIdentKind::Unknown;
            return true;
        }

        // all_t rewrite
        if t.ident == "all_t".to_string() {
            t.kind = TypeKind::Anyptr; // 底层类型
            t.ident = "all_t".to_string();
            t.ident_kind = TypeIdentKind::Builtin;
            if t.args.len() > 0 {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("all_type cannot contains arg"),
                        is_warning: false,
                    },
                );
            }
            return true;
        }

        // fn_t rewrite
        if t.ident == "fn_t".to_string() {
            t.kind = TypeKind::Anyptr;
            t.ident = "fn_t".to_string();
            t.ident_kind = TypeIdentKind::Builtin;
            if t.args.len() > 0 {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("fn_t cannot contains arg"),
                        is_warning: false,
                    },
                );
            }
            return true;
        }

        if t.ident == "integer_t".to_string() {
            t.kind = TypeKind::Int;
            t.ident = "integer_t".to_string();
            t.ident_kind = TypeIdentKind::Builtin;

            if t.args.len() > 0 {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("fn_t cannot contains arg"),
                        is_warning: false,
                    },
                );
            }
            return true;
        }

        if t.ident == "floater_t".to_string() {
            t.kind = TypeKind::Int;
            t.ident = "floater_t".to_string();
            t.ident_kind = TypeIdentKind::Builtin;

            if t.args.len() > 0 {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("fn_t cannot contains arg"),
                        is_warning: false,
                    },
                );
            }
            return true;
        }

        return false;
    }

    pub(crate) fn analyze_type(&mut self, t: &mut Type) {
        if Type::is_ident(t) || t.ident_kind == TypeIdentKind::Interface {
            // 处理导入的全局模式别名，例如  package.foo_t
            if !t.import_as.is_empty() {
                // 只要存在 import as, 就必须能够在 imports 中找到对应的 import
                let import_stmt = self.imports.iter().find(|i| i.as_name == t.import_as);
                if import_stmt.is_none() {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: t.start,
                            end: t.end,
                            message: format!("import '{}' undeclared", t.import_as),
                            is_warning: false,
                        },
                    );
                    t.err = true;
                    return;
                }

                let import_stmt = import_stmt.unwrap();

                // 从 symbol table 中查找相关的 global symbol id
                if let Some(symbol_id) = self.symbol_table.find_module_symbol_id(&import_stmt.module_ident, &t.ident) {
                    t.import_as = "".to_string();
                    t.ident = format_global_ident(import_stmt.module_ident.clone(), t.ident.clone());
                    t.symbol_id = symbol_id;
                } else {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: t.start,
                            end: t.end,
                            message: format!("type '{}' undeclared in {} module", t.ident, import_stmt.module_ident),
                            is_warning: false,
                        },
                    );
                    t.err = true;
                    return;
                }
            } else {
                // no import as, maybe local ident or parent indet
                if let Some(symbol_id) = self.resolve_typedef(&mut t.ident) {
                    t.symbol_id = symbol_id;
                } else {
                    // maybe check is special type ident
                    if self.analyze_special_type_rewrite(t) {
                        return;
                    }

                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: t.start,
                            end: t.end,
                            message: format!("type '{}' undeclared", t.ident),
                            is_warning: false,
                        },
                    );
                    t.err = true;
                    return;
                }
            }

            if let Some(symbol) = self.symbol_table.get_symbol(t.symbol_id) {
                let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: t.start,
                            end: t.end,
                            message: format!("'{}' not a type", t.ident),
                            is_warning: false,
                        },
                    );
                    t.err = true;
                    return;
                };
                let (is_alias, is_interface) = {
                    let typedef_stmt = typedef_mutex.lock().unwrap();
                    (typedef_stmt.is_alias, typedef_stmt.is_interface)
                };

                // 确认具体类型
                if t.ident_kind == TypeIdentKind::Unknown {
                    if is_alias {
                        t.ident_kind = TypeIdentKind::Alias;
                        if t.args.len() > 0 {
                            errors_push(
                                self.module,
                                AnalyzerError {
                                    start: t.start,
                                    end: t.end,
                                    message: format!("alias '{}' cannot contains generics type args", t.ident),
                                    is_warning: false,
                                },
                            );
                            return;
                        }
                    } else if is_interface {
                        t.ident_kind = TypeIdentKind::Interface;
                    } else {
                        t.ident_kind = TypeIdentKind::Def;
                    }
                }
            }

            // analyzer args
            if t.args.len() > 0 {
                for arg_type in &mut t.args {
                    self.analyze_type(arg_type);
                }
            }

            return;
        }

        match &mut t.kind {
            TypeKind::Interface(elements) => {
                for element in elements {
                    self.analyze_type(element);
                }
            }
            TypeKind::Union(_, _, elements) => {
                for element in elements.iter_mut() {
                    self.analyze_type(element);
                }
            }
            TypeKind::Map(key_type, value_type) => {
                self.analyze_type(key_type);
                self.analyze_type(value_type);
            }
            TypeKind::Set(element_type) => {
                self.analyze_type(element_type);
            }
            TypeKind::Vec(element_type) => {
                self.analyze_type(element_type);
            }
            TypeKind::Chan(element_type) => {
                self.analyze_type(element_type);
            }
            TypeKind::Arr(length_expr, length, element_type) => {
                self.analyze_expr(length_expr);
                if let AstNode::Literal(literal_kind, literal_value) = &mut length_expr.node {
                    if Type::is_integer(literal_kind) {
                        if let Ok(parsed_length) = literal_value.parse::<i64>() {
                            *length = parsed_length as u64;
                        } else {
                            errors_push(
                                self.module,
                                AnalyzerError {
                                    start: t.start,
                                    end: t.end,
                                    message: "array length must be constans or integer literal".to_string(),
                                    is_warning: false,
                                },
                            );
                        }
                    } else {
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: t.start,
                                end: t.end,
                                message: "array length must be constans or integer literal".to_string(),
                                is_warning: false,
                            },
                        );
                    }
                } else {
                    // error push
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: t.start,
                            end: t.end,
                            message: "array length must be constans or integer literal".to_string(),
                            is_warning: false,
                        },
                    );
                }

                self.analyze_type(element_type);
            }
            TypeKind::Tuple(elements, _align) => {
                for element in elements {
                    self.analyze_type(element);
                }
            }
            TypeKind::Ref(value_type) => {
                self.analyze_type(value_type);
            }
            TypeKind::Ptr(value_type) => {
                self.analyze_type(value_type);
            }
            TypeKind::Fn(fn_type) => {
                self.analyze_type(&mut fn_type.return_type);

                for param_type in &mut fn_type.param_types {
                    self.analyze_type(param_type);
                }
            }
            TypeKind::Struct(_ident, _, properties) => {
                for property in properties.iter_mut() {
                    self.analyze_type(&mut property.type_);

                    // 可选的又值
                    if let Some(value) = &mut property.value {
                        self.analyze_expr(value);

                        // value kind cannot is fndef
                        if let AstNode::FnDef(..) = value.node {
                            errors_push(
                                self.module,
                                AnalyzerError {
                                    start: value.start,
                                    end: value.end,
                                    message: format!("struct field default value cannot be a fn def, use fn def ident instead"),
                                    is_warning: false,
                                },
                            );
                            t.err = true;
                        }
                    }
                }
            }
            TypeKind::Enum(element_type, properties) => {
                // Analyze element type
                self.analyze_type(element_type);

                // Analyze value expressions for each enum member
                for property in properties.iter_mut() {
                    if let Some(value_expr) = &mut property.value_expr {
                        self.analyze_expr(value_expr);
                    }
                }
            }
            TypeKind::TaggedUnion(_ident, elements) => {
                for element in elements.iter_mut() {
                    self.analyze_type(&mut element.type_);
                }
            }
            _ => {
                return;
            }
        }
    }

    /**
     * Update the semantic token type for an identifier based on its resolved symbol kind.
     * This ensures function references get FUNCTION coloring, types get TYPE, etc.
     */
    pub(crate) fn update_ident_token_type(&mut self, start: usize, end: usize, symbol_id: NodeId) {
        if symbol_id == 0 {
            return;
        }
        let sem_type = if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
            match &symbol.kind {
                SymbolKind::Fn(_) => SemanticTokenType::FUNCTION,
                SymbolKind::Type(_) => SemanticTokenType::TYPE,
                SymbolKind::Const(_) => SemanticTokenType::MACRO,
                SymbolKind::Var(_) => SemanticTokenType::VARIABLE,
            }
        } else {
            return;
        };
        let sem_idx = semantic_token_type_index(sem_type);
        for token in self.module.sem_token_db.iter_mut() {
            if token.start == start && token.end == end {
                token.semantic_token_type = sem_idx;
                break;
            }
        }
    }

    /// Update the semantic token type for a token found by its end position (for select expr keys).
    pub(crate) fn update_ident_token_by_end(&mut self, end: usize, symbol_id: NodeId) {
        if symbol_id == 0 {
            return;
        }
        let sem_type = if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
            match &symbol.kind {
                SymbolKind::Fn(_) => SemanticTokenType::FUNCTION,
                SymbolKind::Type(_) => SemanticTokenType::TYPE,
                SymbolKind::Const(_) => SemanticTokenType::MACRO,
                SymbolKind::Var(_) => SemanticTokenType::PROPERTY,
            }
        } else {
            return;
        };
        let sem_idx = semantic_token_type_index(sem_type);
        for token in self.module.sem_token_db.iter_mut().rev() {
            if token.end == end && token.token_type == super::lexer::TokenType::Ident {
                token.semantic_token_type = sem_idx;
                break;
            }
        }
    }

    /**
     * 验证选择性导入的符号是否存在于目标模块中，
     * 并根据符号类型设置正确的语义 token 颜色。
     * 例如: import co.mutex.{mutex_t} 时验证 mutex_t 是否真实存在
     */
    fn validate_selective_imports(&mut self) {
        // Collect token updates first to avoid borrow conflicts
        let mut token_updates: Vec<(usize, usize, usize)> = Vec::new(); // (start, end, sem_idx)
        let mut alias_updates: Vec<(usize, usize, usize)> = Vec::new(); // (item_start, item_end, sem_idx)
        let mut errors: Vec<(usize, usize, String, String)> = Vec::new();

        for import in &self.imports {
            if !import.is_selective {
                continue;
            }

            let Some(ref items) = import.select_items else { continue };

            for item in items {
                let global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                if let Some(symbol_id) = self.symbol_table.find_symbol_id(&global_ident, self.symbol_table.global_scope_id) {
                    // Classify the import symbol's semantic token based on what it actually is
                    if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                        let sem_type = match &symbol.kind {
                            SymbolKind::Fn(_) => SemanticTokenType::FUNCTION,
                            SymbolKind::Type(_) => SemanticTokenType::TYPE,
                            SymbolKind::Const(_) => SemanticTokenType::MACRO,
                            SymbolKind::Var(_) => SemanticTokenType::VARIABLE,
                        };
                        let sem_idx = semantic_token_type_index(sem_type);
                        token_updates.push((item.start, item.end, sem_idx));

                        if item.alias.is_some() {
                            alias_updates.push((item.start, item.end, sem_idx));
                        }
                    }
                    continue;
                }

                errors.push((import.start, import.end, item.ident.clone(), import.module_ident.clone()));
            }
        }

        // Apply semantic token updates
        for (start, end, sem_idx) in token_updates {
            for token in self.module.sem_token_db.iter_mut() {
                if token.start == start && token.end == end {
                    token.semantic_token_type = sem_idx;
                    break;
                }
            }
        }

        // Apply alias token updates (alias token is after the original ident within the item range)
        for (item_start, item_end, sem_idx) in alias_updates {
            for token in self.module.sem_token_db.iter_mut() {
                if token.start > item_start && token.end == item_end {
                    token.semantic_token_type = sem_idx;
                    break;
                }
            }
        }

        // Report errors
        for (start, end, ident, module_ident) in errors {
            errors_push(
                self.module,
                AnalyzerError {
                    start,
                    end,
                    message: format!("symbol '{}' not found in module '{}'", ident, module_ident),
                    is_warning: false,
                },
            );
        }
    }

    /**
     * analyze 之前，相关 module 的 global symbol 都已经注册完成, 这里不能再重复注册了。
     */
    pub fn analyze(&mut self) {
        // 验证选择性导入的符号是否存在
        self.validate_selective_imports();

        let mut global_fn_stmt_list = Vec::<Arc<Mutex<AstFnDef>>>::new();

        let mut stmts = Vec::<Box<Stmt>>::new();

        let mut global_vardefs = Vec::new();

        // 跳过 import
        for i in 0..self.stmts.len() {
            // 使用 clone 避免对 self 所有权占用
            let mut stmt = self.stmts[i].clone();

            match &mut stmt.node {
                AstNode::Import(..) => continue,
                AstNode::FnDef(fndef_mutex) => {
                    let mut fndef = fndef_mutex.lock().unwrap();
                    let symbol_name = fndef.symbol_name.clone();

                    if fndef.impl_type.kind.is_exist() {
                        let mut impl_type_ident = fndef.impl_type.ident.clone();

                        if Type::is_impl_builtin_type(&fndef.impl_type.kind) {
                            impl_type_ident = fndef.impl_type.kind.to_string();
                        }

                        // 非 builtin type 则进行 resolve type 查找
                        if !Type::is_impl_builtin_type(&fndef.impl_type.kind) {
                            // resolve global ident
                            if let Some(symbol_id) = self.resolve_typedef(&mut fndef.impl_type.ident) {
                                // ident maybe change
                                fndef.impl_type.symbol_id = symbol_id;
                                impl_type_ident = fndef.impl_type.ident.clone();

                                // 自定义泛型 impl type 必须显式给出类型参数（仅检查 impl_type.args）
                                if let Some(symbol) = self.symbol_table.get_symbol(symbol_id) {
                                    if let SymbolKind::Type(typedef_mutex) = &symbol.kind {
                                        let typedef = typedef_mutex.lock().unwrap();
                                        if !typedef.params.is_empty() && fndef.impl_type.args.len() != typedef.params.len() {
                                            errors_push(
                                                self.module,
                                                AnalyzerError {
                                                    start: fndef.symbol_start,
                                                    end: fndef.symbol_end,
                                                    message: format!("impl type '{}' must specify generics params", fndef.impl_type.ident),
                                                    is_warning: false,
                                                },
                                            );
                                        }
                                    }
                                }
                            } else {
                                errors_push(
                                    self.module,
                                    AnalyzerError {
                                        start: fndef.symbol_start,
                                        end: fndef.symbol_end,
                                        message: format!("type '{}' undeclared", fndef.impl_type.symbol_id),
                                        is_warning: false,
                                    },
                                );
                            }
                        }

                        fndef.symbol_name = format_impl_ident(impl_type_ident, symbol_name);

                        // register to global symbol table
                        match self.symbol_table.define_symbol_in_scope(
                            fndef.symbol_name.clone(),
                            SymbolKind::Fn(fndef_mutex.clone()),
                            fndef.symbol_start,
                            self.module.scope_id,
                        ) {
                            Ok(symbol_id) => {
                                fndef.symbol_id = symbol_id;
                            }
                            Err(e) => {
                                errors_push(
                                    self.module,
                                    AnalyzerError {
                                        start: fndef.symbol_start,
                                        end: fndef.symbol_end,
                                        message: e,
                                        is_warning: false,
                                    },
                                );
                            }
                        }

                        // register to global symbol
                        let _ = self.symbol_table.define_global_symbol(
                            fndef.symbol_name.clone(),
                            SymbolKind::Fn(fndef_mutex.clone()),
                            fndef.symbol_start,
                            self.module.scope_id,
                        );
                    }

                    global_fn_stmt_list.push(fndef_mutex.clone());

                    if let Some(generics_params) = &mut fndef.generics_params {
                        for generics_param in generics_params {
                            for constraint in &mut generics_param.constraints {
                                self.analyze_type(constraint);
                            }
                        }
                    }
                }
                AstNode::VarDef(var_decl_mutex, right_expr) => {
                    let mut var_decl = var_decl_mutex.lock().unwrap();
                    self.analyze_type(&mut var_decl.type_);

                    // push to global_vardef
                    global_vardefs.push(AstNode::VarDef(var_decl_mutex.clone(), right_expr.clone()));
                }

                AstNode::Typedef(type_alias_mutex) => {
                    let mut type_expr = {
                        let mut typedef = type_alias_mutex.lock().unwrap();

                        // 处理 params constraints, type foo<T:int|float, E:int:bool> = ...
                        if typedef.params.len() > 0 {
                            for param in typedef.params.iter_mut() {
                                // 遍历所有 constraints 类型 进行 analyze
                                for constraint in &mut param.constraints {
                                    // TODO constraint 不能是自身
                                    self.analyze_type(constraint);
                                }
                            }
                        }

                        if typedef.impl_interfaces.len() > 0 {
                            for impl_interface in &mut typedef.impl_interfaces {
                                debug_assert!(impl_interface.kind == TypeKind::Ident && impl_interface.ident_kind == TypeIdentKind::Interface);
                                self.analyze_type(impl_interface);
                            }
                        }

                        // analyzer type expr, symbol table 中存储的是 type_expr 的 arc clone, 所以这里的修改会同步到 symbol table 中
                        // 递归依赖处理
                        typedef.type_expr.clone()
                    };

                    self.analyze_type(&mut type_expr);

                    {
                        let mut typedef = type_alias_mutex.lock().unwrap();
                        typedef.type_expr = type_expr;
                    }
                }

                AstNode::ConstDef(const_mutex) => {
                    let mut constdef = const_mutex.lock().unwrap();
                    self.analyze_expr(&mut constdef.right);

                    if !matches!(constdef.right.node, AstNode::Literal(..)) {
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: constdef.symbol_start,
                                end: constdef.symbol_end,
                                message: format!("const cannot be initialized"),
                                is_warning: false,
                            },
                        );
                    }
                }
                _ => {
                    // 语义分析中包含许多错误
                }
            }

            // 归还 stmt list
            stmts.push(stmt);
        }

        // 对 fn stmt list 进行 analyzer 处理。
        for fndef_mutex in &global_fn_stmt_list {
            self.module.all_fndefs.push(fndef_mutex.clone());
            self.analyze_global_fn(fndef_mutex.clone());
        }

        // global vardef 的右值不在函数体里，需要独立做 analyze
        for node in &mut global_vardefs {
            match node {
                AstNode::VarDef(_, right_expr) => {
                    if let AstNode::FnDef(_) = &right_expr.node {
                        // fn def 会自动 arc 引用传递，这里无需重复处理
                    } else {
                        self.analyze_expr(right_expr);
                    }
                }
                _ => {}
            }
        }

        self.module.stmts = stmts;
        self.module.global_vardefs = global_vardefs;
        self.module.global_fndefs = global_fn_stmt_list;
        self.module.analyzer_errors.extend(self.errors.clone());
    }

    pub fn resolve_typedef(&mut self, ident: &mut String) -> Option<NodeId> {
        // 首先尝试在当前作用域和父级作用域中直接查找该符号, 最终会找到 m.scope_id, 这里包含当前 module 的全局符号
        if let Some(symbol_id) = self.symbol_table.lookup_symbol(ident, self.current_scope_id) {
            return Some(symbol_id);
        }

        // 首先尝试在当前 module 中查找该符号
        if let Some(symbol_id) = self.symbol_table.find_module_symbol_id(&self.module.ident, ident) {
            let current_module_ident = format_global_ident(self.module.ident.clone(), ident.to_string());
            *ident = current_module_ident;
            return Some(symbol_id);
        }

        // Check selective imports: import math.{sqrt, pow, Point}
        for import in &self.imports {
            if !import.is_selective {
                continue;
            }
            let Some(ref items) = import.select_items else { continue };
            for item in items {
                let local_name = item.alias.as_ref().unwrap_or(&item.ident);
                if local_name != ident {
                    continue;
                }
                let global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                if let Some(id) = self.symbol_table.find_symbol_id(&global_ident, self.symbol_table.global_scope_id) {
                    *ident = global_ident;
                    return Some(id);
                }
            }
        }

        // import x as * 产生的全局符号
        for i in &self.imports {
            if i.as_name != "*" {
                continue;
            };

            if let Some(symbol_id) = self.symbol_table.find_module_symbol_id(&i.module_ident, ident) {
                *ident = format_global_ident(i.module_ident.clone(), ident.to_string());
                return Some(symbol_id);
            }
        }

        // builtin 全局符号，不需要进行 format 链接，直接读取 global 符号表
        return self.symbol_table.find_symbol_id(ident, self.symbol_table.global_scope_id);
    }

    pub fn symbol_typedef_add_method(&mut self, typedef_ident: String, method_ident: String, fndef: Arc<Mutex<AstFnDef>>) -> Result<(), String> {
        // get typedef from symbol table(global symbol)
        let symbol = self
            .symbol_table
            .find_global_symbol(&typedef_ident)
            .ok_or_else(|| format!("symbol {} not found", typedef_ident))?;
        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return Err(format!("symbol {} is not typedef", typedef_ident));
        };
        let mut typedef = typedef_mutex.lock().unwrap();
        typedef.method_table.insert(method_ident, fndef);

        return Ok(());
    }
}
