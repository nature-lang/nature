//! Signature help: show the function signature and highlight the active
//! parameter while typing inside a call expression `fn_name(…|…)`.

use tower_lsp::lsp_types::*;

use crate::analyzer::common::{AstCall, AstNode, Expr, Stmt, TypeKind};
use crate::analyzer::symbol::SymbolKind;
use crate::project::{Module, Project};
use crate::utils::position_to_char_offset;

use super::Backend;

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_signature_help(
        &self,
        params: SignatureHelpParams,
    ) -> Option<SignatureHelp> {
        let uri = &params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;
        build_signature_help(&project, file_path, position)
    }
}

// ─── Signature help construction ────────────────────────────────────────────────

/// Build a `SignatureHelp` for the call expression surrounding the cursor.
fn build_signature_help(
    project: &Project,
    file_path: &str,
    position: Position,
) -> Option<SignatureHelp> {
    let mh = project.module_handled.lock().ok()?;
    let &idx = mh.get(file_path)?;
    drop(mh);

    let db = project.module_db.lock().ok()?;
    let module = db.get(idx)?;

    let char_offset = position_to_char_offset(position, &module.rope)?;

    // Find the innermost call that contains the cursor.
    let call_ctx = find_call_at_offset(&module.stmts, module, char_offset)?;

    // Resolve the callee to get parameter information.
    let info = resolve_signature_info(&call_ctx.call, project)?;

    let active_param = compute_active_parameter(&call_ctx.call, char_offset);

    Some(SignatureHelp {
        signatures: vec![info],
        active_signature: Some(0),
        active_parameter: Some(active_param),
    })
}

// ─── Call context ───────────────────────────────────────────────────────────────

/// A call expression found at the cursor position.
struct CallContext<'a> {
    call: &'a AstCall,
}

/// Search the AST for the innermost `Call` expression containing `offset`.
fn find_call_at_offset<'a>(
    stmts: &'a [Box<Stmt>],
    module: &'a Module,
    offset: usize,
) -> Option<CallContext<'a>> {
    // First scan top-level statements.
    if let Some(ctx) = find_call_in_stmts(stmts, offset) {
        return Some(ctx);
    }

    // Then scan function bodies.
    for fndef_mutex in &module.all_fndefs {
        let fndef = fndef_mutex.lock().unwrap();
        // Quick bounds check.
        if offset < fndef.body.start || offset > fndef.body.end {
            continue;
        }
        // Safety: We're reading the body stmts while holding the lock.
        // The stmts reference is valid for the duration of this block.
        // We need to use unsafe to extend the lifetime since the borrow
        // checker can't see that the data outlives the lock.
        let stmts_ptr = &fndef.body.stmts as *const Vec<Box<Stmt>>;
        drop(fndef);
        // SAFETY: module_db is borrowed immutably and all_fndefs are Arc<Mutex<>>.
        // The underlying data won't be mutated while we hold the module_db lock.
        let stmts = unsafe { &*stmts_ptr };
        if let Some(ctx) = find_call_in_stmts(stmts, offset) {
            return Some(ctx);
        }
    }

    None
}

/// Walk a list of statements looking for the innermost call at `offset`.
fn find_call_in_stmts<'a>(stmts: &'a [Box<Stmt>], offset: usize) -> Option<CallContext<'a>> {
    for stmt in stmts {
        if offset < stmt.start || offset > stmt.end {
            continue;
        }
        if let Some(ctx) = find_call_in_node(&stmt.node, offset) {
            return Some(ctx);
        }
    }
    None
}

/// Walk an AST node looking for the innermost call whose span covers `offset`.
fn find_call_in_node<'a>(node: &'a AstNode, offset: usize) -> Option<CallContext<'a>> {
    match node {
        AstNode::Call(call) => {
            // First check nested calls in arguments (innermost wins).
            for arg in &call.args {
                if offset >= arg.start && offset <= arg.end {
                    if let Some(inner) = find_call_in_node(&arg.node, offset) {
                        return Some(inner);
                    }
                }
            }
            // This call itself contains the cursor.
            Some(CallContext { call })
        }
        AstNode::VarDef(_, right) => find_call_in_expr(right, offset),
        AstNode::Assign(left, right) => {
            find_call_in_expr(left, offset).or_else(|| find_call_in_expr(right, offset))
        }
        AstNode::Return(Some(expr)) | AstNode::Ret(expr) | AstNode::Fake(expr) => {
            find_call_in_expr(expr, offset)
        }
        AstNode::If(cond, consequent, alternate) => {
            find_call_in_expr(cond, offset)
                .or_else(|| find_call_in_stmts(&consequent.stmts, offset))
                .or_else(|| find_call_in_stmts(&alternate.stmts, offset))
        }
        AstNode::ForIterator(iter_expr, _, _, body) => {
            find_call_in_expr(iter_expr, offset)
                .or_else(|| find_call_in_stmts(&body.stmts, offset))
        }
        AstNode::ForCond(cond, body) => {
            find_call_in_expr(cond, offset)
                .or_else(|| find_call_in_stmts(&body.stmts, offset))
        }
        _ => None,
    }
}

