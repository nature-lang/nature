use log::debug;

use crate::project::Module;
use crate::utils::{errors_push, format_global_ident, format_impl_ident};

use super::common::*;
use super::symbol::{NodeId, ScopeKind, SymbolKind, SymbolTable};
use std::sync::{Arc, Mutex};

#[derive(Debug)]
pub struct Semantic<'a> {
    symbol_table: &'a mut SymbolTable,
    errors: Vec<AnalyzerError>,
    module: &'a mut Module,
    stmts: Vec<Box<Stmt>>,
    imports: Vec<ImportStmt>,
    current_local_fn_list: Vec<Arc<Mutex<AstFnDef>>>,
    current_scope_id: NodeId,
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

    fn enter_scope(&mut self, kind: ScopeKind, start: usize, end: usize) {
        let scope_id = self.symbol_table.create_scope(kind, self.current_scope_id, start, end);
        self.current_scope_id = scope_id;
    }

    fn exit_scope(&mut self) {
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
                    },
                );
            }
            return true;
        }

        return false;
    }

    // 常量折叠 - 在编译时计算常量表达式的值
    fn constant_folding(&mut self, expr: &mut Box<Expr>) {
        match &mut expr.node {
            AstNode::Binary(op, left, right) => {
                // 递归处理左右操作数
                self.constant_folding(left);
                self.constant_folding(right);

                // 检查是否都是字面量
                if let (AstNode::Literal(left_kind, left_literal), AstNode::Literal(right_kind, right_literal)) = (&left.node, &right.node) {
                    // 处理字符串连接
                    if matches!(left_kind, TypeKind::String) && matches!(right_kind, TypeKind::String) && *op == ExprOp::Add {
                        let result_str = format!("{}{}", left_literal, right_literal);
                        expr.node = AstNode::Literal(TypeKind::String, result_str);
                        return;
                    }

                    // 处理数字运算
                    if Type::is_number(&left_kind) && Type::is_number(&right_kind) {
                        let has_float = Type::is_float(&left_kind) || Type::is_float(&right_kind);
                        if has_float {
                            // 浮点数运算
                            if let (Ok(left_val), Ok(right_val)) = (left_literal.parse::<f64>(), right_literal.parse::<f64>()) {
                                let result = match op {
                                    ExprOp::Add => Some(left_val + right_val),
                                    ExprOp::Sub => Some(left_val - right_val),
                                    ExprOp::Mul => Some(left_val * right_val),
                                    ExprOp::Div => {
                                        if right_val != 0.0 {
                                            Some(left_val / right_val)
                                        } else {
                                            None // 除零错误，不进行折叠
                                        }
                                    }
                                    ExprOp::Rem => {
                                        if right_val != 0.0 {
                                            Some(left_val % right_val)
                                        } else {
                                            None
                                        }
                                    }
                                    _ => None, // 浮点数不支持位运算
                                };

                                if let Some(result_float) = result {
                                    expr.node = AstNode::Literal(TypeKind::Float, format!("{}", result_float));
                                }
                            }
                        } else {
                            // 整数运算
                            if let (Ok(left_val), Ok(right_val)) = (left_literal.parse::<i64>(), right_literal.parse::<i64>()) {
                                let result = match op {
                                    ExprOp::Add => Some(left_val.wrapping_add(right_val)),
                                    ExprOp::Sub => Some(left_val.wrapping_sub(right_val)),
                                    ExprOp::Mul => Some(left_val.wrapping_mul(right_val)),
                                    ExprOp::Div => {
                                        if right_val != 0 {
                                            Some(left_val / right_val)
                                        } else {
                                            None // 除零错误
                                        }
                                    }
                                    ExprOp::Rem => {
                                        if right_val != 0 {
                                            Some(left_val % right_val)
                                        } else {
                                            None
                                        }
                                    }
                                    ExprOp::And => Some(left_val & right_val),
                                    ExprOp::Or => Some(left_val | right_val),
                                    ExprOp::Xor => Some(left_val ^ right_val),
                                    ExprOp::Lshift => Some(left_val << right_val),
                                    ExprOp::Rshift => Some(left_val >> right_val),
                                    _ => None,
                                };

                                if let Some(result_val) = result {
                                    expr.node = AstNode::Literal(TypeKind::Int, result_val.to_string());
                                }
                            }
                        }
                    }
                }
            }

            AstNode::Unary(op, operand) => {
                // 递归处理操作数
                self.constant_folding(operand);

                if let AstNode::Literal(operand_kind, operand_value) = &operand.node {
                    if Type::is_number(operand_kind) {
                        if Type::is_float(operand_kind) {
                            // 浮点数一元运算
                            if let Ok(operand_val) = operand_value.parse::<f64>() {
                                let result = match op {
                                    ExprOp::Neg => Some(-operand_val),
                                    _ => None, // 浮点数不支持位运算
                                };

                                if let Some(result_val) = result {
                                    expr.node = AstNode::Literal(TypeKind::Float64, result_val.to_string());
                                }
                            }
                        } else {
                            // 整数一元运算
                            if let Ok(operand_val) = operand_value.parse::<i64>() {
                                let result = match op {
                                    ExprOp::Neg => Some(-operand_val),
                                    ExprOp::Bnot => Some(!operand_val),
                                    _ => None,
                                };

                                if let Some(result_val) = result {
                                    expr.node = AstNode::Literal(TypeKind::Int64, result_val.to_string());
                                }
                            }
                        }
                    } else if matches!(operand_kind, TypeKind::Bool) && *op == ExprOp::Not {
                        // 布尔值取反
                        let operand_val = operand_value == "true";
                        let result = !operand_val;

                        expr.node = AstNode::Literal(TypeKind::Bool, if result { "true".to_string() } else { "false".to_string() });
                    }
                }
            }

            _ => {
                // 其他表达式类型不需要特殊处理
            }
        }
    }

    fn constant_propagation(&mut self, expr: &mut Box<Expr>) {
        // 检查表达式是否为标识符
        let AstNode::Ident(_, symbol_id) = &expr.node else {
            return;
        };

        // 从符号表中获取符号
        let Some(symbol) = self.symbol_table.get_symbol(*symbol_id) else {
            return;
        };

        // 检查符号是否为常量
        let SymbolKind::Const(const_def_mutex) = &symbol.kind.clone() else {
            return;
        };

        let mut const_def = const_def_mutex.lock().unwrap();

        // 检查是否存在循环初始化
        if const_def.processing {
            errors_push(
                self.module,
                AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: "const initialization cycle detected".to_string(),
                },
            );
            return;
        }

        // 设置处理标志，防止循环引用
        const_def.processing = true;

        // 递归分析常量的右值表达式
        self.analyze_expr(&mut const_def.right);

        // 重置处理标志
        const_def.processing = false;

        // 检查右值是否为字面量
        let AstNode::Literal(literal_kind, literal_value) = &const_def.right.node else {
            errors_push(
                self.module,
                AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: "const cannot be initialized with non-literal value".to_string(),
                },
            );
            return;
        };

        // 创建新的字面量表达式替换原标识符表达式
        expr.node = AstNode::Literal(literal_kind.clone(), literal_value.clone());
    }

    fn analyze_type(&mut self, t: &mut Type) {
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
     * 验证选择性导入的符号是否存在于目标模块中
     * 例如: import co.mutex.{mutex_t} 时验证 mutex_t 是否真实存在
     */
    fn validate_selective_imports(&mut self) {
        for import in &self.imports {
            if !import.is_selective {
                continue;
            }

            let Some(ref items) = import.select_items else { continue };

            for item in items {
                let global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                if self.symbol_table.find_symbol_id(&global_ident, self.symbol_table.global_scope_id).is_some() {
                    continue;
                }

                errors_push(
                    self.module,
                    AnalyzerError {
                        start: import.start,
                        end: import.end,
                        message: format!("symbol '{}' not found in module '{}'", item.ident, import.module_ident),
                    },
                );
            }
        }
    }

    /**
     * analyze 之前，相关 module 的 global symbol 都已经注册完成, 这里不能再重复注册了。
     */
    pub fn analyze(&mut self) {
        // 验证选择性导入的符号是否存在
        self.validate_selective_imports();

        let mut global_fn_stmt_list = Vec::<Arc<Mutex<AstFnDef>>>::new();

        let mut var_assign_list = Vec::<Box<Stmt>>::new();

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
                        // 非 builtin type 则进行 resolve type 查找
                        if !Type::is_impl_builtin_type(&fndef.impl_type.kind) {
                            // resolve global ident
                            if let Some(symbol_id) = self.resolve_typedef(&mut fndef.impl_type.ident) {
                                // ident maybe change
                                fndef.impl_type.symbol_id = symbol_id;

                                // 检查: 如果 fndef 没有泛型参数，但 typedef 有泛型参数，则报错
                                // 例如: type person_t<T> = struct{...}
                                // fn person_t.hello() 是错误的，应该是 fn person_t<T>.hello()
                                if fndef.generics_params.is_none() {
                                    if let Some(symbol) = self.symbol_table.get_symbol(symbol_id) {
                                        if let SymbolKind::Type(typedef_mutex) = &symbol.kind {
                                            let typedef = typedef_mutex.lock().unwrap();
                                            if !typedef.params.is_empty() {
                                                errors_push(
                                                    self.module,
                                                    AnalyzerError {
                                                        start: fndef.symbol_start,
                                                        end: fndef.symbol_end,
                                                        message: format!("impl type '{}' must specify generics params", fndef.impl_type.ident),
                                                    },
                                                );
                                            }
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
                                    },
                                );
                            }
                        }

                        fndef.symbol_name = format_impl_ident(fndef.impl_type.ident.clone(), symbol_name);

                        // 泛型 impl 函数的符号表注册延迟到 pre_infer 阶段处理
                        // 与编译器行为保持一致
                        if fndef.generics_params.is_none() {
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
                    }

                    global_fn_stmt_list.push(fndef_mutex.clone());

                    if let Some(generics_params) = &mut fndef.generics_params {
                        for generics_param in generics_params {
                            for constraint in &mut generics_param.constraints.0 {
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

                    // 将 vardef 转换成 assign 导入到 package init 中进行初始化
                    let assign_left = Box::new(Expr::ident(
                        var_decl.symbol_start,
                        var_decl.symbol_end,
                        var_decl.ident.clone(),
                        var_decl.symbol_id,
                    ));

                    let assign_stmt = Box::new(Stmt {
                        node: AstNode::Assign(assign_left, right_expr.clone()),
                        start: right_expr.start,
                        end: right_expr.end,
                    });
                    var_assign_list.push(assign_stmt);
                }

                AstNode::Typedef(type_alias_mutex) => {
                    let mut type_expr = {
                        let mut typedef = type_alias_mutex.lock().unwrap();

                        // 处理 params constraints, type foo<T:int|float, E:int:bool> = ...
                        if typedef.params.len() > 0 {
                            for param in typedef.params.iter_mut() {
                                // 遍历所有 constraints 类型 进行 analyze
                                for constraint in &mut param.constraints.0 {
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

        // 封装 fn init
        if !var_assign_list.is_empty() {
            // 创建init函数定义
            let mut fn_init = AstFnDef::default();
            fn_init.symbol_name = format_global_ident(self.module.ident.clone(), "init".to_string());
            fn_init.fn_name = fn_init.symbol_name.clone();
            fn_init.return_type = Type::new(TypeKind::Void);
            fn_init.body = AstBody {
                stmts: var_assign_list,
                start: 0,
                end: 0,
            };

            global_fn_stmt_list.push(Arc::new(Mutex::new(fn_init)));
        }

        // 对 fn stmt list 进行 analyzer 处理。
        for fndef_mutex in &global_fn_stmt_list {
            self.module.all_fndefs.push(fndef_mutex.clone());
            self.analyze_global_fn(fndef_mutex.clone());
        }

        // global vardefs 的 right 没有和 assign stmt 关联，而是使用了 clone, 所以此处需要单独对又值进行 analyze handle
        for node in &mut global_vardefs {
            match node {
                AstNode::VarDef(_, right_expr) => {
                    if let AstNode::FnDef(_) = &right_expr.node {
                        // fn def 会自动 arc 引用传递, 所以不需要进行单独的 analyze handle, 只有在 fn init 中进行 analyzer 即可注册相关符号，然后再 infer 阶段进行 global var 自动 check
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

    pub fn analyze_global_fn(&mut self, fndef_mutex: Arc<Mutex<AstFnDef>>) {
        {
            let mut fndef = fndef_mutex.lock().unwrap();

            fndef.is_local = false;
            fndef.module_index = self.module.index;
            if fndef.generics_params.is_some() {
                fndef.is_generics = true;
            }

            if fndef.is_tpl && fndef.body.stmts.len() == 0 {
                debug_assert!(fndef.body.stmts.len() == 0);
            }

            self.analyze_type(&mut fndef.return_type);

            // 如果 impl type 是 type alias, 则从符号表中获取当前的 type alias 的全称进行更新
            // fn vec<T>.len() -> fn vec_len(vec<T> self)
            // impl 是 type alias 时，只能是 fn person_t.len() 而不能是 fn pkg.person_t.len()
            if fndef.impl_type.kind.is_exist() {
                if !fndef.is_static && fndef.self_kind != SelfKind::Null {
                    // 重构 params 的位置, 新增 self param
                    let mut new_params = Vec::new();
                    let param_type = fndef.impl_type.clone();
                    let self_vardecl = VarDeclExpr {
                        ident: String::from("self"),
                        type_: param_type,
                        be_capture: false,
                        heap_ident: None,
                        symbol_start: fndef.symbol_start,
                        symbol_end: fndef.symbol_end,
                        symbol_id: 0,
                    };

                    new_params.push(Arc::new(Mutex::new(self_vardecl)));
                    new_params.extend(fndef.params.iter().cloned());
                    fndef.params = new_params;

                    // builtin type 没有注册在符号表，不能添加 method
                    if !Type::is_impl_builtin_type(&fndef.impl_type.kind) {
                        self.symbol_typedef_add_method(fndef.impl_type.ident.clone(), fndef.symbol_name.clone(), fndef_mutex.clone())
                            .unwrap_or_else(|e| {
                                errors_push(
                                    self.module,
                                    AnalyzerError {
                                        start: fndef.symbol_start,
                                        end: fndef.symbol_end,
                                        message: e,
                                    },
                                );
                            });
                    }
                }
            }

            self.enter_scope(ScopeKind::GlobalFn(fndef_mutex.clone()), fndef.symbol_start, fndef.symbol_end);

            // 函数形参处理
            for param_mutex in &fndef.params {
                let mut param = param_mutex.lock().unwrap();
                self.analyze_type(&mut param.type_);

                // 将参数添加到符号表中
                match self.symbol_table.define_symbol_in_scope(
                    param.ident.clone(),
                    SymbolKind::Var(param_mutex.clone()),
                    param.symbol_start,
                    self.current_scope_id,
                ) {
                    Ok(symbol_id) => {
                        param.symbol_id = symbol_id;
                    }
                    Err(e) => {
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: param.symbol_start,
                                end: param.symbol_end,
                                message: e,
                            },
                        );
                    }
                }
            }
        }

        {
            let mut body = {
                let mut fndef = fndef_mutex.lock().unwrap();
                std::mem::take(&mut fndef.body)
            };

            if body.stmts.len() > 0 {
                self.analyze_body(&mut body);
            }

            // 将当前的 fn 添加到 global fn 的 local_children 中
            {
                let mut fndef = fndef_mutex.lock().unwrap();

                // 归还 body
                fndef.body = body;
                fndef.local_children = self.current_local_fn_list.clone();
            }
        }

        // 清空 self.current_local_fn_list, 进行重新计算
        self.current_local_fn_list.clear();

        self.exit_scope();
    }

    pub fn analyze_body(&mut self, body: &mut AstBody) {
        for stmt in &mut body.stmts {
            self.analyze_stmt(stmt);
        }
    }

    pub fn analyze_as_star_or_builtin(&mut self, ident: &str) -> Option<(NodeId, String)> {
        // import * ident
        for import in &self.imports {
            if import.as_name == "*" {
                if let Some(id) = self.symbol_table.find_module_symbol_id(&import.module_ident, &ident) {
                    return Some((id, format_global_ident(import.module_ident.clone(), ident.to_string().clone())));
                }
            }
        }

        // builtin ident
        if let Some(symbol_id) = self.symbol_table.find_symbol_id(ident, self.symbol_table.global_scope_id) {
            return Some((symbol_id, ident.to_string().clone()));
        }

        return None;
    }

    pub fn rewrite_select_expr(&mut self, expr: &mut Box<Expr>) {
        let AstNode::SelectExpr(left, key, _) = &mut expr.node else { unreachable!() };

        if let AstNode::Ident(left_ident, symbol_id) = &mut left.node {
            // 尝试 find local or parent ident, 如果找到，将 symbol_id 添加到 Ident 中
            // symbol 可能是 parent local, 也可能是 parent fn，此时则发生闭包函数引用, 需要将 ident 改写成 env access
            if let Some(id) = self.symbol_table.lookup_symbol(left_ident, self.current_scope_id) {
                *symbol_id = id;
                return;
            }

            // current module ident
            if let Some(id) = self.symbol_table.find_module_symbol_id(&self.module.ident, left_ident) {
                *symbol_id = id;
                *left_ident = format_global_ident(self.module.ident.clone(), left_ident.to_string().clone());

                debug!("rewrite_select_expr -> analyze_ident find, symbol_id {}, new ident {}", id, left_ident);
                return;
            }

            // import package ident
            let import_stmt = self.imports.iter().find(|i| i.as_name == *left_ident);
            if let Some(import_stmt) = import_stmt {
                // debug!("import as name {}, module_ident {}, key {key}", import_stmt.as_name, import_stmt.module_ident);

                // select left 以及找到了，但是还是改不了？ infer 阶段能快速定位就好了。现在的关键是，找到了又怎么样, 又能做什么，也改写不了什么。只能是？
                // 只能是添加一个 symbol_id? 但是符号本身也没有意义了？如果直接改成 ident + symbol_id 呢？还是改，只是改成了更为奇怪的存在。
                if let Some(id) = self.symbol_table.find_module_symbol_id(&import_stmt.module_ident, key) {
                    // debug!("find symbol id {} by module_ident {}, key {key}", id, import_stmt.module_ident);

                    // 将整个 expr 直接改写成 global ident, 这也是 analyze_select_expr 的核心目录
                    expr.node = AstNode::Ident(format_global_ident(import_stmt.module_ident.clone(), key.clone()), id);
                    return;
                } else {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: expr.start,
                            end: expr.end,
                            message: format!("identifier '{}' undeclared in '{}' module", key, left_ident),
                        },
                    );
                    return;
                }
            }

            // builtin ident or import as * 也会产生 select expr 的 left ident, 如果是 import *
            if let Some((id, global_ident)) = self.analyze_as_star_or_builtin(left_ident) {
                *left_ident = global_ident;
                *symbol_id = id;
                return;
            }

            errors_push(
                self.module,
                AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: format!("identifier '{}.{}' undeclared", left_ident, key),
                },
            );

            return;
        }

        self.analyze_expr(left);
    }

    pub fn analyze_ident(&mut self, ident: &mut String, symbol_id: &mut NodeId) -> bool {
        // 尝试 find local or parent ident, 如果找到，将 symbol_id 添加到 Ident 中
        // symbol 可能是 parent local, 也可能是 parent fn，此时则发生闭包函数引用, 需要将 ident 改写成 env access
        if let Some(id) = self.symbol_table.lookup_symbol(ident, self.current_scope_id) {
            *symbol_id = id;
            return true;
        }

        if let Some(id) = self.symbol_table.find_module_symbol_id(&self.module.ident, ident) {
            *symbol_id = id;
            *ident = format_global_ident(self.module.ident.clone(), ident.clone());

            // debug!("analyze_ident find, synbol_id {}, new ident {}", id, ident);
            return true;
        }

        // Check selective imports: import math.{sqrt, pow}
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
                    *symbol_id = id;
                    return true;
                }
            }
        }

        if let Some((id, global_ident)) = self.analyze_as_star_or_builtin(ident) {
            *ident = global_ident;
            *symbol_id = id;
            return true;
        }

        return false;
    }

    pub fn analyze_match(&mut self, subject: &mut Option<Box<Expr>>, cases: &mut Vec<MatchCase>, start: usize, end: usize) {
        // 支持任意表达式作为 subject，不再限制必须是 ident
        if let Some(subject_expr) = subject {
            self.analyze_expr(subject_expr);
        }

        self.enter_scope(ScopeKind::Local, start, end);
        let cases_len = cases.len();
        for (i, case) in cases.iter_mut().enumerate() {
            let cond_list_len = case.cond_list.len();
            let mut is_cond = false;
            for cond in case.cond_list.iter_mut() {
                // default case check
                if let AstNode::Ident(ident, _symbol_id) = &cond.node {
                    if ident == "_" {
                        if cond_list_len != 1 {
                            errors_push(
                                self.module,
                                AnalyzerError {
                                    start: cond.start,
                                    end: cond.end,
                                    message: "default case '_' conflict in a 'match' expression".to_string(),
                                },
                            );
                        }

                        if i != cases_len - 1 {
                            errors_push(
                                self.module,
                                AnalyzerError {
                                    start: cond.start,
                                    end: cond.end,
                                    message: "default case '_' must be the last one in a 'match' expression".to_string(),
                                },
                            );
                        }

                        case.is_default = true;
                        continue;
                    }
                } else if let AstNode::Is(t, _union_tag, None, _binding) = &mut cond.node {
                    // src=None means match-is
                    self.analyze_type(t);

                    is_cond = true;
                }

                self.analyze_expr(cond);
            }

            if case.cond_list.len() > 1 {
                is_cond = false; // cond is logic, not is expr
            }

            // 支持任意表达式作为 subject
            if is_cond && subject.is_some() {
                let Some(subject_expr) = subject else { unreachable!() };
                let Some(cond_expr) = case.cond_list.first() else { unreachable!() };
                let AstNode::Is(target_type, union_tag, None, binding_expr) = &cond_expr.node else {
                    unreachable!()
                };

                // 只有当 binding_expr 存在时才插入 auto as stmt
                if let Some(binding) = binding_expr {
                    case.handle_body.stmts.insert(
                        0,
                        self.auto_as_stmt(cond_expr.start, cond_expr.end, subject_expr, binding, target_type, union_tag),
                    );
                }
            }

            if case.handle_body.stmts.len() > 0 {
                self.enter_scope(ScopeKind::Local, case.handle_body.start, case.handle_body.end);
                self.analyze_body(&mut case.handle_body);
                self.exit_scope();
            }
        }
        self.exit_scope();
    }

    pub fn analyze_async(&mut self, async_expr: &mut MacroAsyncExpr) {
        self.analyze_local_fndef(&async_expr.closure_fn);
        self.analyze_local_fndef(&async_expr.closure_fn_void);

        // closure_fn 的 fn_name 需要继承当前 fn 的 fn_name, 这样报错才会更加的精准, 当前 global 以及是 unlock 状态了，不太妥当
        let mut fndef = async_expr.closure_fn.lock().unwrap();
        let Some(global_fn_mutex) = self.symbol_table.find_global_fn(self.current_scope_id) else {
            return;
        };

        fndef.fn_name = {
            let global_fn = global_fn_mutex.lock().unwrap();
            global_fn.fn_name.clone()
        };

        self.analyze_call(&mut async_expr.origin_call);
        if let Some(flag_expr) = &mut async_expr.flag_expr {
            self.analyze_expr(flag_expr);
        }
    }

    pub fn analyze_expr(&mut self, expr: &mut Box<Expr>) {
        match &mut expr.node {
            AstNode::Binary(_op, left, right) => {
                self.analyze_expr(left);
                self.analyze_expr(right);

                self.constant_folding(expr);
            }
            AstNode::Ternary(condition, consequent, alternate) => {
                self.analyze_expr(condition);
                self.analyze_expr(consequent);
                self.analyze_expr(alternate);
            }
            AstNode::Unary(_op, expr) => {
                self.analyze_expr(expr);

                self.constant_folding(expr);
            }
            AstNode::Catch(try_expr, catch_err, catch_body) => {
                self.analyze_expr(try_expr);

                self.enter_scope(ScopeKind::Local, catch_body.start, catch_body.end);
                self.analyze_var_decl(catch_err);
                self.analyze_body(catch_body);
                self.exit_scope();
            }
            AstNode::As(type_, _, src) => {
                self.analyze_type(type_);
                self.analyze_expr(src);
            }
            AstNode::Is(target_type, union_tag, src, _binding) => {
                // 处理 union_tag (如果存在)
                if let Some(ut) = union_tag {
                    // union_tag 应该是 SelectExpr，分析后可能变成 TaggedUnionElement 或 Ident
                    self.analyze_expr(ut);

                    // 分析完成后检查 union_tag 的类型
                    match &mut ut.node {
                        AstNode::TaggedUnionNew(union_type, tagged_name, element, _) => {
                            // 如果是 TaggedUnionNew，改写为 TaggedUnionElement
                            ut.node = AstNode::TaggedUnionElement(union_type.clone(), tagged_name.clone(), element.clone());
                        }
                        AstNode::SelectExpr(left, key, type_args) => {
                            let AstNode::Ident(ident, _symbol_id) = &left.node else {
                                errors_push(
                                    self.module,
                                    AnalyzerError {
                                        start: ut.start,
                                        end: ut.end,
                                        message: "unexpected is expr".to_string(),
                                    },
                                );
                                return;
                            };

                            let mut union_type = Type::ident_new(ident.clone(), TypeIdentKind::TaggedUnion);
                            if let Some(args) = type_args.as_ref() {
                                union_type.args = args.clone();
                            }
                            self.analyze_type(&mut union_type);

                            ut.node = AstNode::TaggedUnionElement(union_type, key.clone(), None);
                        }
                        AstNode::Ident(ident, _) => {
                            // 如果是 Ident，将 ident 信息复制到 target_type，并清空 union_tag
                            target_type.ident = ident.clone();
                            target_type.kind = TypeKind::Ident;
                            target_type.ident_kind = TypeIdentKind::Unknown;
                            target_type.start = ut.start;
                            target_type.end = ut.end;
                            *union_tag = None;
                        }
                        _ => {
                            errors_push(
                                self.module,
                                AnalyzerError {
                                    start: ut.start,
                                    end: ut.end,
                                    message: "unexpected is expr".to_string(),
                                },
                            );
                        }
                    }
                }

                // 分析 target_type (如果 kind 已设置)
                if target_type.kind.is_exist() {
                    self.analyze_type(target_type);
                }

                // 分析 src (如果存在)
                if let Some(s) = src {
                    self.analyze_expr(s);
                }
            }
            AstNode::MacroSizeof(target_type) | AstNode::MacroDefault(target_type) => {
                self.analyze_type(target_type);
            }
            AstNode::MacroUla(src) => {
                self.analyze_expr(src);
            }
            AstNode::MacroReflectHash(target_type) => {
                self.analyze_type(target_type);
            }
            AstNode::MacroTypeEq(left_type, right_type) => {
                self.analyze_type(left_type);
                self.analyze_type(right_type);
            }
            AstNode::New(type_, properties, default_expr) => {
                self.analyze_type(type_);
                if properties.len() > 0 {
                    for property in properties {
                        self.analyze_expr(&mut property.value);
                    }
                }

                if let Some(expr) = default_expr {
                    self.analyze_expr(expr);
                }
            }
            AstNode::StructNew(_ident, type_, properties) => {
                self.analyze_type(type_);
                for property in properties {
                    self.analyze_expr(&mut property.value);
                }
            }
            AstNode::MapNew(elements) => {
                for element in elements {
                    self.analyze_expr(&mut element.key);
                    self.analyze_expr(&mut element.value);
                }
            }
            AstNode::SetNew(elements) => {
                for element in elements {
                    self.analyze_expr(element);
                }
            }
            AstNode::TupleNew(elements) => {
                for element in elements {
                    self.analyze_expr(element);
                }
            }
            AstNode::TupleDestr(elements) => {
                for element in elements {
                    self.analyze_expr(element);
                }
            }
            AstNode::VecNew(elements, _len, _cap) => {
                for element in elements {
                    self.analyze_expr(element);
                }
            }
            AstNode::VecRepeatNew(default_element, len_expr) => {
                self.analyze_expr(default_element);
                self.analyze_expr(len_expr);
            }
            AstNode::VecSlice(left, start, end) => {
                self.analyze_expr(left);
                self.analyze_expr(start);
                self.analyze_expr(end);
            }
            AstNode::AccessExpr(left, key) => {
                self.analyze_expr(left);
                self.analyze_expr(key);
            }
            AstNode::SelectExpr(..) => {
                self.rewrite_select_expr(expr);
                if let AstNode::SelectExpr(_, _, Some(type_args)) = &mut expr.node {
                    for arg in type_args {
                        self.analyze_type(arg);
                    }
                }

                if matches!(expr.node, AstNode::Ident(..)) {
                    self.constant_propagation(expr);
                }

                if let AstNode::SelectExpr(left, _, _) = &mut expr.node {
                    if matches!(left.node, AstNode::Ident(..)) {
                        self.constant_propagation(left)
                    }
                }
            }
            AstNode::Ident(ident, symbol_id) => {
                if !self.analyze_ident(ident, symbol_id) {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: expr.start,
                            end: expr.end,
                            message: format!("identifier '{}' undeclared", ident),
                        },
                    );
                }

                // propagation
                self.constant_propagation(expr);
            }
            AstNode::Match(subject, cases) => self.analyze_match(subject, cases, expr.start, expr.end),
            AstNode::Call(call) => {
                self.analyze_call(call);

                // shape.ellipse -> shape.ellipse(arg)
                // 如果 call.left 是 TaggedUnionNew，需要将整个表达式改写为 TaggedUnionNew
                if let AstNode::TaggedUnionNew(union_type, tagged_name, element, _) = &call.left.node {
                    if call.args.is_empty() {
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: expr.start,
                                end: expr.end,
                                message: "tagged union uses parentheses but passes no arguments".to_string(),
                            },
                        );
                        return;
                    }

                    let arg = if call.args.len() == 1 {
                        // 单个参数直接使用
                        call.args[0].clone()
                    } else {
                        // 多个参数装配成 tuple
                        let tuple_elements = call.args.clone();
                        Box::new(Expr {
                            start: call.args[0].start,
                            end: call.args.last().map(|e| e.end).unwrap_or(expr.end),
                            type_: Type::default(),
                            target_type: Type::default(),
                            node: AstNode::TupleNew(tuple_elements),
                        })
                    };

                    // 改写表达式为 TaggedUnionNew
                    expr.node = AstNode::TaggedUnionNew(union_type.clone(), tagged_name.clone(), element.clone(), Some(arg));
                }
            }
            AstNode::MacroAsync(async_expr) => self.analyze_async(async_expr),
            AstNode::FnDef(fndef_mutex) => self.analyze_local_fndef(fndef_mutex),
            AstNode::TaggedUnionNew(union_type, _tagged_name, _element, arg) => {
                self.analyze_type(union_type);
                if let Some(arg_expr) = arg {
                    self.analyze_expr(arg_expr);
                }
            }
            _ => {
                return;
            }
        }
    }

    /* if (expr->assert_type == AST_VAR_DECL) {
        analyzer_var_decl(m, expr->value, true);
    } else if (expr->assert_type == AST_EXPR_TUPLE_DESTR) {
        analyzer_var_tuple_destr(m, expr->value);
    } else {
        ANALYZER_ASSERTF(false, "var tuple destr expr type exception");
    } */
    pub fn analyze_var_tuple_destr_item(&mut self, item: &Box<Expr>) {
        match &item.node {
            AstNode::VarDecl(var_decl_mutex) => {
                self.analyze_var_decl(var_decl_mutex);
            }
            AstNode::TupleDestr(elements) => {
                self.analyze_var_tuple_destr(elements);
            }
            _ => {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: item.start,
                        end: item.end,
                        message: "var tuple destr expr type exception".to_string(),
                    },
                );
            }
        }
    }

    pub fn analyze_var_tuple_destr(&mut self, elements: &Vec<Box<Expr>>) {
        for item in elements.iter() {
            self.analyze_var_tuple_destr_item(item);
        }
    }

    pub fn analyze_call(&mut self, call: &mut AstCall) {
        self.analyze_expr(&mut call.left);

        for generics_arg in call.generics_args.iter_mut() {
            self.analyze_type(generics_arg);
        }

        for arg in call.args.iter_mut() {
            self.analyze_expr(arg);
        }
    }

    /**
     * local fn in global fn
     */
    pub fn analyze_local_fndef(&mut self, fndef_mutex: &Arc<Mutex<AstFnDef>>) {
        self.module.all_fndefs.push(fndef_mutex.clone());

        let mut fndef = fndef_mutex.lock().unwrap();

        // find global fn in symbol table
        let Some(global_fn_mutex) = self.symbol_table.find_global_fn(self.current_scope_id) else {
            return;
        };
        fndef.global_parent = Some(global_fn_mutex.clone());
        fndef.is_local = true;

        self.current_local_fn_list.push(fndef_mutex.clone());

        // local fn 作为闭包函数, 不能进行类型扩展和泛型参数
        if fndef.impl_type.kind.is_exist() || fndef.generics_params.is_some() {
            errors_push(
                self.module,
                AnalyzerError {
                    start: fndef.symbol_start,
                    end: fndef.symbol_end,
                    message: "closure fn cannot be generics or impl type alias".to_string(),
                },
            );
        }

        // 闭包不能包含 macro ident
        if fndef.linkid.is_some() {
            errors_push(
                self.module,
                AnalyzerError {
                    start: fndef.symbol_start,
                    end: fndef.symbol_end,
                    message: "closure fn cannot have #linkid label".to_string(),
                },
            );
        }

        if fndef.is_tpl {
            errors_push(
                self.module,
                AnalyzerError {
                    start: fndef.symbol_start,
                    end: fndef.symbol_end,
                    message: "closure fn cannot be template".to_string(),
                },
            );
        }

        self.analyze_type(&mut fndef.return_type);

        self.enter_scope(ScopeKind::LocalFn(fndef_mutex.clone()), fndef.symbol_start, fndef.symbol_end);

        // 形参处理
        for param_mutex in &fndef.params {
            let mut param = param_mutex.lock().unwrap();
            self.analyze_type(&mut param.type_);

            // 将参数添加到符号表中
            match self.symbol_table.define_symbol_in_scope(
                param.ident.clone(),
                SymbolKind::Var(param_mutex.clone()),
                param.symbol_start,
                self.current_scope_id,
            ) {
                Ok(symbol_id) => {
                    param.symbol_id = symbol_id;
                }
                Err(e) => {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: param.symbol_start,
                            end: param.symbol_end,
                            message: e,
                        },
                    );
                }
            }
        }

        // handle body
        self.analyze_body(&mut fndef.body);

        let mut free_var_count = 0;
        let scope = self.symbol_table.find_scope(self.current_scope_id);
        for (_, free_ident) in scope.frees.iter() {
            if matches!(free_ident.kind, SymbolKind::Var(..)) {
                free_var_count += 1;
            }
        }

        self.exit_scope();

        // 当前函数需要编译成闭包, 所有的 call fn 改造成 call fn_var
        if free_var_count > 0 {
            fndef.is_closure = true;
        }

        // 将 fndef lambda 添加到 symbol table 中
        match self.symbol_table.define_symbol_in_scope(
            fndef.symbol_name.clone(),
            SymbolKind::Fn(fndef_mutex.clone()),
            fndef.symbol_start,
            self.current_scope_id,
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
                    },
                );
            }
        }
    }

    pub fn extract_is_expr(&mut self, cond: &Box<Expr>) -> Option<Box<Expr>> {
        // 支持任意表达式作为 is 表达式的源，不再限制必须是 ident
        if let AstNode::Is(_target_type, _union_tag, _src, _binding) = &cond.node {
            return Some(cond.clone());
        }

        // binary && extract
        if let AstNode::Binary(op, left, right) = &cond.node {
            if *op == ExprOp::AndAnd {
                let left_is = self.extract_is_expr(left);
                let right_is = self.extract_is_expr(right);

                // condition expr cannot contains multiple is expr
                if left_is.is_some() && right_is.is_some() {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: cond.start,
                            end: cond.end,
                            message: "condition expr cannot contains multiple is expr".to_string(),
                        },
                    );
                }

                return if left_is.is_some() { left_is } else { right_is };
            }
        }

        return None;
    }

    pub fn auto_as_stmt(
        &mut self,
        start: usize,
        end: usize,
        source_expr: &Box<Expr>,
        binding: &Box<Expr>,
        target_type: &Type,
        union_tag: &Option<Box<Expr>>,
    ) -> Box<Stmt> {
        // var binding = source as T
        // var (a, b) = source as T

        // 克隆源表达式作为 as 表达式的源
        let src_expr = source_expr.clone();
        let target_type_clone = target_type.clone();

        let as_node = AstNode::As(target_type_clone.clone(), union_tag.clone(), src_expr);

        let right_expr = Box::new(Expr {
            node: as_node,
            start,
            end,
            type_: Type::default(),
            target_type: Type::default(),
        });

        // Handle different binding types
        match &binding.node {
            AstNode::Ident(binding_ident, _) => {
                // Simple ident binding: var binding = source as T
                let var_decl = Arc::new(Mutex::new(VarDeclExpr {
                    ident: binding_ident.clone(),
                    type_: target_type_clone, // maybe null
                    be_capture: false,
                    heap_ident: None,
                    symbol_start: start,
                    symbol_end: end,
                    symbol_id: 0,
                }));

                Box::new(Stmt {
                    node: AstNode::VarDef(var_decl, right_expr),
                    start,
                    end,
                })
            }
            AstNode::TupleDestr(elements) => {
                // Tuple destructuring binding: var (a, b) = source as T
                Box::new(Stmt {
                    node: AstNode::VarTupleDestr(elements.clone(), right_expr),
                    start,
                    end,
                })
            }
            _ => {
                // Fallback - shouldn't happen in valid code
                panic!("auto_as_stmt: unexpected binding type")
            }
        }
    }

    pub fn analyze_if(&mut self, cond: &mut Box<Expr>, consequent: &mut AstBody, alternate: &mut AstBody) {
        self.analyze_expr(cond);

        // if has is expr push T e = e as T
        if let Some(is_expr) = self.extract_is_expr(cond) {
            let AstNode::Is(target_type, union_tag, src, binding_expr) = is_expr.node else {
                unreachable!()
            };

            // 只有当 binding_expr 存在且 src 存在时才插入 auto as stmt
            if let Some(binding) = binding_expr {
                if let Some(src_expr) = src {
                    let ast_stmt = self.auto_as_stmt(is_expr.start, is_expr.end, &src_expr, &binding, &target_type, &union_tag);
                    // insert ast_stmt to consequent first
                    consequent.stmts.insert(0, ast_stmt);
                }
            }
        }

        self.enter_scope(ScopeKind::Local, consequent.start, consequent.end);
        self.analyze_body(consequent);
        self.exit_scope();

        self.enter_scope(ScopeKind::Local, alternate.start, alternate.end);
        self.analyze_body(alternate);
        self.exit_scope();
    }

    // local constdef
    fn analyze_constdef(&mut self, constdef_mutex: Arc<Mutex<AstConstDef>>) {
        let mut constdef = constdef_mutex.lock().unwrap();

        self.analyze_expr(&mut constdef.right);

        // 添加 local constdef 到符号表
        match self.symbol_table.define_symbol_in_scope(
            constdef.ident.clone(),
            SymbolKind::Const(constdef_mutex.clone()),
            constdef.symbol_start,
            self.current_scope_id,
        ) {
            Ok(symbol_id) => {
                constdef.symbol_id = symbol_id;
            }
            Err(e) => {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: constdef.symbol_start,
                        end: constdef.symbol_end,
                        message: e,
                    },
                );
            }
        }
    }
    pub fn analyze_stmt(&mut self, stmt: &mut Box<Stmt>) {
        match &mut stmt.node {
            AstNode::Fake(expr) | AstNode::Ret(expr) => {
                self.analyze_expr(expr);
            }
            AstNode::VarDecl(var_decl_mutex) => {
                self.analyze_var_decl(var_decl_mutex);
            }
            AstNode::VarDef(var_decl_mutex, expr) => {
                self.analyze_expr(expr);
                self.analyze_var_decl(var_decl_mutex);
            }
            AstNode::ConstDef(constdef_mutex) => {
                self.analyze_constdef(constdef_mutex.clone());
            }
            AstNode::VarTupleDestr(elements, expr) => {
                self.analyze_expr(expr);
                self.analyze_var_tuple_destr(elements);
            }
            AstNode::Assign(left, right) => {
                self.analyze_expr(left);
                self.analyze_expr(right);
            }
            AstNode::Call(call) => {
                self.analyze_call(call);
            }
            AstNode::Catch(try_expr, catch_err, catch_body) => {
                self.analyze_expr(try_expr);

                self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                self.analyze_var_decl(&catch_err);
                self.analyze_body(catch_body);
                self.exit_scope();
            }
            AstNode::TryCatch(try_body, catch_err, catch_body) => {
                self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                self.analyze_body(try_body);
                self.exit_scope();

                self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                self.analyze_var_decl(&catch_err);
                self.analyze_body(catch_body);
                self.exit_scope();
            }
            AstNode::Select(cases, _has_default, _send_count, _recv_count) => {
                let len = cases.len();
                for (i, case) in cases.iter_mut().enumerate() {
                    if let Some(on_call) = &mut case.on_call {
                        self.analyze_call(on_call);
                    }
                    self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                    if let Some(recv_var) = &case.recv_var {
                        self.analyze_var_decl(recv_var);
                    }
                    self.analyze_body(&mut case.handle_body);
                    self.exit_scope();

                    if case.is_default && i != len - 1 {
                        // push error
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: case.handle_body.start,
                                end: case.handle_body.end,
                                message: "default case must be the last case".to_string(),
                            },
                        );
                    }
                }
            }
            AstNode::Throw(expr) => {
                self.analyze_expr(expr);
            }
            AstNode::If(cond, consequent, alternate) => {
                self.analyze_if(cond, consequent, alternate);
            }
            AstNode::ForCond(condition, body) => {
                self.analyze_expr(condition);
                self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                self.analyze_body(body);
                self.exit_scope();
            }
            AstNode::ForIterator(iterate, first, second, body) => {
                self.analyze_expr(iterate);

                self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                self.analyze_var_decl(first);
                if let Some(second) = second {
                    self.analyze_var_decl(second);
                }
                self.analyze_body(body);
                self.exit_scope();
            }
            AstNode::ForTradition(init, condition, update, body) => {
                self.enter_scope(ScopeKind::Local, stmt.start, stmt.end);
                self.analyze_stmt(init);
                self.analyze_expr(condition);
                self.analyze_stmt(update);
                self.analyze_body(body);
                self.exit_scope();
            }
            AstNode::Return(expr) => {
                if let Some(expr) = expr {
                    self.analyze_expr(expr);
                }
            }
            AstNode::Typedef(type_alias_mutex) => {
                let mut typedef = type_alias_mutex.lock().unwrap();
                // local type alias 不允许携带 param
                if typedef.params.len() > 0 {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: typedef.symbol_start,
                            end: typedef.symbol_end,
                            message: "local type alias cannot have params".to_string(),
                        },
                    );
                }

                // interface type alias 不允许携带 impl
                if typedef.impl_interfaces.len() > 0 {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: typedef.symbol_start,
                            end: typedef.symbol_end,
                            message: "local typedef cannot with impls".to_string(),
                        },
                    );
                }

                self.analyze_type(&mut typedef.type_expr);

                match self.symbol_table.define_symbol_in_scope(
                    typedef.ident.clone(),
                    SymbolKind::Type(type_alias_mutex.clone()),
                    typedef.symbol_start,
                    self.current_scope_id,
                ) {
                    Ok(symbol_id) => {
                        typedef.symbol_id = symbol_id;
                    }
                    Err(e) => {
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: typedef.symbol_start,
                                end: typedef.symbol_end,
                                message: e,
                            },
                        );
                    }
                }
            }
            _ => {
                return;
            }
        }
    }

    pub fn analyze_var_decl(&mut self, var_decl_mutex: &Arc<Mutex<VarDeclExpr>>) {
        let mut var_decl = var_decl_mutex.lock().unwrap();

        self.analyze_type(&mut var_decl.type_);

        // 添加到符号表，返回值 sysmbol_id 添加到 var_decl 中, 已经包含了 redeclare check
        match self.symbol_table.define_symbol_in_scope(
            var_decl.ident.clone(),
            SymbolKind::Var(var_decl_mutex.clone()),
            var_decl.symbol_start,
            self.current_scope_id,
        ) {
            Ok(symbol_id) => {
                var_decl.symbol_id = symbol_id;
            }
            Err(e) => {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: var_decl.symbol_start,
                        end: var_decl.symbol_end,
                        message: e,
                    },
                );
            }
        }
    }
}
