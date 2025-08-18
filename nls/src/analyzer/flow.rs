use crate::analyzer::common::{AnalyzerError, AstCall, AstFnDef, AstNode, Expr, Stmt, TypeKind, VarDeclExpr};
use crate::project::Module;
use std::sync::{Arc, Mutex};

#[derive(Debug)]
pub struct Flow<'a> {
    module: &'a mut Module,
    errors: Vec<AnalyzerError>,
}

impl<'a> Flow<'a> {
    pub fn new(module: &'a mut Module) -> Self {
        Self { errors: Vec::new(), module }
    }

    pub fn analyze(&mut self) -> Vec<AnalyzerError> {
        for i in 0..self.module.all_fndefs.len() {
            let fndef_mutex = self.module.all_fndefs[i].clone();
            self.analyze_fndef(fndef_mutex);
        }

        return self.errors.clone();
    }

    pub fn analyze_fndef(&mut self, fndef_mutex: Arc<Mutex<AstFnDef>>) {
        let fndef = fndef_mutex.lock().unwrap();

        let (has_return, _) = self.analyze_body(&fndef.body);
        if !matches!(fndef.return_type.kind, TypeKind::Void) && !has_return {
            self.errors.push(AnalyzerError {
                start: fndef.symbol_end,
                end: fndef.symbol_end,
                message: format!("missing return"),
            });
        }
    }

    fn analyze_body(&mut self, stmts: &Vec<Box<Stmt>>) -> (bool, bool) {
        let mut has_return = false;
        let mut has_break = false;

        for stmt in stmts {
            let (item_has_return, item_has_break) = self.analyze_stmt(stmt);
            has_return = has_return || item_has_return;
            has_break = has_break || item_has_break;
        }

        (has_return, has_break)
    }

    fn analyze_stmt(&mut self, stmt: &Box<Stmt>) -> (bool, bool) {
        match &stmt.node {
            AstNode::Return(expr_opt) => {
                if let Some(expr) = expr_opt {
                    self.analyze_expr(expr);
                }
                (true, true)
            }
            AstNode::Throw(expr) => {
                self.analyze_expr(expr);
                (true, true)
            }
            AstNode::Break(_) => (false, true),
            AstNode::Assign(_, right) => {
                return self.analyze_expr(right); // need check return
            }
            AstNode::VarTupleDestr(elements, right) => {
                return self.analyze_expr(right); // need check return
            }
            AstNode::VarDef(_, right) => {
                return self.analyze_expr(right);
            }
            AstNode::Call(call) => {
                // 分析调用表达式中的参数
                for arg in &call.args {
                    self.analyze_expr(arg); // only need check match
                }
                (false, false)
            }
            AstNode::If(condition, then_stmts, else_stmts) => {
                self.analyze_expr(condition);
                let (then_has_return, then_has_break) = self.analyze_body(then_stmts);
                let (else_has_return, else_has_break) = if !else_stmts.is_empty() {
                    self.analyze_body(else_stmts)
                } else {
                    (false, false)
                };
                (then_has_return && else_has_return, then_has_break || else_has_break)
            }
            AstNode::Fake(expr) => self.analyze_expr(expr),

            _ => (false, false),
        }
    }

    fn analyze_expr(&mut self, expr: &Box<Expr>) -> (bool, bool) {
        match &expr.node {
            AstNode::Match(_, cases) => {
                if cases.is_empty() {
                    return (false, false);
                }

                let mut has_return = true;
                let mut has_break = true;
                for case in cases {
                    let (case_has_return, case_has_break) = self.analyze_body(&case.handle_body);
                    has_return = has_return && case_has_return;
                    has_break = has_break && case_has_break
                }

                // check has break
                if !matches!(&expr.target_type.kind, TypeKind::Void | TypeKind::Unknown) && has_break == false {
                    self.errors.push(AnalyzerError {
                        end: expr.end,
                        start: expr.end,
                        message: "missing break".to_string(),
                    });
                }

                // has break 属于当前 match, 离开当前 match 后，break 作废
                (has_return, false)
            }
            _ => (false, false),
        }
    }
}
