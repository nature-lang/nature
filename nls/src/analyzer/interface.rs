use std::collections::HashMap;

use super::{
    common::{AnalyzerError, AstFnDef, AstNode, ReductionStatus, SelfKind, Type, TypeFn, TypeKind, TypedefStmt},
    symbol::SymbolTable,
    typesys::Typesys,
};
use crate::{project::Module, utils::format_impl_ident};

fn push_error(errors: &mut Vec<AnalyzerError>, start: usize, end: usize, message: String) {
    errors.push(AnalyzerError { start, end, message });
}

fn interface_equal(typesys: &mut Typesys, left: &Type, right: &Type) -> bool {
    if left.ident.is_empty() || right.ident.is_empty() {
        return false;
    }

    if left.ident != right.ident {
        return false;
    }

    if left.args.len() != right.args.len() {
        return false;
    }

    for (left_arg, right_arg) in left.args.iter().zip(right.args.iter()) {
        let Ok(reduced_left_arg) = typesys.reduction_type(left_arg.clone()) else {
            return false;
        };
        let Ok(reduced_right_arg) = typesys.reduction_type(right_arg.clone()) else {
            return false;
        };

        if !typesys.type_compare(&reduced_left_arg, &reduced_right_arg) {
            return false;
        }
    }

    true
}

pub(crate) fn check_impl_interface_contains(typesys: &mut Typesys, typedef_stmt: &TypedefStmt, find_target_interface: &Type) -> bool {
    if typedef_stmt.impl_interfaces.is_empty() {
        return false;
    }

    for impl_interface in &typedef_stmt.impl_interfaces {
        if interface_equal(typesys, impl_interface, find_target_interface) {
            return true;
        }

        let Some(impl_typedef) = typesys.find_global_typedef(&impl_interface.ident) else {
            continue;
        };

        if check_impl_interface_contains(typesys, &impl_typedef, find_target_interface) {
            return true;
        }
    }

    false
}

fn combination_interface(typesys: &mut Typesys, typedef_stmt: &mut TypedefStmt) -> Result<(), AnalyzerError> {
    let TypeKind::Interface(origin_elements) = &mut typedef_stmt.type_expr.kind else {
        return Err(AnalyzerError {
            start: typedef_stmt.symbol_start,
            end: typedef_stmt.symbol_end,
            message: "typedef type is not interface".to_string(),
        });
    };

    let mut exists = HashMap::new();
    for element in origin_elements.clone() {
        if let TypeKind::Fn(type_fn) = &element.kind {
            exists.insert(type_fn.name.clone(), element);
        }
    }

    for impl_interface in &mut typedef_stmt.impl_interfaces {
        *impl_interface = typesys.reduction_type(impl_interface.clone())?;

        if !matches!(impl_interface.kind, TypeKind::Interface(..)) {
            return Err(AnalyzerError {
                start: impl_interface.start,
                end: impl_interface.end,
                message: format!("interface '{}' impl target '{}' is not interface", typedef_stmt.ident, impl_interface.ident),
            });
        }

        let TypeKind::Interface(elements) = &impl_interface.kind else {
            unreachable!();
        };

        for element in elements {
            let TypeKind::Fn(type_fn) = &element.kind else {
                return Err(AnalyzerError {
                    start: element.start,
                    end: element.end,
                    message: format!("interface '{}' contains non-fn method", typedef_stmt.ident),
                });
            };

            if let Some(exist_type) = exists.get(&type_fn.name) {
                if !typesys.type_compare(element, exist_type) {
                    return Err(AnalyzerError {
                        start: element.start,
                        end: element.end,
                        message: format!("duplicate method '{}'", type_fn.name),
                    });
                }
                continue;
            }

            exists.insert(type_fn.name.clone(), element.clone());
            origin_elements.push(element.clone());
        }
    }

    Ok(())
}

