use std::collections::HashSet;

use super::common::{
    AnalyzerError, AstBody, AstCall, AstFnDef, AstNode, Expr, ExprOp, GenericsParam, MacroArg, MatchCase, SelectCase, Stmt, Type, TypeIdentKind, TypeKind,
};
use super::symbol::{SymbolKind, SymbolTable};
use crate::project::Module;
use crate::utils::errors_push;

const EQUATABLE_IDENT: &str = "equatable";
const COMPARABLE_IDENT: &str = "comparable";
const ADDABLE_IDENT: &str = "addable";
const NUMERIC_IDENT: &str = "numeric";

pub struct Generics<'a> {
    module: &'a mut Module,
    symbol_table: &'a SymbolTable,
}

impl<'a> Generics<'a> {
    pub fn new(module: &'a mut Module, symbol_table: &'a SymbolTable) -> Self {
        Self { module, symbol_table }
    }

    pub fn analyze(&mut self) {
        let global_fndefs = self.module.global_fndefs.clone();
        for fndef_mutex in global_fndefs {
            let fndef = fndef_mutex.lock().unwrap().clone();
            if !fndef.is_generics {
                continue;
            }
            if fndef.generics_params.is_none() || fndef.body.stmts.is_empty() {
                continue;
            }
            self.check_body(&fndef, &fndef.body);
        }
    }

    fn push_error(&mut self, start: usize, end: usize, message: String) {
        errors_push(self.module, AnalyzerError { start, end, message, is_warning: false });
    }

