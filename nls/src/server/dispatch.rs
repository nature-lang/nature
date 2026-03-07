//! Document lifecycle handlers: open, change, save, close, config, watched files.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use log::debug;
use tower_lsp::lsp_types::*;

use crate::analyzer::module_unique_ident;
use crate::package::parse_package;
use crate::project::Project;
use crate::utils::offset_to_position;

use super::Backend;

/// Internal helper carrying document info for [`Backend::on_change`].
pub(crate) struct TextDocumentItem<'a> {
    pub uri: Url,
    pub text: &'a str,
    pub version: Option<i32>,
}

impl Backend {
    // ── Lifecycle handlers ──────────────────────────────────────────────

    pub(crate) async fn handle_did_open(&self, params: DidOpenTextDocumentParams) {
        debug!("file opened: {}", params.text_document.uri);

        let file_path = params.text_document.uri.path();
        let file_dir = std::path::Path::new(file_path).parent();

        // Ensure a project exists for this file.
        if let Some(dir) = file_dir {
            let (project_root, log_msg) = if let Some(pkg_dir) = self.find_package_dir(dir) {
                (pkg_dir.to_string_lossy().to_string(), "found project at")
            } else {
                (
                    dir.to_string_lossy().to_string(),
                    "creating project from file directory:",
                )
            };

            if !self.projects.contains_key(&project_root) {
                debug!("{} {}", log_msg, project_root);
                let project = Project::new(project_root.clone()).await;
                project.start_queue_worker();
                debug!("project created: {}", project_root);
                self.projects.insert(project_root, project);
            }
        }

        // Track the document in our store.
        self.documents.open(
            &params.text_document.uri,
            &params.text_document.text,
            params.text_document.version,
            params.text_document.language_id.clone(),
        );

        self.on_change(TextDocumentItem {
            uri: params.text_document.uri,
            text: &params.text_document.text,
            version: Some(params.text_document.version),
        })
        .await;

        let _ = self.client.semantic_tokens_refresh().await;
    }

    pub(crate) async fn handle_did_change(&self, params: DidChangeTextDocumentParams) {
        // Apply incremental edits and get the resulting full text.
        let full_text = match self.documents.apply_changes(
            &params.text_document.uri,
            params.text_document.version,
            &params.content_changes,
        ) {
            Some(text) => text,
            None => {
                debug!(
                    "did_change for unknown document: {}",
                    params.text_document.uri
                );
                return;
            }
        };

        // Debounce: only trigger on_change for the latest version.
        let file_path = params.text_document.uri.path().to_string();
        let counter = self
            .debounce_versions
            .entry(file_path)
            .or_insert_with(|| Arc::new(AtomicU64::new(0)))
            .clone();
        let my_version = counter.fetch_add(1, Ordering::SeqCst) + 1;

        let delay = self.debounce_delay();
        tokio::time::sleep(std::time::Duration::from_millis(delay)).await;

        if counter.load(Ordering::SeqCst) != my_version {
            debug!(
                "debounce: skipping stale change (version {} superseded)",
                my_version
            );
            return;
        }

        self.on_change(TextDocumentItem {
            text: &full_text,
            uri: params.text_document.uri,
            version: Some(params.text_document.version),
        })
        .await;

        let _ = self.client.semantic_tokens_refresh().await;
    }

    pub(crate) async fn handle_did_save(&self, params: DidSaveTextDocumentParams) {
        if let Some(text) = params.text {
            self.on_change(TextDocumentItem {
                uri: params.text_document.uri,
                text: &text,
                version: None,
            })
            .await;
            let _ = self.client.semantic_tokens_refresh().await;
        }
        debug!("file saved");
    }

    pub(crate) async fn handle_did_close(&self, params: DidCloseTextDocumentParams) {
        self.documents.close(&params.text_document.uri);
        debug!("file closed: {}", params.text_document.uri);
    }

