//! Signature help handler and helpers.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::utils::is_ident_char;

use super::hover::format_type_display;
use super::navigation::find_innermost_scope;
use super::Backend;

impl Backend {
    pub(crate) async fn handle_signature_help(&self, params: SignatureHelpParams) -> Option<SignatureHelp> {
        let uri = params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        if project.is_building.load(std::sync::atomic::Ordering::SeqCst) {
            return None;
        }

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;

        let text = rope.to_string();
        let (fn_name, active_param) = find_call_context(&text, char_offset)?;

        debug!("signature_help: fn='{}', active_param={}", fn_name, active_param);

        let symbol_table = project.symbol_table.lock().ok()?;
        let current_scope_id = find_innermost_scope(&symbol_table, module.scope_id, char_offset);

        resolve_signature_help(&symbol_table, module, &fn_name, current_scope_id, active_param)
    }
}

// ─── Signature helpers ──────────────────────────────────────────────────────────

/// Walk backwards from the cursor to find the function name being called and the active parameter index.
fn find_call_context(text: &str, cursor: usize) -> Option<(String, u32)> {
    let chars: Vec<char> = text.chars().collect();
    if cursor == 0 || cursor > chars.len() {
        return None;
    }

    let mut depth = 0i32;
    let mut comma_count = 0u32;
    let mut i = cursor.saturating_sub(1);

    loop {
        let ch = chars[i];
        match ch {
            ')' => depth += 1,
            '(' => {
                if depth == 0 {
                    if i == 0 {
                        return None;
                    }
                    let mut end = i;
                    while end > 0 && chars[end - 1].is_whitespace() {
                        end -= 1;
                    }
                    let mut start = end;
                    while start > 0 && is_ident_char(chars[start - 1]) {
                        start -= 1;
                    }
                    if start == end {
                        return None;
                    }
                    let fn_name: String = chars[start..end].iter().collect();
                    return Some((fn_name, comma_count));
                }
                depth -= 1;
            }
            ',' if depth == 0 => comma_count += 1,
            _ => {}
        }
        if i == 0 {
            break;
        }
        i -= 1;
    }

    None
}

/// Resolve signature help for a function call.
fn resolve_signature_help(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module: &crate::project::Module,
    fn_name: &str,
    current_scope_id: crate::analyzer::symbol::NodeId,
    active_param: u32,
) -> Option<SignatureHelp> {
    use crate::analyzer::symbol::SymbolKind;
    use crate::utils::format_global_ident;

    let build_sig = |fndef: &crate::analyzer::common::AstFnDef, display_name: &str| -> SignatureHelp {
        let mut params = Vec::new();
        let mut label_parts = Vec::new();

        for param in &fndef.params {
            if let Ok(p) = param.lock() {
                let param_label = if p.ident == "self" {
                    match fndef.self_kind {
                        crate::analyzer::common::SelfKind::SelfRefT => "&self".to_string(),
                        crate::analyzer::common::SelfKind::SelfPtrT => "*self".to_string(),
                        _ => "self".to_string(),
                    }
                } else {
                    format!("{}: {}", p.ident, format_type_display(&p.type_))
                };
                params.push(ParameterInformation {
                    label: ParameterLabel::Simple(param_label.clone()),
                    documentation: None,
                });
                label_parts.push(param_label);
            }
        }

        let ret = format_type_display(&fndef.return_type);
        let mut label = format!("{}({})", display_name, label_parts.join(", "));
        if ret != "void" {
            label.push_str(&format!(": {}", ret));
        }
        if fndef.is_errable {
            label.push('!');
        }

        SignatureHelp {
            signatures: vec![SignatureInformation {
                label,
                documentation: None,
                parameters: Some(params),
                active_parameter: Some(active_param),
            }],
            active_signature: Some(0),
            active_parameter: Some(active_param),
        }
    };

    // 1. Local symbol lookup
    if let Some(symbol_id) = symbol_table.lookup_symbol(fn_name, current_scope_id) {
        if let Some(symbol) = symbol_table.get_symbol_ref(symbol_id) {
            if let SymbolKind::Fn(fndef_mutex) = &symbol.kind {
                let fndef = fndef_mutex.lock().ok()?;
                return Some(build_sig(&fndef, fn_name));
            }
        }
    }

    // 2. Global symbol
    let global_ident = format_global_ident(module.ident.clone(), fn_name.to_string());
    if let Some(symbol) = symbol_table.find_global_symbol(&global_ident) {
        if let SymbolKind::Fn(fndef_mutex) = &symbol.kind {
            let fndef = fndef_mutex.lock().ok()?;
            return Some(build_sig(&fndef, fn_name));
        }
    }

    // 3. Selective imports
    for import in &module.dependencies {
        if import.is_selective {
            if let Some(ref items) = import.select_items {
                for item in items {
                    let effective_name = item.alias.as_deref().unwrap_or(&item.ident);
                    if effective_name == fn_name {
                        let imported_global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                        if let Some(symbol) = symbol_table.find_global_symbol(&imported_global_ident) {
                            if let SymbolKind::Fn(fndef_mutex) = &symbol.kind {
                                let fndef = fndef_mutex.lock().ok()?;
                                return Some(build_sig(&fndef, fn_name));
                            }
                        }
                    }
                }
            }
        }
    }

    None
}