/// Walk an expression looking for calls.
fn find_call_in_expr<'a>(expr: &'a Expr, offset: usize) -> Option<CallContext<'a>> {
    if offset < expr.start || offset > expr.end {
        return None;
    }
    find_call_in_node(&expr.node, offset)
}

// ─── Signature resolution ───────────────────────────────────────────────────────

/// Resolve the callee of a `Call` to a `SignatureInformation`.
fn resolve_signature_info(call: &AstCall, project: &Project) -> Option<SignatureInformation> {
    // Direct ident call.
    if let AstNode::Ident(name, symbol_id) = &call.left.node {
        return signature_from_symbol(*symbol_id, name, project);
    }

    // Method call via StructSelect.
    if let AstNode::StructSelect(_, key, prop) = &call.left.node {
        if let TypeKind::Fn(ref fn_type) = prop.type_.kind {
            return signature_from_fn_name(&fn_type.name, key, project);
        }
    }

    // Fallback: try from the left expression's type.
    if let TypeKind::Fn(ref fn_type) = call.left.type_.kind {
        let label = format_signature_from_type(fn_type, &fn_type.name);
        return Some(SignatureInformation {
            label,
            documentation: None,
            parameters: None,
            active_parameter: None,
        });
    }

    None
}

/// Build a `SignatureInformation` from a symbol id (direct call).
fn signature_from_symbol(
    symbol_id: usize,
    name: &str,
    project: &Project,
) -> Option<SignatureInformation> {
    let st = project.symbol_table.lock().ok()?;
    let symbol = st.get_symbol_ref(symbol_id)?;

    match &symbol.kind {
        SymbolKind::Fn(fndef_mutex) => {
            let fndef = fndef_mutex.lock().unwrap();
            Some(build_signature_information(&fndef, name))
        }
        _ => None,
    }
}

/// Try to find a function by name across all modules.
fn signature_from_fn_name(
    fn_name: &str,
    display_name: &str,
    project: &Project,
) -> Option<SignatureInformation> {
    if fn_name.is_empty() {
        return None;
    }
    let db = project.module_db.lock().ok()?;
    for module in db.iter() {
        for fndef_mutex in &module.all_fndefs {
            let fndef = fndef_mutex.lock().unwrap();
            if fndef.fn_name == fn_name || fndef.symbol_name == fn_name {
                return Some(build_signature_information(&fndef, display_name));
            }
        }
    }
    None
}

/// Construct the full `SignatureInformation` including parameter labels.
fn build_signature_information(
    fndef: &crate::analyzer::common::AstFnDef,
    display_name: &str,
) -> SignatureInformation {
    let mut param_parts: Vec<String> = Vec::new();
    let mut parameters: Vec<ParameterInformation> = Vec::new();

    for param in &fndef.params {
        let p = param.lock().unwrap();

        if p.ident == "self" {
            // Don't include self in the parameter list for signature help.
            continue;
        }

        let type_str = type_display(&p.type_);
        let param_label = format!("{}: {}", p.ident, type_str);
        param_parts.push(param_label.clone());
        parameters.push(ParameterInformation {
            label: ParameterLabel::Simple(param_label),
            documentation: None,
        });
    }

    if fndef.rest_param {
        let rest_label = "...".to_string();
        param_parts.push(rest_label.clone());
        parameters.push(ParameterInformation {
            label: ParameterLabel::Simple(rest_label),
            documentation: None,
        });
    }

    let ret = &fndef.return_type;
    let mut label = format!("fn {}({}): {}", display_name, param_parts.join(", "), ret);
    if fndef.is_errable {
        label.push('!');
    }

    SignatureInformation {
        label,
        documentation: None,
        parameters: Some(parameters),
        active_parameter: None,
    }
}

/// Format a signature from a `TypeFn` (no parameter names available).
fn format_signature_from_type(
    fn_type: &crate::analyzer::common::TypeFn,
    name: &str,
) -> String {
    let params: Vec<String> = fn_type
        .param_types
        .iter()
        .map(|t| type_display(t))
        .collect();
    let mut sig = format!("fn {}({}): {}", name, params.join(", "), fn_type.return_type);
    if fn_type.errable {
        sig.push('!');
    }
    sig
}

