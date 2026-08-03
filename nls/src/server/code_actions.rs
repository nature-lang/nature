//! Code actions: quick-fixes and refactoring operations.
//!
//! Currently supports:
//! - **Remove unused import** — offer to delete import lines that have an
//!   `UNNECESSARY` diagnostic published by `build_diagnostics`.
//! - **Organize imports** — sort imports alphabetically, grouped by kind:
//!   std library → package/dotted → relative file.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::utils::offset_to_position;

use super::Backend;

/// Prefix used in diagnostic messages for unused imports.
pub(crate) const UNUSED_IMPORT_MSG_PREFIX: &str = "is imported but never used";

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_code_action(
        &self,
        params: CodeActionParams,
    ) -> Option<Vec<CodeActionOrCommand>> {
        let uri = &params.text_document.uri;
        let file_path = uri.path();

        // Grab module data then release locks.
        let module_data = {
            let project = self.get_file_project(file_path)?;
            let module_index = {
                let mh = project.module_handled.lock().ok()?;
                *mh.get(file_path)?
            };
            let module_db = project.module_db.lock().ok()?;
            let module = module_db.get(module_index)?;

            let import_ranges: Vec<(usize, usize)> = module
                .dependencies
                .iter()
                .filter(|imp| imp.start > 0 || imp.end > 0)
                .map(|imp| (imp.start, imp.end))
                .collect();

            ModuleData {
                source: module.source.clone(),
                rope: module.rope.clone(),
                import_ranges,
            }
        };

        let mut actions: Vec<CodeActionOrCommand> = Vec::new();

        // ── Remove unused import quick-fixes ────────────────────────────
        for diag in &params.context.diagnostics {
            let is_unused_import = diag
                .tags
                .as_ref()
                .map_or(false, |t| t.contains(&DiagnosticTag::UNNECESSARY))
                && diag.message.contains(UNUSED_IMPORT_MSG_PREFIX);

            if !is_unused_import {
                continue;
            }

            debug!("code action for unused import: {:?}", diag.range);

            let delete_range = expand_to_full_line(&module_data.source, &diag.range);

            let mut changes = std::collections::HashMap::new();
            changes.insert(
                uri.clone(),
                vec![TextEdit {
                    range: delete_range,
                    new_text: String::new(),
                }],
            );

            actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                title: "Remove unused import".into(),
                kind: Some(CodeActionKind::QUICKFIX),
                diagnostics: Some(vec![diag.clone()]),
                edit: Some(WorkspaceEdit {
                    changes: Some(changes),
                    ..Default::default()
                }),
                is_preferred: Some(true),
                ..Default::default()
            }));
        }

        // ── Aggregate "Remove all" when ≥ 2 ────────────────────────────
        if actions.len() > 1 {
            let mut all_edits: Vec<TextEdit> = Vec::new();
            let mut all_diags: Vec<Diagnostic> = Vec::new();
            for action in &actions {
                if let CodeActionOrCommand::CodeAction(ca) = action {
                    if let Some(ref edit) = ca.edit {
                        if let Some(ref changes) = edit.changes {
                            if let Some(edits) = changes.get(uri) {
                                all_edits.extend(edits.clone());
                            }
                        }
                    }
                    if let Some(ref ds) = ca.diagnostics {
                        all_diags.extend(ds.clone());
                    }
                }
            }

            let mut changes = std::collections::HashMap::new();
            changes.insert(uri.clone(), all_edits);

            actions.push(CodeActionOrCommand::CodeAction(CodeAction {
                title: "Remove all unused imports".into(),
                kind: Some(CodeActionKind::QUICKFIX),
                diagnostics: Some(all_diags),
                edit: Some(WorkspaceEdit {
                    changes: Some(changes),
                    ..Default::default()
                }),
                is_preferred: Some(false),
                ..Default::default()
            }));
        }

        // ── Organize imports ────────────────────────────────────────────
        if let Some(organize_action) = build_organize_imports_action(uri, &module_data) {
            actions.push(organize_action);
        }

        if actions.is_empty() {
            None
        } else {
            Some(actions)
        }
    }
}

