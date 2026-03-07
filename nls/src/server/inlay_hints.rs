//! Inlay hints: type annotations for variable declarations and parameter
//! labels at call sites.

use tower_lsp::lsp_types::*;

use crate::analyzer::common::{AstCall, AstNode, Expr, Stmt, TypeKind};
use crate::analyzer::symbol::SymbolKind;
use crate::project::{Module, Project};
use crate::server::config::{cfg_bool, CFG_INLAY_TYPE_HINTS, CFG_INLAY_PARAM_HINTS};
use crate::utils::{offset_to_position, position_to_char_offset};

use super::Backend;

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_inlay_hint(
        &self,
        params: InlayHintParams,
    ) -> Option<Vec<InlayHint>> {
        let type_hints = cfg_bool(&self.config, CFG_INLAY_TYPE_HINTS, false);
        let param_hints = cfg_bool(&self.config, CFG_INLAY_PARAM_HINTS, false);

        // Nothing to do if both kinds are disabled.
        if !type_hints && !param_hints {
            return Some(vec![]);
        }

        let file_path = params.text_document.uri.path();
        let project = self.get_file_project(file_path)?;
        build_inlay_hints(&project, file_path, params.range, type_hints, param_hints)
    }
}

// ─── Hint collection ────────────────────────────────────────────────────────────

/// Build inlay hints for the visible `range` in the given file.
fn build_inlay_hints(
    project: &Project,
    file_path: &str,
    range: Range,
    type_hints: bool,
    param_hints: bool,
) -> Option<Vec<InlayHint>> {
    let mh = project.module_handled.lock().ok()?;
    let &idx = mh.get(file_path)?;
    drop(mh);

    let db = project.module_db.lock().ok()?;
    let module = db.get(idx)?;

    let range_start = position_to_char_offset(range.start, &module.rope)?;
    let range_end = position_to_char_offset(range.end, &module.rope)?;

    let mut hints = Vec::new();

    let opts = HintOpts { type_hints, param_hints };

    // Walk top-level statements.
    collect_hints_from_stmts(&module.stmts, module, project, range_start, range_end, &opts, &mut hints);

    // Walk all function bodies.
    for fndef_mutex in &module.all_fndefs {
        let fndef = fndef_mutex.lock().unwrap();
        collect_hints_from_stmts(
            &fndef.body.stmts,
            module,
            project,
            range_start,
            range_end,
            &opts,
            &mut hints,
        );
    }

    Some(hints)
}

/// Which hint kinds are enabled.
struct HintOpts {
    type_hints: bool,
    param_hints: bool,
}

/// Collect inlay hints from a list of statements.
fn collect_hints_from_stmts(
    stmts: &[Box<Stmt>],
    module: &Module,
    project: &Project,
    range_start: usize,
    range_end: usize,
    opts: &HintOpts,
    hints: &mut Vec<InlayHint>,
) {
    for stmt in stmts {
        // Quick range check: skip statements entirely outside the visible range.
        if stmt.end < range_start || stmt.start > range_end {
            continue;
        }

        match &stmt.node {
            // ── Type hints for variable declarations ─────────────────
            AstNode::VarDef(var_mutex, _right) if opts.type_hints => {
                let var = var_mutex.lock().unwrap();
                if let Some(hint) = make_type_hint(&var.type_, var.symbol_end, &module.rope) {
                    hints.push(hint);
                }
            }

            // ── Recursively check if/for/etc. bodies ─────────────────
            AstNode::If(_, consequent, alternate) => {
                collect_hints_from_stmts(&consequent.stmts, module, project, range_start, range_end, opts, hints);
                collect_hints_from_stmts(&alternate.stmts, module, project, range_start, range_end, opts, hints);
            }
            AstNode::ForIterator(_, _, _, body) => {
                collect_hints_from_stmts(&body.stmts, module, project, range_start, range_end, opts, hints);
            }
            AstNode::ForCond(_, body) => {
                collect_hints_from_stmts(&body.stmts, module, project, range_start, range_end, opts, hints);
            }
            _ => {}
        }

        // ── Parameter hints at call sites ────────────────────────────
        if opts.param_hints {
            collect_call_hints_from_expr_in_node(&stmt.node, module, project, range_start, range_end, hints);
        }
    }
}