fn interface_extract_fn_type(typesys: &mut Typesys, fndef: &AstFnDef) -> Result<Type, AnalyzerError> {
    if fndef.impl_type.kind.is_unknown() {
        return Err(AnalyzerError {
            start: fndef.symbol_start,
            end: fndef.symbol_end,
            message: "cannot infer function without interface".to_string(),
        });
    }

    let mut type_fn = TypeFn {
        name: fndef.fn_name.clone(),
        tpl: fndef.is_tpl,
        errable: fndef.is_errable,
        rest: fndef.rest_param,
        fx: fndef.is_fx,
        param_types: Vec::new(),
        return_type: typesys.reduction_type(fndef.return_type.clone())?,
    };

    for (i, param) in fndef.params.iter().enumerate() {
        if !fndef.is_static && fndef.self_kind != SelfKind::Null && i == 0 {
            continue;
        }

        let param_type = {
            let temp_param = param.lock().unwrap();
            temp_param.type_.clone()
        };

        let param_type = typesys.reduction_type(param_type)?;
        type_fn.param_types.push(param_type);
    }

    let mut result = Type::new(TypeKind::Fn(Box::new(type_fn)));
    result.status = ReductionStatus::Done;
    Ok(result)
}

fn check_typedef_impl(typesys: &mut Typesys, impl_interface: &Type, typedef_ident: &str, typedef_stmt: &TypedefStmt) -> Result<(), String> {
    let TypeKind::Interface(elements) = &impl_interface.kind else {
        return Ok(());
    };

    for expect_type in elements {
        if expect_type.status != ReductionStatus::Done {
            return Err(format!("type '{}' not done", expect_type.ident));
        }

        let TypeKind::Fn(interface_fn_type) = &expect_type.kind else {
            return Err("interface element must be function type".to_string());
        };

        let fn_ident = format_impl_ident(typedef_ident.to_string(), interface_fn_type.name.clone());
        let Some(ast_fndef_mutex) = typedef_stmt.method_table.get(&fn_ident) else {
            return Err(format!(
                "type '{}' not impl fn '{}' for interface '{}'",
                typedef_ident, interface_fn_type.name, impl_interface.ident
            ));
        };

        let ast_fndef = ast_fndef_mutex.lock().unwrap().clone();
        let actual_type = interface_extract_fn_type(typesys, &ast_fndef).map_err(|e| e.message)?;

        if !typesys.type_compare(expect_type, &actual_type) {
            return Err(format!(
                "the fn '{}' of type '{}' mismatch interface '{}'",
                interface_fn_type.name, typedef_ident, impl_interface.ident
            ));
        }
    }

    Ok(())
}

pub struct Interface<'a> {
    module: &'a mut Module,
    symbol_table: &'a mut SymbolTable,
}

impl<'a> Interface<'a> {
    pub fn new(module: &'a mut Module, symbol_table: &'a mut SymbolTable) -> Self {
        Self { module, symbol_table }
    }

    pub fn analyze(&mut self) -> Vec<AnalyzerError> {
        let mut errors = Vec::new();
        let stmts = self.module.stmts.clone();
        let mut typesys = Typesys::new(self.symbol_table, self.module);

        for stmt in stmts {
            let AstNode::Typedef(typedef_mutex) = &stmt.node else {
                continue;
            };

            let mut typedef = {
                let typedef_stmt = typedef_mutex.lock().unwrap();
                if typedef_stmt.impl_interfaces.is_empty() {
                    continue;
                }
                typedef_stmt.clone()
            };

            if typedef.is_interface {
                match typesys.reduction_type(typedef.type_expr.clone()) {
                    Ok(reduced) => typedef.type_expr = reduced,
                    Err(e) => {
                        push_error(&mut errors, e.start, e.end, e.message);
                        continue;
                    }
                }

                if let Err(e) = combination_interface(&mut typesys, &mut typedef) {
                    push_error(&mut errors, e.start, e.end, e.message);
                }

                let mut target = typedef_mutex.lock().unwrap();
                target.type_expr = typedef.type_expr.clone();
                target.impl_interfaces = typedef.impl_interfaces.clone();
                continue;
            }

            for impl_interface in &mut typedef.impl_interfaces {
                match typesys.reduction_type(impl_interface.clone()) {
                    Ok(reduced) => *impl_interface = reduced,
                    Err(e) => {
                        push_error(&mut errors, e.start, e.end, e.message);
                        impl_interface.err = true;
                    }
                }
            }

            let typedef_ident = typedef.ident.clone();
            for impl_interface in typedef.impl_interfaces.clone() {
                if impl_interface.err {
                    continue;
                }

                if let Err(message) = check_typedef_impl(&mut typesys, &impl_interface, &typedef_ident, &typedef) {
                    push_error(&mut errors, impl_interface.start, impl_interface.end, message);
                }
            }

            let mut target = typedef_mutex.lock().unwrap();
            target.impl_interfaces = typedef.impl_interfaces.clone();
        }

        errors
    }
}