/// Prefer the type's ident for display.
fn type_display(t: &crate::analyzer::common::Type) -> String {
    if !t.ident.is_empty() {
        t.ident.clone()
    } else {
        t.to_string()
    }
}

// ─── Active parameter ───────────────────────────────────────────────────────────

/// Determine which parameter is "active" based on the cursor offset relative
/// to the arguments in the call.
fn compute_active_parameter(call: &AstCall, offset: usize) -> u32 {
    if call.args.is_empty() {
        return 0;
    }

    // Count how many argument separators the cursor has passed.
    for (i, arg) in call.args.iter().enumerate().rev() {
        if offset >= arg.start {
            return i as u32;
        }
    }

    0
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::common::{AstFnDef, Type, TypeKind, VarDeclExpr};
    use std::sync::{Arc, Mutex};

    #[test]
    fn build_signature_info_simple() {
        let fndef = AstFnDef {
            fn_name: "add".into(),
            return_type: Type::new(TypeKind::Int64),
            ..AstFnDef::default()
        };
        let info = build_signature_information(&fndef, "add");
        assert!(info.label.contains("fn add()"), "got: {}", info.label);
        assert!(info.label.contains("i64"), "got: {}", info.label);
        assert!(info.parameters.as_ref().unwrap().is_empty());
    }

    #[test]
    fn build_signature_info_with_params() {
        let param_a = Arc::new(Mutex::new(VarDeclExpr {
            ident: "a".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 0,
            type_: Type::new(TypeKind::Int64),
            be_capture: false,
            heap_ident: None,
        }));
        let param_b = Arc::new(Mutex::new(VarDeclExpr {
            ident: "b".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 0,
            type_: Type::new(TypeKind::String),
            be_capture: false,
            heap_ident: None,
        }));

        let fndef = AstFnDef {
            fn_name: "greet".into(),
            params: vec![param_a, param_b],
            return_type: Type::new(TypeKind::Void),
            ..AstFnDef::default()
        };
        let info = build_signature_information(&fndef, "greet");
        assert!(
            info.label.contains("a: i64, b: string"),
            "got: {}",
            info.label
        );
        let params = info.parameters.unwrap();
        assert_eq!(params.len(), 2);
    }

    #[test]
    fn build_signature_info_skips_self() {
        let self_param = Arc::new(Mutex::new(VarDeclExpr {
            ident: "self".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 0,
            type_: Type::default(),
            be_capture: false,
            heap_ident: None,
        }));
        let x_param = Arc::new(Mutex::new(VarDeclExpr {
            ident: "x".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 0,
            type_: Type::new(TypeKind::Int64),
            be_capture: false,
            heap_ident: None,
        }));

        let fndef = AstFnDef {
            fn_name: "method".into(),
            params: vec![self_param, x_param],
            return_type: Type::new(TypeKind::Void),
            ..AstFnDef::default()
        };
        let info = build_signature_information(&fndef, "method");
        assert!(!info.label.contains("self"), "got: {}", info.label);
        let params = info.parameters.unwrap();
        assert_eq!(params.len(), 1);
        assert!(matches!(&params[0].label, ParameterLabel::Simple(s) if s.contains("x")));
    }

    #[test]
    fn active_parameter_empty_args() {
        let call = AstCall {
            return_type: Type::default(),
            left: Box::new(Expr::default()),
            generics_args: vec![],
            args: vec![],
            spread: false,
        };
        assert_eq!(compute_active_parameter(&call, 10), 0);
    }

    #[test]
    fn active_parameter_between_args() {
        let arg0 = Box::new(Expr {
            start: 5,
            end: 6,
            ..Expr::default()
        });
        let arg1 = Box::new(Expr {
            start: 8,
            end: 10,
            ..Expr::default()
        });
        let call = AstCall {
            return_type: Type::default(),
            left: Box::new(Expr::default()),
            generics_args: vec![],
            args: vec![arg0, arg1],
            spread: false,
        };

        // Cursor before first arg.
        assert_eq!(compute_active_parameter(&call, 3), 0);
        // Cursor at first arg.
        assert_eq!(compute_active_parameter(&call, 5), 0);
        // Cursor between args (after first, before second).
        assert_eq!(compute_active_parameter(&call, 7), 0);
        // Cursor at second arg.
        assert_eq!(compute_active_parameter(&call, 8), 1);
    }
}