    fn find_generics_param<'b>(&self, fndef: &'b AstFnDef, ident: &str) -> Option<&'b GenericsParam> {
        fndef.generics_params.as_ref()?.iter().find(|p| p.ident == ident)
    }

    fn expr_generics_param<'b>(&self, fndef: &'b AstFnDef, expr: &Expr) -> Option<&'b GenericsParam> {
        let AstNode::Ident(_, symbol_id) = &expr.node else {
            return None;
        };
        if *symbol_id == 0 {
            return None;
        }

        let symbol = self.symbol_table.get_symbol_ref(*symbol_id)?;
        if !symbol.is_local {
            return None;
        }
        let SymbolKind::Var(var_decl_mutex) = &symbol.kind else {
            return None;
        };
        let var_decl = var_decl_mutex.lock().unwrap();
        if var_decl.type_.kind != TypeKind::Ident || var_decl.type_.ident_kind != TypeIdentKind::GenericsParam {
            return None;
        }
        self.find_generics_param(fndef, &var_decl.type_.ident)
    }

    fn constraint_contains_interface(&self, interface_type: &Type, interface_ident: &str, visited: &mut HashSet<String>) -> bool {
        if interface_type.ident == interface_ident {
            return true;
        }
        if interface_type.ident.is_empty() {
            return false;
        }
        if !visited.insert(interface_type.ident.clone()) {
            return false;
        }

        let Some(symbol) = self.symbol_table.find_global_symbol(&interface_type.ident) else {
            return false;
        };
        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return false;
        };
        let typedef_stmt = typedef_mutex.lock().unwrap();
        for impl_interface in &typedef_stmt.impl_interfaces {
            if self.constraint_contains_interface(impl_interface, interface_ident, visited) {
                return true;
            }
        }

        false
    }

    fn constraint_has_interface(&self, param: &GenericsParam, interface_ident: &str) -> bool {
        for constraint in &param.constraints {
            if self.constraint_contains_interface(constraint, interface_ident, &mut HashSet::new()) {
                return true;
            }
        }
        false
    }

    fn interface_declares_method(&self, interface_type: &Type, method_name: &str, visited: &mut HashSet<String>) -> bool {
        if let TypeKind::Interface(elements) = &interface_type.kind {
            for element in elements {
                if let TypeKind::Fn(type_fn) = &element.kind {
                    if type_fn.name == method_name {
                        return true;
                    }
                }
            }
        }

        if interface_type.ident.is_empty() {
            return false;
        }
        if !visited.insert(interface_type.ident.clone()) {
            return false;
        }

        let Some(symbol) = self.symbol_table.find_global_symbol(&interface_type.ident) else {
            return false;
        };
        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return false;
        };
        let typedef_stmt = typedef_mutex.lock().unwrap();

        if let TypeKind::Interface(elements) = &typedef_stmt.type_expr.kind {
            for element in elements {
                if let TypeKind::Fn(type_fn) = &element.kind {
                    if type_fn.name == method_name {
                        return true;
                    }
                }
            }
        }

        for impl_interface in &typedef_stmt.impl_interfaces {
            if self.interface_declares_method(impl_interface, method_name, visited) {
                return true;
            }
        }

        false
    }

    fn constraint_has_method(&self, param: &GenericsParam, method_name: &str) -> bool {
        for constraint in &param.constraints {
            if self.interface_declares_method(constraint, method_name, &mut HashSet::new()) {
                return true;
            }
        }
        false
    }

    fn check_bool_operand(&mut self, fndef: &AstFnDef, expr: &Expr) {
        if let Some(param) = self.expr_generics_param(fndef, expr) {
            self.push_error(expr.start, expr.end, format!("generic param '{}' cannot be used as bool value", param.ident));
            return;
        }

        match &expr.node {
            AstNode::Unary(op, operand) if *op == ExprOp::Not => {
                self.check_bool_operand(fndef, operand);
            }
            AstNode::Binary(op, left, right) if *op == ExprOp::AndAnd || *op == ExprOp::OrOr => {
                self.check_bool_operand(fndef, left);
                self.check_bool_operand(fndef, right);
            }
            AstNode::Ternary(condition, _, _) => {
                self.check_bool_operand(fndef, condition);
            }
            _ => {}
        }
    }

    fn check_operator_operand(&mut self, fndef: &AstFnDef, operand: &Expr, op: ExprOp, expect_interface: &str) {
        let Some(param) = self.expr_generics_param(fndef, operand) else {
            return;
        };

        if self.constraint_has_interface(param, expect_interface) {
            return;
        }

        self.push_error(
            operand.start,
            operand.end,
            format!(
                "generic param '{}' cannot use operator '{}' without '{}' constraint",
                param.ident, op, expect_interface
            ),
        );
    }

    fn check_unary(&mut self, fndef: &AstFnDef, op: ExprOp, operand: &Expr) {
        self.check_expr(fndef, operand);

        if op == ExprOp::Neg || op == ExprOp::Bnot {
            self.check_operator_operand(fndef, operand, op, NUMERIC_IDENT);
            return;
        }

        if op == ExprOp::Not {
            self.check_bool_operand(fndef, operand);
        }
    }

    fn expect_interface_for_binary(op: ExprOp) -> Option<&'static str> {
        match op {
            ExprOp::Add => Some(ADDABLE_IDENT),
            ExprOp::Sub | ExprOp::Mul | ExprOp::Div | ExprOp::Rem | ExprOp::And | ExprOp::Or | ExprOp::Xor | ExprOp::Lshift | ExprOp::Rshift => {
                Some(NUMERIC_IDENT)
            }
            ExprOp::Lt | ExprOp::Le | ExprOp::Gt | ExprOp::Ge => Some(COMPARABLE_IDENT),
            ExprOp::Ee | ExprOp::Ne => Some(EQUATABLE_IDENT),
            _ => None,
        }
    }

    fn generic_param_ident_from_select_left(&self, fndef: &AstFnDef, select_left: &Expr) -> Option<String> {
        let AstNode::Ident(ident, symbol_id) = &select_left.node else {
            return None;
        };

        if *symbol_id != 0 {
            if let Some(symbol) = self.symbol_table.get_symbol_ref(*symbol_id) {
                if symbol.is_local {
                    if let SymbolKind::Var(var_decl_mutex) = &symbol.kind {
                        let var_decl = var_decl_mutex.lock().unwrap();
                        if var_decl.type_.kind == TypeKind::Ident && var_decl.type_.ident_kind == TypeIdentKind::GenericsParam {
                            return Some(var_decl.type_.ident.clone());
                        }
                    }
                }
            }
        }

        self.find_generics_param(fndef, ident).map(|p| p.ident.clone())
    }

    fn check_call(&mut self, fndef: &AstFnDef, call: &AstCall) {
        for arg in &call.args {
            self.check_expr(fndef, arg);
        }

        let AstNode::SelectExpr(select_left, key, _) = &call.left.node else {
            self.check_expr(fndef, &call.left);
            return;
        };

        let Some(generic_param_ident) = self.generic_param_ident_from_select_left(fndef, select_left) else {
            self.check_expr(fndef, select_left);
            return;
        };

        let Some(param) = self.find_generics_param(fndef, &generic_param_ident) else {
            return;
        };
        if param.constraints.is_empty() {
            self.push_error(
                call.left.start,
                call.left.end,
                format!("generic param '{}' has no constraint declaring fn '{}'", generic_param_ident, key),
            );
            return;
        }

        if !self.constraint_has_method(param, key) {
            self.push_error(
                call.left.start,
                call.left.end,
                format!("generic param '{}' has no constraint declaring fn '{}'", generic_param_ident, key),
            );
        }
    }

    fn check_match_cases(&mut self, fndef: &AstFnDef, cases: &Vec<MatchCase>) {
        for case in cases {
            self.check_body(fndef, &case.handle_body);
        }
    }

    fn check_select_cases(&mut self, fndef: &AstFnDef, cases: &Vec<SelectCase>) {
        for case in cases {
            if let Some(on_call) = &case.on_call {
                self.check_call(fndef, on_call);
            }
            self.check_body(fndef, &case.handle_body);
        }
    }

    fn check_expr(&mut self, fndef: &AstFnDef, expr: &Expr) {
        match &expr.node {
            AstNode::Call(call) => {
                self.check_call(fndef, call);
            }
            AstNode::Binary(op, left, right) => {
                self.check_expr(fndef, left);
                self.check_expr(fndef, right);

                if let Some(expect_interface) = Self::expect_interface_for_binary(op.clone()) {
                    self.check_operator_operand(fndef, left, op.clone(), expect_interface);
                    self.check_operator_operand(fndef, right, op.clone(), expect_interface);
                } else if *op == ExprOp::AndAnd || *op == ExprOp::OrOr {
                    self.check_bool_operand(fndef, left);
                    self.check_bool_operand(fndef, right);
                }
            }
            AstNode::Unary(op, operand) => {
                self.check_unary(fndef, op.clone(), operand);
            }
            AstNode::Ternary(condition, consequent, alternate) => {
                self.check_bool_operand(fndef, condition);
                self.check_expr(fndef, condition);
                self.check_expr(fndef, consequent);
                self.check_expr(fndef, alternate);
            }
            AstNode::As(_, _, src) => {
                self.check_expr(fndef, src);
            }
            AstNode::Is(_, _, src, _) => {
                if let Some(src) = src {
                    self.check_expr(fndef, src);
                }
            }
            AstNode::Catch(try_expr, _, catch_body) => {
                self.check_expr(fndef, try_expr);
                self.check_body(fndef, catch_body);
            }
            AstNode::Match(subject, cases) => {
                if let Some(subject) = subject {
                    self.check_expr(fndef, subject);
                }
                self.check_match_cases(fndef, cases);
            }
            AstNode::SelectExpr(left, _, _) => {
                self.check_expr(fndef, left);
            }
            AstNode::AccessExpr(left, key) => {
                self.check_expr(fndef, left);
                self.check_expr(fndef, key);
            }
            AstNode::MapAccess(_, _, left, key) => {
                self.check_expr(fndef, left);
                self.check_expr(fndef, key);
            }
            AstNode::VecAccess(_, left, index) | AstNode::ArrayAccess(_, left, index) => {
                self.check_expr(fndef, left);
                self.check_expr(fndef, index);
            }
            AstNode::TupleAccess(_, left, _) | AstNode::StructSelect(left, _, _) => {
                self.check_expr(fndef, left);
            }
            AstNode::VecSlice(left, start, end) => {
                self.check_expr(fndef, left);
                self.check_expr(fndef, start);
                self.check_expr(fndef, end);
            }
            AstNode::VecRepeatNew(default_element, len_expr) | AstNode::ArrRepeatNew(default_element, len_expr) => {
                self.check_expr(fndef, default_element);
                self.check_expr(fndef, len_expr);
            }
            AstNode::VecNew(elements, len_expr, cap_expr) => {
                for element in elements {
                    self.check_expr(fndef, element);
                }
                if let Some(len_expr) = len_expr {
                    self.check_expr(fndef, len_expr);
                }
                if let Some(cap_expr) = cap_expr {
                    self.check_expr(fndef, cap_expr);
                }
            }
            AstNode::ArrayNew(elements) | AstNode::SetNew(elements) | AstNode::TupleNew(elements) => {
                for element in elements {
                    self.check_expr(fndef, element);
                }
            }
            AstNode::MapNew(elements) => {
                for element in elements {
                    self.check_expr(fndef, &element.key);
                    self.check_expr(fndef, &element.value);
                }
            }
            AstNode::New(_, properties, scalar_expr) => {
                for property in properties {
                    self.check_expr(fndef, &property.value);
                }
                if let Some(scalar_expr) = scalar_expr {
                    self.check_expr(fndef, scalar_expr);
                }
            }
            AstNode::MacroAsync(async_expr) => {
                self.check_call(fndef, &async_expr.origin_call);
            }
            AstNode::MacroCall(_, args) => {
                for arg in args {
                    match arg {
                        MacroArg::Expr(expr) => self.check_expr(fndef, expr),
                        MacroArg::Stmt(stmt) => self.check_stmt(fndef, stmt),
                        MacroArg::Type(_) => {}
                    }
                }
            }
            _ => {}
        }
    }

    fn check_stmt(&mut self, fndef: &AstFnDef, stmt: &Stmt) {
        match &stmt.node {
            AstNode::Fake(expr) => self.check_expr(fndef, expr),
            AstNode::VarDef(_, right) => self.check_expr(fndef, right),
            AstNode::VarTupleDestr(_, right) => self.check_expr(fndef, right),
            AstNode::Assign(left, right) => {
                self.check_expr(fndef, left);
                self.check_expr(fndef, right);
            }
            AstNode::Call(call) => self.check_call(fndef, call),
            AstNode::If(condition, consequent, alternate) => {
                self.check_bool_operand(fndef, condition);
                self.check_expr(fndef, condition);
                self.check_body(fndef, consequent);
                self.check_body(fndef, alternate);
            }
            AstNode::ForCond(condition, body) => {
                self.check_bool_operand(fndef, condition);
                self.check_expr(fndef, condition);
                self.check_body(fndef, body);
            }
            AstNode::ForIterator(iterate, _, _, body) => {
                self.check_expr(fndef, iterate);
                self.check_body(fndef, body);
            }
            AstNode::ForTradition(init, condition, update, body) => {
                self.check_stmt(fndef, init);
                self.check_bool_operand(fndef, condition);
                self.check_expr(fndef, condition);
                self.check_stmt(fndef, update);
                self.check_body(fndef, body);
            }
            AstNode::Return(expr) => {
                if let Some(expr) = expr {
                    self.check_expr(fndef, expr);
                }
            }
            AstNode::Ret(expr) => self.check_expr(fndef, expr),
            AstNode::Throw(error_expr) => self.check_expr(fndef, error_expr),
            AstNode::Catch(try_expr, _, catch_body) => {
                self.check_expr(fndef, try_expr);
                self.check_body(fndef, catch_body);
            }
            AstNode::TryCatch(try_body, _, catch_body) => {
                self.check_body(fndef, try_body);
                self.check_body(fndef, catch_body);
            }
            AstNode::Let(expr) => self.check_expr(fndef, expr),
            AstNode::Match(subject, cases) => {
                if let Some(subject) = subject {
                    self.check_expr(fndef, subject);
                }
                self.check_match_cases(fndef, cases);
            }
            AstNode::Select(cases, _, _, _) => self.check_select_cases(fndef, cases),
            AstNode::ConstDef(constdef_mutex) => {
                let constdef = constdef_mutex.lock().unwrap();
                self.check_expr(fndef, &constdef.right);
            }
            _ => {}
        }
    }

    fn check_body(&mut self, fndef: &AstFnDef, body: &AstBody) {
        for stmt in &body.stmts {
            self.check_stmt(fndef, stmt);
        }
    }
}