/// Walk an AST node looking for `Call` expressions to generate parameter hints.
fn collect_call_hints_from_expr_in_node(
    node: &AstNode,
    module: &Module,
    project: &Project,
    range_start: usize,
    range_end: usize,
    hints: &mut Vec<InlayHint>,
) {
    match node {
        AstNode::Call(call) => {
            collect_param_hints(call, module, project, hints);
            // Recurse into arguments (they may contain nested calls).
            for arg in &call.args {
                collect_call_hints_from_expr(arg, module, project, range_start, range_end, hints);
            }
            // Recurse into the callee expression.
            collect_call_hints_from_expr(&call.left, module, project, range_start, range_end, hints);
        }
        AstNode::VarDef(_, right) => {
            collect_call_hints_from_expr(right, module, project, range_start, range_end, hints);
        }
        AstNode::Assign(left, right) => {
            collect_call_hints_from_expr(left, module, project, range_start, range_end, hints);
            collect_call_hints_from_expr(right, module, project, range_start, range_end, hints);
        }
        AstNode::Return(Some(expr)) => {
            collect_call_hints_from_expr(expr, module, project, range_start, range_end, hints);
        }
        AstNode::Ret(expr) => {
            collect_call_hints_from_expr(expr, module, project, range_start, range_end, hints);
        }
        AstNode::Fake(expr) => {
            collect_call_hints_from_expr(expr, module, project, range_start, range_end, hints);
        }
        _ => {}
    }
}

/// Walk an expression tree looking for `Call` nodes.
fn collect_call_hints_from_expr(
    expr: &Expr,
    module: &Module,
    project: &Project,
    range_start: usize,
    range_end: usize,
    hints: &mut Vec<InlayHint>,
) {
    if expr.end < range_start || expr.start > range_end {
        return;
    }
    collect_call_hints_from_expr_in_node(&expr.node, module, project, range_start, range_end, hints);
}

// ─── Type hints ─────────────────────────────────────────────────────────────────

/// Create a type-annotation inlay hint placed after the variable name
/// (right after `symbol_end`).
fn make_type_hint(
    type_: &crate::analyzer::common::Type,
    symbol_end: usize,
    rope: &ropey::Rope,
) -> Option<InlayHint> {
    // Don't show hints for unknown / void types.
    if matches!(type_.kind, TypeKind::Unknown | TypeKind::Void) {
        return None;
    }

    let position = offset_to_position(symbol_end, rope)?;
    let label = format!(": {}", type_display(type_));

    Some(InlayHint {
        position,
        label: InlayHintLabel::String(label),
        kind: Some(InlayHintKind::TYPE),
        text_edits: None,
        tooltip: None,
        padding_left: None,
        padding_right: None,
        data: None,
    })
}

/// Prefer the type's `ident` for display (e.g., `MyStruct` instead of `struct {...}`).
fn type_display(t: &crate::analyzer::common::Type) -> String {
    if !t.ident.is_empty() {
        t.ident.clone()
    } else {
        t.to_string()
    }
}

// ─── Parameter hints ────────────────────────────────────────────────────────────

/// Generate parameter-name hints for a function call's arguments.
fn collect_param_hints(
    call: &AstCall,
    module: &Module,
    project: &Project,
    hints: &mut Vec<InlayHint>,
) {
    // Resolve the callee to its function definition to get param names.
    let param_names = resolve_call_param_names(call, project);
    let param_names = match param_names {
        Some(names) if !names.is_empty() => names,
        _ => return,
    };

    for (i, arg) in call.args.iter().enumerate() {
        let Some(name) = param_names.get(i) else {
            break;
        };

        // Skip if the argument already looks like the parameter name
        // (e.g., passing `name` to a param called `name`).
        if arg_matches_param(arg, name) {
            continue;
        }

        let Some(position) = offset_to_position(arg.start, &module.rope) else {
            continue;
        };

        hints.push(InlayHint {
            position,
            label: InlayHintLabel::String(format!("{}:", name)),
            kind: Some(InlayHintKind::PARAMETER),
            text_edits: None,
            tooltip: None,
            padding_left: None,
            padding_right: Some(true),
            data: None,
        });
    }
}