    pub(crate) async fn handle_did_change_configuration(
        &self,
        params: DidChangeConfigurationParams,
    ) {
        debug!("configuration changed: {:?}", params.settings);

        let nature_section = params
            .settings
            .as_object()
            .and_then(|o| o.get("nature"))
            .cloned()
            .unwrap_or(params.settings);

        self.apply_config(&nature_section);

        // Also pull fresh config from the client.
        if let Ok(response) = self
            .client
            .configuration(vec![ConfigurationItem {
                scope_uri: None,
                section: Some("nature".into()),
            }])
            .await
        {
            if let Some(settings) = response.into_iter().next() {
                self.apply_config(&settings);
            }
        }
    }

    pub(crate) async fn handle_did_change_watched_files(
        &self,
        params: DidChangeWatchedFilesParams,
    ) {
        debug!("watched files changed");

        for change in params.changes {
            let file_path = change.uri.path();

            // .n file created/deleted → re-scan workspace index.
            if file_path.ends_with(".n") {
                if let Some(project) = self.get_file_project(file_path) {
                    if let Ok(mut ws_index) = project.workspace_index.lock() {
                        ws_index.scan_workspace(&project.root, &project.nature_root);
                        debug!("workspace re-indexed after .n file change: {}", file_path);
                    }
                }
                continue;
            }

            // package.toml changes → re-parse package config.
            if !file_path.ends_with("package.toml") {
                continue;
            }

            let Some(project) = self.get_file_project(file_path) else {
                debug!("no project for watched file: {}", file_path);
                continue;
            };

            match parse_package(file_path) {
                Ok(pkg) => {
                    *project.package_config.lock().unwrap() = Some(pkg);
                    self.client
                        .publish_diagnostics(change.uri.clone(), vec![], None)
                        .await;
                }
                Err(e) => {
                    if let Ok(content) = std::fs::read_to_string(file_path) {
                        let rope = ropey::Rope::from_str(&content);
                        let start = offset_to_position(e.start, &rope)
                            .unwrap_or(Position::new(0, 0));
                        let end =
                            offset_to_position(e.end, &rope).unwrap_or(Position::new(0, 0));
                        let diag = Diagnostic::new_simple(
                            Range::new(start, end),
                            format!("package.toml parse error: {}", e.message),
                        );
                        self.client
                            .publish_diagnostics(change.uri.clone(), vec![diag], None)
                            .await;
                    }
                }
            }
        }
    }

    // ── Core rebuild trigger ────────────────────────────────────────────

    /// Called after every meaningful document change. Runs the analysis
    /// pipeline and publishes diagnostics.
    pub(crate) async fn on_change(&self, params: TextDocumentItem<'_>) {
        debug!("on_change: {}", params.uri);

        let file_path = params.uri.path();
        let Some(mut project) = self.get_file_project(file_path) else {
            debug!("no project for file: {}, skipping", file_path);
            return;
        };

        if file_path.ends_with("package.toml") {
            debug!("skipping package.toml in on_change");
            return;
        }

        let module_ident = module_unique_ident(&project.root, file_path);
        debug!("building module: {}", module_ident);

        let module_index = project
            .build(file_path, &module_ident, Some(params.text.to_string()))
            .await;
        debug!("build complete");

        // Collect diagnostics for the changed module and its dependents.
        let all_diagnostics = {
            let module_db = project.module_db.lock().unwrap();
            let mut result: Vec<(String, Vec<Diagnostic>)> = Vec::new();

            if let Some(m) = module_db.get(module_index) {
                result.push((m.path.clone(), Self::build_diagnostics(m)));
                for &ref_idx in &m.references {
                    if let Some(dep) = module_db.get(ref_idx) {
                        result.push((dep.path.clone(), Self::build_diagnostics(dep)));
                    }
                }
            }

            result
        };

        // Publish diagnostics.
        for (path, diagnostics) in all_diagnostics {
            if path == file_path {
                self.client
                    .publish_diagnostics(params.uri.clone(), diagnostics, params.version)
                    .await;
            } else if let Ok(uri) = Url::from_file_path(&path) {
                self.client
                    .publish_diagnostics(uri, diagnostics, None)
                    .await;
            }
        }
    }