/// Temporary struct to carry module data out of the lock scope.
struct ModuleData {
    source: String,
    rope: ropey::Rope,
    import_ranges: Vec<(usize, usize)>,
}

// ─── Organize imports ───────────────────────────────────────────────────────────

/// Build a `source.organizeImports` code action that sorts imports by group:
///   1. Standard library (simple ident: `fmt`, `time`, `co`)
///   2. Package/dotted imports (`testpkg.lib.math`, `co.mutex`)
///   3. Relative file imports (`'mod.n'`, `'utils.n'.{add}`)
///
/// Within each group, imports are sorted case-insensitively.
/// Groups are separated by a blank line.
fn build_organize_imports_action(
    uri: &Url,
    data: &ModuleData,
) -> Option<CodeActionOrCommand> {
    if data.import_ranges.is_empty() {
        return None;
    }

    // Extract each import's source text (start/end are char offsets).
    let chars: Vec<char> = data.source.chars().collect();
    let char_len = chars.len();
    let mut import_texts: Vec<String> = Vec::new();
    for &(start, end) in &data.import_ranges {
        if start < char_len && end <= char_len && start < end {
            let text: String = chars[start..end].iter().collect();
            import_texts.push(text.trim_end().to_string());
        }
    }

    if import_texts.is_empty() {
        return None;
    }

    // Classify and sort.
    let mut std_imports: Vec<String> = Vec::new();
    let mut pkg_imports: Vec<String> = Vec::new();
    let mut file_imports: Vec<String> = Vec::new();

    for text in &import_texts {
        match classify_import(text) {
            ImportCategory::Std => std_imports.push(text.clone()),
            ImportCategory::Package => pkg_imports.push(text.clone()),
            ImportCategory::File => file_imports.push(text.clone()),
        }
    }

    std_imports.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));
    pkg_imports.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));
    file_imports.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));

    // Join groups with blank line separators.
    let mut groups: Vec<&Vec<String>> = Vec::new();
    if !std_imports.is_empty() {
        groups.push(&std_imports);
    }
    if !pkg_imports.is_empty() {
        groups.push(&pkg_imports);
    }
    if !file_imports.is_empty() {
        groups.push(&file_imports);
    }

    let sorted_text = groups
        .iter()
        .map(|g| g.join("\n"))
        .collect::<Vec<_>>()
        .join("\n\n");

    // Don't offer if already sorted.
    let current_text = import_texts.join("\n");
    if current_text == sorted_text {
        return None;
    }

    // Range covering all imports.
    let first_start = data.import_ranges.first()?.0;
    let last_end = data.import_ranges.last()?.1;

    let start_pos = offset_to_position(first_start, &data.rope)?;
    let end_offset = find_line_end_chars(&data.source, last_end);
    let end_pos = offset_to_position(end_offset, &data.rope)?;

    let range = Range::new(Position::new(start_pos.line, 0), end_pos);
    let new_text = format!("{}\n", sorted_text);

    let mut changes = std::collections::HashMap::new();
    changes.insert(uri.clone(), vec![TextEdit { range, new_text }]);

    Some(CodeActionOrCommand::CodeAction(CodeAction {
        title: "Organize imports".into(),
        kind: Some(CodeActionKind::new("source.organizeImports")),
        diagnostics: None,
        edit: Some(WorkspaceEdit {
            changes: Some(changes),
            ..Default::default()
        }),
        is_preferred: Some(true),
        ..Default::default()
    }))
}

#[derive(Debug, PartialEq)]
enum ImportCategory {
    /// Simple standard library: `import fmt`, `import time`
    Std,
    /// Dotted package path: `import co.mutex`, `import testpkg.lib.math`
    Package,
    /// Relative file: `import 'mod.n'`, `import 'utils.n'.{add}`
    File,
}

fn classify_import(text: &str) -> ImportCategory {
    let after_import = text
        .trim_start()
        .strip_prefix("import")
        .unwrap_or(text)
        .trim_start();

    if after_import.starts_with('\'') || after_import.starts_with('"') {
        ImportCategory::File
    } else if after_import.contains('.') {
        ImportCategory::Package
    } else {
        ImportCategory::Std
    }
}

