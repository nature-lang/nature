use log::debug;
use tower_lsp::lsp_types::SemanticTokenType;

use crate::utils::{errors_push, format_global_ident};

use super::super::common::*;
use super::super::lexer::semantic_token_type_index;
use super::super::symbol::{NodeId, ScopeKind, SymbolKind};
use super::Semantic;
use std::sync::{Arc, Mutex};

impl<'a> Semantic<'a> {
    // 常量折叠 - 在编译时计算常量表达式的值
    pub(crate) fn constant_folding(&mut self, expr: &mut Box<Expr>) {
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

    pub(crate) fn constant_propagation(&mut self, expr: &mut Box<Expr>) {
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
                    is_warning: false,
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
                    is_warning: false,
                },
            );
            return;
        };

        // 创建新的字面量表达式替换原标识符表达式
        expr.node = AstNode::Literal(literal_kind.clone(), literal_value.clone());
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

        // Empty key means incomplete parse recovery (user is typing "x."), 
        // resolve the left side but skip key-dependent analysis and error messages
        if key.is_empty() {
            if let AstNode::Ident(left_ident, symbol_id) = &mut left.node {
                if let Some(id) = self.symbol_table.lookup_symbol(left_ident, self.current_scope_id) {
                    *symbol_id = id;
                } else if let Some(id) = self.symbol_table.find_module_symbol_id(&self.module.ident, left_ident) {
                    *symbol_id = id;
                    *left_ident = format_global_ident(self.module.ident.clone(), left_ident.to_string().clone());
                }
            } else {
                self.analyze_expr(left);
            }
            return;
        }

        if let AstNode::Ident(left_ident, symbol_id) = &mut left.node {
            // 尝试 find local or parent ident, 如果找到，将 symbol_id 添加到 Ident 中
            // symbol 可能是 parent local, 也可能是 parent fn，此时则发生闭包函数引用, 需要将 ident 改写成 env access
            if let Some(id) = self.symbol_table.lookup_symbol(left_ident, self.current_scope_id) {
                *symbol_id = id;
                // Update the left ident token type based on what it actually is
                self.update_ident_token_type(left.start, left.end, id);
                return;
            }

            // current module ident
            if let Some(id) = self.symbol_table.find_module_symbol_id(&self.module.ident, left_ident) {
                *symbol_id = id;
                *left_ident = format_global_ident(self.module.ident.clone(), left_ident.to_string().clone());

                debug!("rewrite_select_expr -> analyze_ident find, symbol_id {}, new ident {}", id, left_ident);
                return;
            }

            // Check selective imports: import colors.{Color} then Color.RED
            for import in &self.imports {
                if !import.is_selective {
                    continue;
                }
                let Some(ref items) = import.select_items else { continue };
                for item in items {
                    let local_name = item.alias.as_ref().unwrap_or(&item.ident);
                    if local_name != left_ident.as_str() {
                        continue;
                    }
                    let global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                    if let Some(id) = self.symbol_table.find_symbol_id(&global_ident, self.symbol_table.global_scope_id) {
                        *left_ident = global_ident;
                        *symbol_id = id;
                        return;
                    }
                }
            }

            // import package ident
            let import_module_ident = self.imports.iter()
                .find(|i| i.as_name == *left_ident)
                .map(|i| i.module_ident.clone());
            if let Some(module_ident) = import_module_ident {
                if let Some(id) = self.symbol_table.find_module_symbol_id(&module_ident, key) {
                    // Mark the import prefix as NAMESPACE and the key token by its resolved kind
                    let left_ns_idx = semantic_token_type_index(SemanticTokenType::NAMESPACE);
                    for token in self.module.sem_token_db.iter_mut() {
                        if token.start == left.start && token.end == left.end {
                            token.semantic_token_type = left_ns_idx;
                            break;
                        }
                    }
                    // Update key token type based on the resolved symbol
                    let expr_end = expr.end;
                    self.update_ident_token_by_end(expr_end, id);

                    // 将整个 expr 直接改写成 global ident
                    expr.node = AstNode::Ident(format_global_ident(module_ident.clone(), key.clone()), id);
                    return;
                } else {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: expr.start,
                            end: expr.end,
                            message: format!("identifier '{}' undeclared in '{}' module", key, left_ident),
                            is_warning: false,
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
                    is_warning: false,
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
                                    is_warning: false,
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
                                    is_warning: false,
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
                                        is_warning: false,
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
                                    is_warning: false,
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
                            is_warning: false,
                        },
                    );
                } else {
                    // Update semantic token type based on the resolved symbol kind
                    self.update_ident_token_type(expr.start, expr.end, *symbol_id);
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
                                is_warning: false,
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
                        is_warning: false,
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
                            is_warning: false,
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
                        is_warning: false,
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

                // Type inference: if var type is unknown, try to infer from right-hand expression
                self.infer_var_type_from_expr(var_decl_mutex, expr);
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
                                is_warning: false,
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
                            is_warning: false,
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
                            is_warning: false,
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
                                is_warning: false,
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
}