    /// Convert a module's analyzer errors into LSP diagnostics.
    /// Also detects unused imports and adds HINT-level diagnostics for them.
    pub fn build_diagnostics(m: &crate::project::Module) -> Vec<Diagnostic> {
        let mut seen = std::collections::HashSet::new();

        let mut diagnostics: Vec<Diagnostic> = m.analyzer_errors
            .iter()
            .filter(|e| e.end > 0)
            .filter(|e| seen.insert((e.start, e.end)))
            .filter_map(|e| {
                let start = offset_to_position(e.start, &m.rope)?;
                let end = offset_to_position(e.end, &m.rope)?;
                let severity = if e.is_warning {
                    DiagnosticSeverity::HINT
                } else {
                    DiagnosticSeverity::ERROR
                };
                Some(Diagnostic {
                    range: Range::new(start, end),
                    severity: Some(severity),
                    message: e.message.clone(),
                    tags: if e.is_warning {
                        Some(vec![DiagnosticTag::UNNECESSARY])
                    } else {
                        None
                    },
                    ..Default::default()
                })
            })
            .collect();

        // ── Unused import diagnostics ───────────────────────────────────
        for dep in &m.dependencies {
            if dep.as_name == "*" {
                continue;
            }
            if dep.as_name.is_empty() && !dep.is_selective {
                continue;
            }
            if dep.start == 0 && dep.end == 0 {
                continue;
            }

            let is_used = if dep.is_selective {
                if let Some(items) = &dep.select_items {
                    items.iter().any(|item| {
                        let name = item.alias.as_deref().unwrap_or(&item.ident);
                        is_identifier_used_in_source(&m.source, name, dep.start, dep.end)
                    })
                } else {
                    true
                }
            } else {
                is_identifier_used_in_source(&m.source, &dep.as_name, dep.start, dep.end)
            };

            if is_used {
                continue;
            }

            // Build a human-readable label for the import.
            let import_label = if dep.is_selective {
                if let Some(items) = &dep.select_items {
                    let names: Vec<&str> = items
                        .iter()
                        .map(|i| i.alias.as_deref().unwrap_or(i.ident.as_str()))
                        .collect();
                    format!("{}.{{{}}}", dep.module_ident, names.join(", "))
                } else {
                    dep.module_ident.clone()
                }
            } else {
                dep.as_name.clone()
            };

            let start = match offset_to_position(dep.start, &m.rope) {
                Some(p) => p,
                None => continue,
            };
            let end = match offset_to_position(dep.end, &m.rope) {
                Some(p) => p,
                None => continue,
            };

            diagnostics.push(Diagnostic {
                range: Range::new(start, end),
                severity: Some(DiagnosticSeverity::HINT),
                message: format!("'{}' is imported but never used", import_label),
                tags: Some(vec![DiagnosticTag::UNNECESSARY]),
                source: Some("nls".into()),
                ..Default::default()
            });
        }

        diagnostics
    }
}

/// Check whether `name` appears as an identifier in `source`, ignoring the
/// region between `skip_start..skip_end` (the import statement itself).
fn is_identifier_used_in_source(source: &str, name: &str, skip_start: usize, skip_end: usize) -> bool {
    if name.is_empty() {
        return true;
    }

    let bytes = source.as_bytes();
    let name_len = name.len();

    let mut pos = 0;
    while pos + name_len <= bytes.len() {
        if let Some(found) = source[pos..].find(name) {
            let abs_pos = pos + found;

            // Skip if inside the import statement range
            if abs_pos >= skip_start && abs_pos < skip_end {
                pos = abs_pos + name_len;
                continue;
            }

            // Check word boundaries
            let before_ok = abs_pos == 0 || !is_ident_char(bytes[abs_pos - 1]);
            let after_ok = abs_pos + name_len >= bytes.len()
                || !is_ident_char(bytes[abs_pos + name_len]);

            if before_ok && after_ok {
                return true;
            }

            pos = abs_pos + name_len;
        } else {
            break;
        }
    }

    false
}

fn is_ident_char(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}
