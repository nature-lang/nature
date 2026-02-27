//! Code actions (auto-import quick fixes).

use tower_lsp::lsp_types::*;

use crate::utils::{extract_symbol_from_diagnostic, offset_to_position};

use super::Backend;

impl Backend {
    pub(crate) async fn handle_code_action(&self, params: CodeActionParams) -> Option<Vec<CodeActionOrCommand>> {
        let uri = params.text_document.uri;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            *module_handled.get(file_path)?
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;
        let workspace_index = project.workspace_index.lock().ok()?;
        let package_config = project.package_config.lock().ok()?;
        let package_name = package_config.as_ref().map(|p| p.package_data.name.as_str());

        let mut actions: Vec<CodeActionOrCommand> = Vec::new();

        for diag in &params.context.diagnostics {
            let symbol_name = match extract_symbol_from_diagnostic(&diag.message) {
                Some(s) => s,
                None => continue,
            };

            let candidates = workspace_index.find_symbols_by_prefix(&symbol_name);
            let exact_matches: Vec<_> = candidates
                .iter()
                .filter(|s| s.name == symbol_name)
                .collect();

            if exact_matches.is_empty() {
                continue;
            }

            for indexed_symbol in &exact_matches {
                let import_info = workspace_index.compute_import_info(
                    &indexed_symbol.file_path,
                    &module.dir,
                    &module.path,
                    &project.root,
                    &project.nature_root,
                    package_name,
                );

                if let Some(info) = import_info {
                    let import_text = format!(
                        "import {} {{{}}}\n",
                        info.import_base.trim_start_matches("import "),
                        symbol_name,
                    );

                    let insert_pos = find_import_insert_position(module);

                    let mut changes = std::collections::HashMap::new();
                    changes.insert(
                        uri.clone(),
                        vec![TextEdit {
                            range: Range::new(insert_pos, insert_pos),
                            new_text: import_text.clone(),
                        }],
                    );

                    actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                        title: format!("Import '{}' from {}", symbol_name, info.module_as_name),
                        kind: Some(CodeActionKind::QUICKFIX),
                        diagnostics: Some(vec![diag.clone()]),
                        edit: Some(WorkspaceEdit {
                            changes: Some(changes),
                            ..Default::default()
                        }),
                        is_preferred: Some(exact_matches.len() == 1),
                        ..Default::default()
                    }));

                    let full_import_text = info.import_statement;
                    let mut full_changes = std::collections::HashMap::new();
                    full_changes.insert(
                        uri.clone(),
                        vec![TextEdit {
                            range: Range::new(insert_pos, insert_pos),
                            new_text: full_import_text,
                        }],
                    );

                    actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                        title: format!(
                            "Import module '{}' (use as {}.{})",
                            info.module_as_name, info.module_as_name, symbol_name,
                        ),
                        kind: Some(CodeActionKind::QUICKFIX),
                        diagnostics: Some(vec![diag.clone()]),
                        edit: Some(WorkspaceEdit {
                            changes: Some(full_changes),
                            ..Default::default()
                        }),
                        is_preferred: Some(false),
                        ..Default::default()
                    }));
                }
            }
        }

        if actions.is_empty() { None } else { Some(actions) }
    }
}

/// Find the position to insert a new import statement.
fn find_import_insert_position(module: &crate::project::Module) -> Position {
    use crate::analyzer::common::AstNode;

    let mut last_import_end: usize = 0;

    for stmt in &module.stmts {
        if let AstNode::Import(_) = &stmt.node {
            if stmt.end > last_import_end {
                last_import_end = stmt.end;
            }
        }
    }

    if last_import_end > 0 {
        offset_to_position(last_import_end, &module.rope).unwrap_or(Position::new(0, 0))
    } else {
        Position::new(0, 0)
    }
}
