use std::sync::{Arc, Mutex};

use crate::project::Module;

use super::common::{AnalyzerError, AstNode, Expr, Type, TypeKind, VarDeclExpr};
use super::symbol::{SymbolKind, SymbolTable};
use super::typesys::Typesys;

#[derive(Debug)]
pub struct GlobalEval<'a> {
    module: &'a mut Module,
    symbol_table: &'a mut SymbolTable,
    errors: Vec<AnalyzerError>,
}

impl<'a> GlobalEval<'a> {
    pub fn new(module: &'a mut Module, symbol_table: &'a mut SymbolTable) -> Self {
        Self {
            module,
            symbol_table,
            errors: Vec::new(),
        }
    }

    pub fn analyze(&mut self) -> Vec<AnalyzerError> {
        let mut global_vardefs = std::mem::take(&mut self.module.global_vardefs);
        let mut precheck_passed = vec![true; global_vardefs.len()];

        for (index, node) in global_vardefs.iter().enumerate() {
            let AstNode::VarDef(_, right_expr) = node else { continue };
            if let Err(e) = self.precheck_expr(right_expr.as_ref()) {
                self.errors.push(e);
                precheck_passed[index] = false;
            }
        }

        let mut infer_errors = Vec::new();
        {
            let mut typesys = Typesys::new(self.symbol_table, self.module);

            for (index, node) in global_vardefs.iter_mut().enumerate() {
                if !precheck_passed[index] {
                    continue;
                }

                let AstNode::VarDef(var_decl_mutex, right_expr) = node else {
                    continue;
                };

                let target_type = match Self::infer_global_vardef(&mut typesys, var_decl_mutex, right_expr) {
                    Ok(t) => t,
                    Err(e) => {
                        infer_errors.push(e);
                        continue;
                    }
                };

                if let Err(e) = Self::validate_initializer_layout(&target_type, right_expr.as_ref()) {
                    infer_errors.push(e);
                }
            }
        }

        self.module.global_vardefs = global_vardefs;
        self.errors.extend(infer_errors);
        self.errors.clone()
    }

    fn infer_global_vardef(typesys: &mut Typesys, var_decl_mutex: &Arc<Mutex<VarDeclExpr>>, right_expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        let (symbol_start, symbol_end, var_ident, mut var_type) = {
            let mut var_decl = var_decl_mutex.lock().unwrap();
            var_decl.type_ = typesys.reduction_type(var_decl.type_.clone())?;

            (var_decl.symbol_start, var_decl.symbol_end, var_decl.ident.clone(), var_decl.type_.clone())
        };

        if matches!(var_type.kind, TypeKind::Void) {
            return Err(AnalyzerError {
                start: symbol_start,
                end: symbol_end,
                message: "cannot assign to void".to_string(),
            });
        }

        let right_type = typesys.infer_right_expr(right_expr, var_type.clone())?;
        if matches!(right_type.kind, TypeKind::Void) {
            return Err(AnalyzerError {
                start: right_expr.start,
                end: right_expr.end,
                message: "cannot assign void to global var".to_string(),
            });
        }

        if var_type.kind.is_unknown() {
            if !typesys.type_confirm(&right_type) {
                return Err(AnalyzerError {
                    start: right_expr.start,
                    end: right_expr.end,
                    message: format!("global var {} type infer failed, right expr cannot confirm", var_ident),
                });
            }
            var_type = right_type;
        }

        var_type = typesys.reduction_type(var_type)?;
        if !typesys.type_confirm(&var_type) {
            return Err(AnalyzerError {
                start: right_expr.start,
                end: right_expr.end,
                message: "global type not confirmed".to_string(),
            });
        }

        {
            let mut var_decl = var_decl_mutex.lock().unwrap();
            var_decl.type_ = var_type.clone();
        }

        Ok(var_type)
    }

    fn precheck_expr(&self, expr: &Expr) -> Result<(), AnalyzerError> {
        match &expr.node {
            AstNode::Literal(..) | AstNode::EmptyCurlyNew | AstNode::MacroDefault(..) | AstNode::MacroReflectHash(..) => Ok(()),
            AstNode::Unary(_, operand) => self.precheck_expr(operand.as_ref()),
            AstNode::Binary(_, left, right) => {
                self.precheck_expr(left.as_ref())?;
                self.precheck_expr(right.as_ref())
            }
            AstNode::Ternary(condition, consequent, alternate) => {
                self.precheck_expr(condition.as_ref())?;
                self.precheck_expr(consequent.as_ref())?;
                self.precheck_expr(alternate.as_ref())
            }
            AstNode::As(_, union_tag, src) => {
                if let Some(tag_expr) = union_tag {
                    self.precheck_expr(tag_expr.as_ref())?;
                }
                self.precheck_expr(src.as_ref())
            }
            AstNode::ArrayNew(elements) | AstNode::TupleNew(elements) | AstNode::SetNew(elements) => {
                for item in elements {
                    self.precheck_expr(item.as_ref())?;
                }
                Ok(())
            }
            AstNode::ArrRepeatNew(default_element, length_expr) | AstNode::VecRepeatNew(default_element, length_expr) => {
                self.precheck_expr(default_element.as_ref())?;
                self.precheck_expr(length_expr.as_ref())
            }
            AstNode::VecNew(elements, _, _) => {
                for item in elements {
                    self.precheck_expr(item.as_ref())?;
                }
                Ok(())
            }
            AstNode::MapNew(elements) => {
                for item in elements {
                    self.precheck_expr(item.key.as_ref())?;
                    self.precheck_expr(item.value.as_ref())?;
                }
                Ok(())
            }
            AstNode::StructNew(_, _, properties) => {
                for property in properties {
                    self.precheck_expr(property.value.as_ref())?;
                }
                Ok(())
            }
            AstNode::Ident(ident, symbol_id) => self.precheck_ident(expr, ident, *symbol_id),
            _ => Err(Self::expr_error(
                expr,
                "global initializer expression is not compile-time evaluable".to_string(),
            )),
        }
    }

