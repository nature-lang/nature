//! Completion handler.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::analyzer::completion::{CompletionItemKind, CompletionProvider};

use super::Backend;

impl Backend {
    pub(crate) async fn handle_completion(&self, params: CompletionParams) -> Option<CompletionResponse> {
        debug!("completion requested");

        let uri = params.text_document_position.text_document.uri;
        let position: Position = params.text_document_position.position;

        let file_path = uri.path();
        let project = self.get_file_project(&file_path)?;

        if project.is_building.load(std::sync::atomic::Ordering::SeqCst) {
            return None;
        }

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let mut module_db = project.module_db.lock().ok()?;
        let module = module_db.get_mut(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let byte_offset = line_char + position.character as usize;

        let text = rope.to_string();
        debug!("Getting completions at byte_offset {}, module_ident '{}'", byte_offset, module.ident.clone());

        let mut symbol_table = project.symbol_table.lock().ok()?;
        let package_config = project.package_config.lock().ok()?.clone();
        let workspace_index = project.workspace_index.lock().ok()?;

        let completion_items = CompletionProvider::new(
            &mut symbol_table,
            module,
            project.nature_root.clone(),
            project.root.clone(),
            package_config,
        )
        .with_workspace_index(&workspace_index)
        .get_completions(byte_offset, &text);

        let lsp_items: Vec<tower_lsp::lsp_types::CompletionItem> = completion_items
            .into_iter()
            .map(|item| {
                let lsp_kind = match item.kind {
                    CompletionItemKind::Variable => tower_lsp::lsp_types::CompletionItemKind::VARIABLE,
                    CompletionItemKind::Parameter => tower_lsp::lsp_types::CompletionItemKind::VARIABLE,
                    CompletionItemKind::Function => tower_lsp::lsp_types::CompletionItemKind::FUNCTION,
                    CompletionItemKind::Constant => tower_lsp::lsp_types::CompletionItemKind::CONSTANT,
                    CompletionItemKind::Module => tower_lsp::lsp_types::CompletionItemKind::MODULE,
                    CompletionItemKind::Struct => tower_lsp::lsp_types::CompletionItemKind::STRUCT,
                    CompletionItemKind::Keyword => tower_lsp::lsp_types::CompletionItemKind::KEYWORD,
                };

                let has_snippet = item.insert_text.contains("$0");

                let additional_edits = if !item.additional_text_edits.is_empty() {
                    Some(
                        item.additional_text_edits
                            .into_iter()
                            .map(|edit| tower_lsp::lsp_types::TextEdit {
                                range: tower_lsp::lsp_types::Range {
                                    start: tower_lsp::lsp_types::Position {
                                        line: edit.line as u32,
                                        character: edit.character as u32,
                                    },
                                    end: tower_lsp::lsp_types::Position {
                                        line: edit.line as u32,
                                        character: edit.character as u32,
                                    },
                                },
                                new_text: edit.new_text,
                            })
                            .collect(),
                    )
                } else {
                    None
                };

                tower_lsp::lsp_types::CompletionItem {
                    label: item.label,
                    kind: Some(lsp_kind),
                    detail: item.detail,
                    documentation: item.documentation.map(|doc| tower_lsp::lsp_types::Documentation::String(doc)),
                    insert_text: Some(item.insert_text),
                    insert_text_format: if has_snippet {
                        Some(tower_lsp::lsp_types::InsertTextFormat::SNIPPET)
                    } else {
                        Some(tower_lsp::lsp_types::InsertTextFormat::PLAIN_TEXT)
                    },
                    sort_text: item.sort_text,
                    additional_text_edits: additional_edits,
                    ..Default::default()
                }
            })
            .collect();

        debug!("Returning {} completion items", lsp_items.len());
        Some(CompletionResponse::Array(lsp_items))
    }
}
