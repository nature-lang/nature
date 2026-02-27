//! Inlay hints: type annotations and parameter names.

use tower_lsp::lsp_types::*;

use crate::utils::offset_to_position;

use super::config::{cfg_bool, CFG_INLAY_HINTS_ENABLED, CFG_INLAY_PARAM_HINTS, CFG_INLAY_TYPE_HINTS};
use super::hover::format_type_display;
use super::Backend;

impl Backend {
    pub(crate) async fn handle_inlay_hint(&self, params: InlayHintParams) -> Option<Vec<InlayHint>> {
        let uri = params.text_document.uri;
        let range = params.range;

        if !cfg_bool(&self.config, CFG_INLAY_HINTS_ENABLED, true) {
            return None;
        }

        let show_type_hints = cfg_bool(&self.config, CFG_INLAY_TYPE_HINTS, true);
        let show_param_hints = cfg_bool(&self.config, CFG_INLAY_PARAM_HINTS, false);

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        // Skip while a build is in progress to avoid reading partial data.
        if project.is_building.load(std::sync::atomic::Ordering::SeqCst) {
            return None;
        }

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            *module_handled.get(file_path)?
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };

        let mut hints = Vec::new();

        if show_type_hints {
            for fndef in &module.all_fndefs {
                // Skip individual fndefs whose lock is contended (e.g. during a
                // concurrent build) instead of aborting the entire handler.
                if let Ok(fndef) = fndef.lock() {
                    collect_var_type_hints(&fndef.body.stmts, rope, &range, &mut hints);
                }
            }
            collect_var_type_hints_from_stmts(&module.stmts, rope, &range, &mut hints);
        }

        if show_param_hints {
            let symbol_table = project.symbol_table.lock().ok()?;
            collect_param_hints_from_stmts(&module.stmts, rope, &range, &mut hints, &symbol_table);
            for fndef in &module.all_fndefs {
                if let Ok(fndef) = fndef.lock() {
                    collect_param_hints(&fndef.body.stmts, rope, &range, &mut hints, &symbol_table);
                }
            }
        }

        let hints = if hints.is_empty() { None } else { Some(hints) };
        hints
    }
}

// ─── Inlay hint helpers ─────────────────────────────────────────────────────────

/// Collect type hints for `var` declarations whose type was inferred.
fn collect_var_type_hints(
    stmts: &[Box<crate::analyzer::common::Stmt>],
    rope: &ropey::Rope,
    visible_range: &Range,
    hints: &mut Vec<InlayHint>,
) {
    use crate::analyzer::common::AstNode;

    for stmt in stmts {
        match &stmt.node {
            AstNode::VarDef(var_decl, _) => {
                if let Ok(vd) = var_decl.lock() {
                    if vd.type_.start == 0 && vd.type_.end == 0 && !vd.type_.kind.is_unknown() {
                        if let Some(pos) = offset_to_position(vd.symbol_end, rope) {
                            if pos.line >= visible_range.start.line && pos.line <= visible_range.end.line {
                                let type_str = format_type_display(&vd.type_);
                                if type_str != "unknown" && type_str != "void" {
                                    hints.push(InlayHint {
                                        position: pos,
                                        label: InlayHintLabel::String(format!(": {}", type_str)),
                                        kind: Some(InlayHintKind::TYPE),
                                        text_edits: None,
                                        tooltip: None,
                                        padding_left: Some(false),
                                        padding_right: Some(true),
                                        data: None,
                                    });
                                }
                            }
                        }
                    }
                }
            }
            AstNode::If(_, consequent, alternate) => {
                collect_var_type_hints(&consequent.stmts, rope, visible_range, hints);
                collect_var_type_hints(&alternate.stmts, rope, visible_range, hints);
            }
            AstNode::ForCond(_, body) => {
                collect_var_type_hints(&body.stmts, rope, visible_range, hints);
            }
            AstNode::ForIterator(_, _, _, body) => {
                collect_var_type_hints(&body.stmts, rope, visible_range, hints);
            }
            AstNode::ForTradition(_, _, _, body) => {
                collect_var_type_hints(&body.stmts, rope, visible_range, hints);
            }
            AstNode::TryCatch(try_body, _, catch_body) => {
                collect_var_type_hints(&try_body.stmts, rope, visible_range, hints);
                collect_var_type_hints(&catch_body.stmts, rope, visible_range, hints);
            }
            _ => {}
        }
    }
}

/// Collect type hints from top-level statements.
fn collect_var_type_hints_from_stmts(
    stmts: &[Box<crate::analyzer::common::Stmt>],
    rope: &ropey::Rope,
    visible_range: &Range,
    hints: &mut Vec<InlayHint>,
) {
    collect_var_type_hints(stmts, rope, visible_range, hints);
}

/// Collect parameter-name hints at call sites inside statements.
fn collect_param_hints_from_stmts(
    stmts: &[Box<crate::analyzer::common::Stmt>],
    rope: &ropey::Rope,
    visible_range: &Range,
    hints: &mut Vec<InlayHint>,
    symbol_table: &crate::analyzer::symbol::SymbolTable,
) {
    collect_param_hints(stmts, rope, visible_range, hints, symbol_table);
}