// ─── Shared helpers ─────────────────────────────────────────────────────────────

/// Expand a range to cover the full line(s), including trailing newline.
fn expand_to_full_line(source: &str, range: &Range) -> Range {
    let start_line = range.start.line;
    let end_line = range.end.line;
    let next_line = end_line + 1;
    let line_count = source.matches('\n').count() + 1;

    let end_pos = if (next_line as usize) < line_count {
        Position::new(next_line, 0)
    } else {
        let last_line_len = source.lines().last().map(|l| l.len() as u32).unwrap_or(0);
        Position::new(end_line, last_line_len)
    };

    Range::new(Position::new(start_line, 0), end_pos)
}

/// Find the end of the line containing char `offset` (past the `\n`).
fn find_line_end_chars(source: &str, offset: usize) -> usize {
    let chars: Vec<char> = source.chars().collect();
    let mut pos = offset;
    while pos < chars.len() {
        if chars[pos] == '\n' {
            return pos + 1;
        }
        pos += 1;
    }
    pos
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    // ── expand_to_full_line ─────────────────────────────────────────

    #[test]
    fn expand_to_full_line_middle() {
        let src = "line0\nline1\nline2\n";
        let range = Range::new(Position::new(1, 2), Position::new(1, 5));
        let expanded = expand_to_full_line(src, &range);
        assert_eq!(expanded.start, Position::new(1, 0));
        assert_eq!(expanded.end, Position::new(2, 0));
    }

    #[test]
    fn expand_to_full_line_first() {
        let src = "import fmt\nvar x = 1\n";
        let range = Range::new(Position::new(0, 0), Position::new(0, 10));
        let expanded = expand_to_full_line(src, &range);
        assert_eq!(expanded.start, Position::new(0, 0));
        assert_eq!(expanded.end, Position::new(1, 0));
    }

    #[test]
    fn expand_to_full_line_last_no_newline() {
        let src = "import fmt\nimport http";
        let range = Range::new(Position::new(1, 0), Position::new(1, 11));
        let expanded = expand_to_full_line(src, &range);
        assert_eq!(expanded.start, Position::new(1, 0));
        assert_eq!(expanded.end, Position::new(1, 11));
    }

    // ── classify_import ─────────────────────────────────────────────

    #[test]
    fn classify_std() {
        assert_eq!(classify_import("import fmt"), ImportCategory::Std);
        assert_eq!(classify_import("import time"), ImportCategory::Std);
    }

    #[test]
    fn classify_package() {
        assert_eq!(classify_import("import co.mutex"), ImportCategory::Package);
        assert_eq!(
            classify_import("import testpkg.lib.math.{add, square}"),
            ImportCategory::Package
        );
        assert_eq!(
            classify_import("import forest.app.create"),
            ImportCategory::Package
        );
    }

    #[test]
    fn classify_file() {
        assert_eq!(classify_import("import 'mod.n'"), ImportCategory::File);
        assert_eq!(
            classify_import("import 'utils.n'.{MAX_SIZE}"),
            ImportCategory::File
        );
        assert_eq!(
            classify_import("import 'mod.n' as *"),
            ImportCategory::File
        );
    }

    // ── sorting ─────────────────────────────────────────────────────

    #[test]
    fn sort_groups_correctly() {
        let mut std = vec!["import time".to_string(), "import fmt".to_string(), "import co".to_string()];
        let mut pkg = vec!["import co.mutex".to_string(), "import app.create".to_string()];
        let mut file = vec!["import 'z.n'".to_string(), "import 'a.n'".to_string()];

        std.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));
        pkg.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));
        file.sort_by(|a, b| a.to_lowercase().cmp(&b.to_lowercase()));

        assert_eq!(std, vec!["import co", "import fmt", "import time"]);
        assert_eq!(pkg, vec!["import app.create", "import co.mutex"]);
        assert_eq!(file, vec!["import 'a.n'", "import 'z.n'"]);
    }
}