/// Extract parameter names from the function being called.
///
/// Resolution strategy:
///   1. If `left` is an `Ident(_, symbol_id)`, look up the symbol directly.
///   2. If the left expression's type is `Fn(TypeFn)`, we only have types, not
///      names — return `None` (no parameter hints for indirect calls).
fn resolve_call_param_names(call: &AstCall, project: &Project) -> Option<Vec<String>> {
    // Direct call via ident: `foo(a, b)`
    if let AstNode::Ident(_, symbol_id) = &call.left.node {
        return param_names_from_symbol(*symbol_id, project);
    }

    // Method call via select: `obj.method(a, b)` — StructSelect carries the
    // property type which may be a function.  Fall back to the left expr type.
    if let AstNode::StructSelect(_, _, prop) = &call.left.node {
        if let TypeKind::Fn(ref fn_type) = prop.type_.kind {
            // TypeFn only has types, not names. Try to find the fndef via name.
            return param_names_from_fn_name(&fn_type.name, project);
        }
    }

    None
}

/// Look up a symbol by id and extract param names if it's a function.
fn param_names_from_symbol(symbol_id: usize, project: &Project) -> Option<Vec<String>> {
    let st = project.symbol_table.lock().ok()?;
    let symbol = st.get_symbol_ref(symbol_id)?;

    match &symbol.kind {
        SymbolKind::Fn(fndef_mutex) => {
            let fndef = fndef_mutex.lock().unwrap();
            Some(extract_param_names(&fndef))
        }
        _ => None,
    }
}

/// Try to find a function by name in all modules' global fndefs.
fn param_names_from_fn_name(name: &str, project: &Project) -> Option<Vec<String>> {
    if name.is_empty() {
        return None;
    }
    let db = project.module_db.lock().ok()?;
    for module in db.iter() {
        for fndef_mutex in &module.all_fndefs {
            let fndef = fndef_mutex.lock().unwrap();
            if fndef.fn_name == name || fndef.symbol_name == name {
                return Some(extract_param_names(&fndef));
            }
        }
    }
    None
}

/// Extract non-self parameter names from a function definition.
fn extract_param_names(fndef: &crate::analyzer::common::AstFnDef) -> Vec<String> {
    fndef
        .params
        .iter()
        .filter_map(|p| {
            let p = p.lock().unwrap();
            if p.ident == "self" {
                None
            } else {
                Some(p.ident.clone())
            }
        })
        .collect()
}

/// Check whether an argument expression is just an ident matching the param name.
fn arg_matches_param(arg: &Expr, param_name: &str) -> bool {
    if let AstNode::Ident(name, _) = &arg.node {
        name == param_name
    } else {
        false
    }
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::common::{Type, VarDeclExpr};
    use ropey::Rope;

    #[test]
    fn type_hint_for_i64() {
        let t = Type::new(TypeKind::Int64);
        let rope = Rope::from_str("var x = 42");
        // symbol_end = 5 (after "var x")
        let hint = make_type_hint(&t, 5, &rope).expect("should produce hint");
        match &hint.label {
            InlayHintLabel::String(s) => assert!(s.contains("i64"), "got: {}", s),
            _ => panic!("expected string label"),
        }
        assert_eq!(hint.kind, Some(InlayHintKind::TYPE));
    }

    #[test]
    fn no_type_hint_for_unknown() {
        let t = Type::new(TypeKind::Unknown);
        let rope = Rope::from_str("var x = ???");
        assert!(make_type_hint(&t, 5, &rope).is_none());
    }

    #[test]
    fn no_type_hint_for_void() {
        let t = Type::new(TypeKind::Void);
        let rope = Rope::from_str("var x = noop()");
        assert!(make_type_hint(&t, 5, &rope).is_none());
    }

    #[test]
    fn type_display_prefers_ident() {
        let mut t = Type::new(TypeKind::Struct("MyStruct".into(), 0, vec![]));
        t.ident = "MyStruct".into();
        assert_eq!(type_display(&t), "MyStruct");
    }

    #[test]
    fn arg_matches_param_ident() {
        let expr = Expr {
            start: 0,
            end: 3,
            type_: Type::default(),
            target_type: Type::default(),
            node: AstNode::Ident("foo".into(), 0),
        };
        assert!(arg_matches_param(&expr, "foo"));
        assert!(!arg_matches_param(&expr, "bar"));
    }

    #[test]
    fn extract_param_names_skips_self() {
        use crate::analyzer::common::AstFnDef;
        use std::sync::{Arc, Mutex};

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
            params: vec![self_param, x_param],
            ..AstFnDef::default()
        };
        let names = extract_param_names(&fndef);
        assert_eq!(names, vec!["x"]);
    }
}