/// Walk statements looking for `Call` nodes and emit parameter-name hints.
fn collect_param_hints(
    stmts: &[Box<crate::analyzer::common::Stmt>],
    rope: &ropey::Rope,
    visible_range: &Range,
    hints: &mut Vec<InlayHint>,
    symbol_table: &crate::analyzer::symbol::SymbolTable,
) {
    use crate::analyzer::common::AstNode;

    for stmt in stmts {
        collect_param_hints_from_node(&stmt.node, rope, visible_range, hints, symbol_table);

        match &stmt.node {
            AstNode::If(_, consequent, alternate) => {
                collect_param_hints(&consequent.stmts, rope, visible_range, hints, symbol_table);
                collect_param_hints(&alternate.stmts, rope, visible_range, hints, symbol_table);
            }
            AstNode::ForCond(_, body) => {
                collect_param_hints(&body.stmts, rope, visible_range, hints, symbol_table);
            }
            AstNode::ForIterator(_, _, _, body) => {
                collect_param_hints(&body.stmts, rope, visible_range, hints, symbol_table);
            }
            AstNode::ForTradition(_, _, _, body) => {
                collect_param_hints(&body.stmts, rope, visible_range, hints, symbol_table);
            }
            AstNode::TryCatch(try_body, _, catch_body) => {
                collect_param_hints(&try_body.stmts, rope, visible_range, hints, symbol_table);
                collect_param_hints(&catch_body.stmts, rope, visible_range, hints, symbol_table);
            }
            _ => {}
        }
    }
}

/// Try to extract parameter-name hints from a single AST node.
fn collect_param_hints_from_node(
    node: &crate::analyzer::common::AstNode,
    rope: &ropey::Rope,
    visible_range: &Range,
    hints: &mut Vec<InlayHint>,
    symbol_table: &crate::analyzer::symbol::SymbolTable,
) {
    use crate::analyzer::common::AstNode;

    match node {
        AstNode::Call(call) => {
            emit_call_param_hints(call, rope, visible_range, hints, symbol_table);
        }
        AstNode::VarDef(_, expr) => {
            if let AstNode::Call(call) = &expr.node {
                emit_call_param_hints(call, rope, visible_range, hints, symbol_table);
            }
        }
        AstNode::Assign(_, expr) => {
            if let AstNode::Call(call) = &expr.node {
                emit_call_param_hints(call, rope, visible_range, hints, symbol_table);
            }
        }
        AstNode::Ret(expr) | AstNode::Throw(expr) => {
            if let AstNode::Call(call) = &expr.node {
                emit_call_param_hints(call, rope, visible_range, hints, symbol_table);
            }
        }
        _ => {}
    }
}

/// Emit parameter-name hints for a specific function call.
fn emit_call_param_hints(
    call: &crate::analyzer::common::AstCall,
    rope: &ropey::Rope,
    visible_range: &Range,
    hints: &mut Vec<InlayHint>,
    symbol_table: &crate::analyzer::symbol::SymbolTable,
) {
    use crate::analyzer::common::AstNode;

    let param_names: Vec<String> = match &call.left.node {
        AstNode::Ident(_, symbol_id) if *symbol_id > 0 => {
            resolve_fn_param_names(*symbol_id, symbol_table)
        }
        AstNode::SelectExpr(left, key, _) => {
            // Try to resolve the impl function through the symbol table.
            // Build the impl ident: "{type_ident}.{method_name}"
            let base_type = match &left.type_.kind {
                crate::analyzer::common::TypeKind::Ref(v) | crate::analyzer::common::TypeKind::Ptr(v) => v.as_ref(),
                _ => &left.type_,
            };
            if !base_type.ident.is_empty() {
                let impl_ident = format!("{}.{}", base_type.ident, key);
                if let Some(symbol_id) = symbol_table.find_symbol_id(&impl_ident, symbol_table.global_scope_id) {
                    // Skip "self" since SelectExpr calls don't include it in args
                    let mut names = resolve_fn_param_names(symbol_id, symbol_table);
                    if names.first().map_or(false, |n| n == "self") {
                        names.remove(0);
                    }
                    names
                } else {
                    return; // cannot resolve param names
                }
            } else {
                return; // no type ident, cannot resolve
            }
        }
        _ => return,
    };

    if param_names.is_empty() {
        return;
    }

    for (i, arg) in call.args.iter().enumerate() {
        if i >= param_names.len() {
            break;
        }

        let param_name = &param_names[i];
        if param_name.is_empty() || param_name == "self" {
            continue;
        }

        if let AstNode::Ident(arg_name, _) = &arg.node {
            if arg_name == param_name {
                continue;
            }
        }

        if call.args.len() == 1 {
            if let AstNode::Literal(_, _) = &arg.node {
                continue;
            }
        }

        if let Some(pos) = offset_to_position(arg.start, rope) {
            if pos.line >= visible_range.start.line && pos.line <= visible_range.end.line {
                hints.push(InlayHint {
                    position: pos,
                    label: InlayHintLabel::String(format!("{}:", param_name)),
                    kind: Some(InlayHintKind::PARAMETER),
                    text_edits: None,
                    tooltip: None,
                    padding_left: Some(false),
                    padding_right: Some(true),
                    data: None,
                });
            }
        }
    }
}

/// Resolve parameter names for a function given its symbol ID.
fn resolve_fn_param_names(
    symbol_id: crate::analyzer::symbol::NodeId,
    symbol_table: &crate::analyzer::symbol::SymbolTable,
) -> Vec<String> {
    use crate::analyzer::symbol::SymbolKind;

    let symbol = match symbol_table.get_symbol_ref(symbol_id) {
        Some(s) => s,
        None => return Vec::new(),
    };

    let fndef = match &symbol.kind {
        SymbolKind::Fn(fndef) => fndef.clone(),
        _ => return Vec::new(),
    };

    if let Ok(fd) = fndef.lock() {
        return fd.params.iter().filter_map(|p| {
            p.lock().ok().map(|vd| vd.ident.clone())
        }).collect();
    }

    Vec::new()
}