    fn precheck_ident(&self, expr: &Expr, ident: &str, symbol_id: usize) -> Result<(), AnalyzerError> {
        if symbol_id == 0 {
            return Err(Self::expr_error(expr, format!("ident '{}' undeclared", ident)));
        }

        let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) else {
            return Err(Self::expr_error(expr, format!("ident '{}' undeclared", ident)));
        };

        if matches!(&symbol.kind, SymbolKind::Var(_)) && !symbol.is_local {
            return Err(Self::expr_error(
                expr,
                format!("global initializer cannot reference global var '{}'", symbol.ident),
            ));
        }

        Err(Self::expr_error(expr, format!("global initializer cannot reference ident '{}'", ident)))
    }

    fn validate_initializer_layout(target_type: &Type, expr: &Expr) -> Result<(), AnalyzerError> {
        match &expr.node {
            AstNode::Literal(literal_kind, literal_value) => Self::validate_literal_assign(target_type, literal_kind, literal_value, expr),
            AstNode::As(_, _, src) => {
                if !matches!(src.node, AstNode::Literal(..)) {
                    return Err(Self::expr_error(expr, "global cast initializer must cast a literal".to_string()));
                }
                Self::validate_initializer_layout(target_type, src.as_ref())
            }
            AstNode::MacroDefault(..) | AstNode::MacroReflectHash(..) => Ok(()),
            AstNode::ArrayNew(elements) => {
                let TypeKind::Arr(_, expected_length, element_type) = &target_type.kind else {
                    return Err(Self::expr_error(expr, format!("array literal target type mismatch, expect '{}'", target_type)));
                };

                if elements.len() as u64 > *expected_length {
                    return Err(Self::expr_error(
                        expr,
                        format!("array literal length overflow, expect={}, actual={}", expected_length, elements.len()),
                    ));
                }

                for item in elements {
                    Self::validate_initializer_layout(element_type.as_ref(), item.as_ref())?;
                }
                Ok(())
            }
            AstNode::ArrRepeatNew(default_element, length_expr) => {
                let TypeKind::Arr(_, expected_length, element_type) = &target_type.kind else {
                    return Err(Self::expr_error(expr, format!("array repeat target type mismatch, expect '{}'", target_type)));
                };

                let AstNode::Literal(_, length_literal) = &length_expr.node else {
                    return Err(Self::expr_error(
                        length_expr.as_ref(),
                        "array repeat length must be compile-time literal".to_string(),
                    ));
                };

                let Some(length) = Self::parse_i64_literal(length_literal) else {
                    return Err(Self::expr_error(
                        length_expr.as_ref(),
                        "array repeat length must be integer literal".to_string(),
                    ));
                };

                if length < 0 || length as u64 != *expected_length {
                    return Err(Self::expr_error(
                        length_expr.as_ref(),
                        format!("array repeat length mismatch, expect={}, actual={}", expected_length, length),
                    ));
                }

                Self::validate_initializer_layout(element_type.as_ref(), default_element.as_ref())
            }
            AstNode::TupleNew(elements) => {
                let TypeKind::Tuple(expect_elements, _) = &target_type.kind else {
                    return Err(Self::expr_error(expr, format!("tuple literal target type mismatch, expect '{}'", target_type)));
                };

                if elements.len() != expect_elements.len() {
                    return Err(Self::expr_error(
                        expr,
                        format!("tuple element count mismatch, expect={}, actual={}", expect_elements.len(), elements.len()),
                    ));
                }

                for (item, expect_type) in elements.iter().zip(expect_elements.iter()) {
                    Self::validate_initializer_layout(expect_type, item.as_ref())?;
                }

                Ok(())
            }
            AstNode::StructNew(_, _, properties) => {
                let TypeKind::Struct(_, _, type_properties) = &target_type.kind else {
                    return Err(Self::expr_error(expr, format!("struct literal target type mismatch, expect '{}'", target_type)));
                };

                for property in properties {
                    let Some(expect_property) = type_properties.iter().find(|p| p.name == property.key) else {
                        return Err(Self::expr_error(property.value.as_ref(), format!("struct field '{}' not found", property.key)));
                    };

                    Self::validate_initializer_layout(&expect_property.type_, property.value.as_ref())?;
                }

                Ok(())
            }
            AstNode::VecNew(elements, _, _) => {
                if !matches!(target_type.kind, TypeKind::Vec(_)) {
                    return Err(Self::expr_error(expr, format!("vec literal target type mismatch, expect '{}'", target_type)));
                }

                if !elements.is_empty() {
                    return Err(Self::expr_error(expr, "global vec initializer must be empty".to_string()));
                }

                Ok(())
            }
            AstNode::MapNew(elements) => {
                if !matches!(target_type.kind, TypeKind::Map(_, _)) {
                    return Err(Self::expr_error(expr, format!("map literal target type mismatch, expect '{}'", target_type)));
                }

                if !elements.is_empty() {
                    return Err(Self::expr_error(expr, "global map initializer must be empty".to_string()));
                }

                Ok(())
            }
            AstNode::SetNew(elements) => {
                if !matches!(target_type.kind, TypeKind::Set(_)) {
                    return Err(Self::expr_error(expr, format!("set literal target type mismatch, expect '{}'", target_type)));
                }

                if !elements.is_empty() {
                    return Err(Self::expr_error(expr, "global set initializer must be empty".to_string()));
                }

                Ok(())
            }
            AstNode::EmptyCurlyNew => {
                if !matches!(target_type.kind, TypeKind::Map(_, _) | TypeKind::Set(_)) {
                    return Err(Self::expr_error(expr, "{} only supports map/set global initializer".to_string()));
                }
                Ok(())
            }
            AstNode::Unary(..) | AstNode::Binary(..) | AstNode::Ternary(..) => {
                Err(Self::expr_error(expr, "global initializer must fold to literal before layout".to_string()))
            }
            _ => Err(Self::expr_error(expr, "global initializer expression is unsupported".to_string())),
        }
    }

    fn validate_literal_assign(target_type: &Type, literal_kind: &TypeKind, literal_value: &str, expr: &Expr) -> Result<(), AnalyzerError> {
        if matches!(target_type.kind, TypeKind::String) {
            if !matches!(literal_kind, TypeKind::String) {
                return Err(Self::expr_error(expr, "global string initializer must be string literal".to_string()));
            }

            if !literal_value.is_empty() {
                return Err(Self::expr_error(expr, "global string initializer must be empty".to_string()));
            }
        }

        if Self::is_pointer_like(&target_type.kind) {
            if !Self::is_null_like_literal(literal_kind, literal_value) {
                return Err(Self::expr_error(expr, "pointer-like global initializer must be null".to_string()));
            }
        }

        if let TypeKind::Union(_, nullable, _) = &target_type.kind {
            if *nullable && !matches!(literal_kind, TypeKind::Null) {
                return Err(Self::expr_error(expr, "nullable union global initializer only supports null".to_string()));
            }
        }

        Ok(())
    }

    fn is_pointer_like(kind: &TypeKind) -> bool {
        matches!(
            kind,
            TypeKind::Ptr(_) | TypeKind::Ref(_) | TypeKind::Fn(_) | TypeKind::Chan(_) | TypeKind::CoroutineT | TypeKind::Null
        )
    }

    fn is_null_like_literal(kind: &TypeKind, value: &str) -> bool {
        if matches!(kind, TypeKind::Null) {
            return true;
        }

        if !Type::is_integer(kind) {
            return false;
        }

        Self::parse_i64_literal(value) == Some(0)
    }

    fn parse_i64_literal(value: &str) -> Option<i64> {
        let raw = value.trim();
        if raw.is_empty() {
            return None;
        }

        let (sign, digits) = if let Some(rest) = raw.strip_prefix('-') {
            (-1i64, rest)
        } else if let Some(rest) = raw.strip_prefix('+') {
            (1i64, rest)
        } else {
            (1i64, raw)
        };

        if digits.is_empty() {
            return None;
        }

        let (radix, body) = if let Some(rest) = digits.strip_prefix("0x") {
            (16, rest)
        } else if let Some(rest) = digits.strip_prefix("0o") {
            (8, rest)
        } else if let Some(rest) = digits.strip_prefix("0b") {
            (2, rest)
        } else {
            (10, digits)
        };

        if body.is_empty() {
            return None;
        }

        let unsigned = i64::from_str_radix(body, radix).ok()?;
        if sign < 0 {
            unsigned.checked_neg()
        } else {
            Some(unsigned)
        }
    }

    fn expr_error(expr: &Expr, message: String) -> AnalyzerError {
        AnalyzerError {
            start: expr.start,
            end: expr.end,
            message,
        }
    }
}
