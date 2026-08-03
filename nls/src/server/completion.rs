//! Completion handler: delegates to the existing `CompletionProvider` in the
//! analyzer and converts its results into LSP `CompletionItem`s.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::analyzer::completion::{CompletionItemKind as AnalyzerKind, CompletionProvider};

use super::Backend;

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_completion(
        &self,
        params: CompletionParams,
    ) -> Option<CompletionResponse> {
        let uri = &params.text_document_position.text_document.uri;
        let position = params.text_document_position.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;

        // Acquire locks — ordering is module_handled → module_db →
        // symbol_table, matching build_inner, so no deadlock is possible.
        // We block briefly if a build is running rather than returning
        // empty completions (which causes VS Code to fall back to its
        // word-based suggestions).
        let module_index = {
            let mh = project.module_handled.lock().ok()?;
            *mh.get(file_path)?
        };

        let mut module_db = project.module_db.lock().ok()?;
        let module = module_db.get_mut(module_index)?;

        // Use the live document rope when available (more up-to-date than the
        // last-analysed rope).
        let live_doc = self.documents.get_by_path(file_path);
        let rope = match &live_doc {
            Some(d) => &d.rope,
            None => &module.rope,
        };

        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;
        let text = rope.to_string();

        debug!(
            "completion at offset={}, module='{}'",
            char_offset, module.ident
        );

        let mut symbol_table = project.symbol_table.lock().ok()?;
        let package_config = project.package_config.lock().ok()?.clone();
        let workspace_index = project.workspace_index.lock().ok()?;

        let items = CompletionProvider::new(
            &mut symbol_table,
            module,
            project.nature_root.clone(),
            project.root.clone(),
            package_config,
        )
        .with_workspace_index(&workspace_index)
        .get_completions(char_offset, &text);

        // Drop heavy guards before building LSP items.
        drop(workspace_index);
        drop(symbol_table);
        drop(module_db);

        let lsp_items: Vec<CompletionItem> = items.into_iter().map(to_lsp_item).collect();

        debug!("returning {} completions", lsp_items.len());
        Some(CompletionResponse::List(CompletionList {
            // Mark as incomplete so VS Code re-requests as the user
            // types more characters, allowing prefix-dependent sources
            // (std modules, workspace symbols, keywords) to appear.
            is_incomplete: true,
            items: lsp_items,
        }))
    }
}

// ─── Conversion ─────────────────────────────────────────────────────────────────

/// Map an analyzer `CompletionItem` → LSP `CompletionItem`.
fn to_lsp_item(item: crate::analyzer::completion::CompletionItem) -> CompletionItem {
    let kind = map_kind(&item.kind);
    let has_snippet = item.insert_text.contains("$0");

    let additional_text_edits = if item.additional_text_edits.is_empty() {
        None
    } else {
        Some(
            item.additional_text_edits
                .into_iter()
                .map(|edit| TextEdit {
                    range: Range {
                        start: Position {
                            line: edit.line as u32,
                            character: edit.character as u32,
                        },
                        end: Position {
                            line: edit.line as u32,
                            character: edit.character as u32,
                        },
                    },
                    new_text: edit.new_text,
                })
                .collect(),
        )
    };

    CompletionItem {
        label: item.label,
        kind: Some(kind),
        detail: item.detail,
        documentation: item
            .documentation
            .map(Documentation::String),
        insert_text: Some(item.insert_text),
        insert_text_format: Some(if has_snippet {
            InsertTextFormat::SNIPPET
        } else {
            InsertTextFormat::PLAIN_TEXT
        }),
        sort_text: item.sort_text,
        additional_text_edits,
        ..Default::default()
    }
}

/// Map analyzer completion kind → LSP completion kind.
fn map_kind(kind: &AnalyzerKind) -> CompletionItemKind {
    match kind {
        AnalyzerKind::Variable | AnalyzerKind::Parameter => CompletionItemKind::VARIABLE,
        AnalyzerKind::Function => CompletionItemKind::FUNCTION,
        AnalyzerKind::Constant => CompletionItemKind::CONSTANT,
        AnalyzerKind::Module => CompletionItemKind::MODULE,
        AnalyzerKind::Struct => CompletionItemKind::STRUCT,
        AnalyzerKind::Keyword => CompletionItemKind::KEYWORD,
    }
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::completion::{
        CompletionItem as AnalyzerItem, CompletionItemKind as AK, TextEdit as AnalyzerEdit,
    };

    #[test]
    fn map_kind_function() {
        assert_eq!(map_kind(&AK::Function), CompletionItemKind::FUNCTION);
    }

    #[test]
    fn map_kind_variable() {
        assert_eq!(map_kind(&AK::Variable), CompletionItemKind::VARIABLE);
    }

    #[test]
    fn map_kind_parameter_maps_to_variable() {
        assert_eq!(map_kind(&AK::Parameter), CompletionItemKind::VARIABLE);
    }

    #[test]
    fn map_kind_keyword() {
        assert_eq!(map_kind(&AK::Keyword), CompletionItemKind::KEYWORD);
    }

    #[test]
    fn to_lsp_plain_text() {
        let item = AnalyzerItem {
            label: "count".into(),
            kind: AK::Variable,
            detail: Some("var: i64".into()),
            documentation: None,
            insert_text: "count".into(),
            sort_text: None,
            additional_text_edits: vec![],
        };
        let lsp = to_lsp_item(item);
        assert_eq!(lsp.label, "count");
        assert_eq!(lsp.kind, Some(CompletionItemKind::VARIABLE));
        assert_eq!(lsp.insert_text_format, Some(InsertTextFormat::PLAIN_TEXT));
        assert!(lsp.additional_text_edits.is_none());
    }

    #[test]
    fn to_lsp_snippet() {
        let item = AnalyzerItem {
            label: "greet".into(),
            kind: AK::Function,
            detail: Some("fn(string): void".into()),
            documentation: None,
            insert_text: "greet($0)".into(),
            sort_text: Some("00000010".into()),
            additional_text_edits: vec![],
        };
        let lsp = to_lsp_item(item);
        assert_eq!(lsp.insert_text_format, Some(InsertTextFormat::SNIPPET));
        assert_eq!(lsp.insert_text.as_deref(), Some("greet($0)"));
    }

    #[test]
    fn to_lsp_with_auto_import_edit() {
        let item = AnalyzerItem {
            label: "fmt".into(),
            kind: AK::Module,
            detail: Some("import fmt".into()),
            documentation: None,
            insert_text: "fmt".into(),
            sort_text: None,
            additional_text_edits: vec![AnalyzerEdit {
                line: 0,
                character: 0,
                new_text: "import fmt\n".into(),
            }],
        };
        let lsp = to_lsp_item(item);
        let edits = lsp.additional_text_edits.expect("should have edits");
        assert_eq!(edits.len(), 1);
        assert_eq!(edits[0].new_text, "import fmt\n");
        assert_eq!(edits[0].range.start.line, 0);
    }
}
