//! Document lifecycle: open, change, save, close, configuration, watched files.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use log::debug;
use ropey::Rope;
use tower_lsp::lsp_types::*;

use crate::analyzer::module_unique_ident;
use crate::package::parse_package;
use crate::project::Project;
use crate::utils::offset_to_position;

use super::{Backend, TextDocumentItem};

impl Backend {
    pub(crate) async fn handle_did_open(&self, params: DidOpenTextDocumentParams) {
        debug!("file opened {}", params.text_document.uri);

        let file_path = params.text_document.uri.path();
        let file_dir = std::path::Path::new(file_path).parent();

        if let Some(dir) = file_dir {
            let (project_root, log_message) = if let Some(package_dir) = self.find_package_dir(dir) {
                (package_dir.to_string_lossy().to_string(), "Found new project at")
            } else {
                (dir.to_string_lossy().to_string(), "Creating new project using file directory as root:")
            };

            if !self.projects.contains_key(&project_root) {
                debug!("{} {}", log_message, project_root);
                let project = Project::new(project_root.clone()).await;
                project.backend_handle_queue();
                debug!("project new success root: {}", project_root);
                self.projects.insert(project_root, project);
            }
        }

        let file_path = params.text_document.uri.path().to_string();
        self.documents
            .insert(file_path, Rope::from_str(&params.text_document.text));

        self.on_change(TextDocumentItem {
            uri: params.text_document.uri,
            text: &params.text_document.text,
            version: Some(params.text_document.version),
        })
        .await
    }

    pub(crate) async fn handle_did_change(&self, params: DidChangeTextDocumentParams) {
        let file_path = params.text_document.uri.path().to_string();

        // Apply incremental edits atomically: hold the DashMap entry lock for
        // the entire read-modify-write cycle.  Without this, concurrent
        // did_change handlers (tower-lsp spawns them in parallel) can interleave
        // and lose edits, causing the server's rope to diverge from the editor.
        let full_text = {
            let mut entry = self
                .documents
                .entry(file_path.clone())
                .or_insert_with(|| Rope::from_str(""));

            for change in &params.content_changes {
                if let Some(range) = change.range {
                    let start_line = range.start.line as usize;
                    let start_char = range.start.character as usize;
                    let end_line = range.end.line as usize;
                    let end_char = range.end.character as usize;

                    let start_idx = entry
                        .try_line_to_char(start_line)
                        .map(|lc| lc + start_char)
                        .unwrap_or(0);
                    let end_idx = entry
                        .try_line_to_char(end_line)
                        .map(|lc| lc + end_char)
                        .unwrap_or(entry.len_chars());

                    let start_idx = start_idx.min(entry.len_chars());
                    let end_idx = end_idx.min(entry.len_chars());

                    if start_idx < end_idx {
                        entry.remove(start_idx..end_idx);
                    }
                    if !change.text.is_empty() {
                        entry.insert(start_idx, &change.text);
                    }
                } else {
                    *entry = Rope::from_str(&change.text);
                }
            }

            entry.to_string()
        };

        let counter = self
            .debounce_versions
            .entry(file_path)
            .or_insert_with(|| Arc::new(AtomicU64::new(0)))
            .clone();
        let my_version = counter.fetch_add(1, Ordering::SeqCst) + 1;

        let delay = self.debounce_delay();
        tokio::time::sleep(std::time::Duration::from_millis(delay)).await;

        if counter.load(Ordering::SeqCst) != my_version {
            debug!("debounce: skipping stale change (version {} superseded)", my_version);
            return;
        }

        self.on_change(TextDocumentItem {
            text: &full_text,
            uri: params.text_document.uri,
            version: Some(params.text_document.version),
        })
        .await
    }

    pub(crate) async fn handle_did_save(&self, params: DidSaveTextDocumentParams) {
        if let Some(text) = params.text {
            let item = TextDocumentItem {
                uri: params.text_document.uri,
                text: &text,
                version: None,
            };
            self.on_change(item).await;
            _ = self.client.semantic_tokens_refresh().await;
        }
        debug!("file saved!");
    }

    pub(crate) async fn handle_did_close(&self, params: DidCloseTextDocumentParams) {
        let file_path = params.text_document.uri.path().to_string();
        self.documents.remove(&file_path);
        debug!("file closed: {}", file_path);
    }

    pub(crate) async fn handle_did_change_configuration(&self, params: DidChangeConfigurationParams) {
        debug!("configuration changed: {:?}", params.settings);

        let nature_section = params
            .settings
            .as_object()
            .and_then(|o| o.get("nature"))
            .cloned()
            .unwrap_or(params.settings);

        self.apply_config(&nature_section);

        if let Ok(response) = self
            .client
            .configuration(vec![ConfigurationItem {
                scope_uri: None,
                section: Some("nature".to_string()),
            }])
            .await
        {
            if let Some(settings) = response.into_iter().next() {
                self.apply_config(&settings);
            }
        }
    }

    pub(crate) async fn handle_did_change_watched_files(&self, params: DidChangeWatchedFilesParams) {
        debug!("watched files changed!");

        for param in params.changes {
            let file_path = param.uri.path();

            if file_path.ends_with(".n") {
                if let Some(project) = self.get_file_project(&file_path) {
                    if let Ok(mut workspace_index) = project.workspace_index.lock() {
                        workspace_index.scan_workspace(&project.root, &project.nature_root);
                        debug!("workspace re-indexed after .n file change: {}", file_path);
                    }
                }
                continue;
            }

            if !file_path.ends_with("package.toml") {
                debug!("Ignoring non-package.toml watched file: {}", file_path);
                continue;
            }

            let Some(project) = self.get_file_project(&file_path) else {
                debug!("No project found for watched file: {}", file_path);
                continue;
            };

            match parse_package(&file_path) {
                Ok(package_conf) => {
                    {
                        let mut package_option = project.package_config.lock().unwrap();
                        *package_option = Some(package_conf);
                    }
                    self.client.publish_diagnostics(param.uri.clone(), vec![], None).await;
                }
                Err(e) => {
                    if let Ok(content) = std::fs::read_to_string(&file_path) {
                        let rope = ropey::Rope::from_str(&content);
                        let start_position = offset_to_position(e.start, &rope).unwrap_or(Position::new(0, 0));
                        let end_position = offset_to_position(e.end, &rope).unwrap_or(Position::new(0, 0));

                        let diagnostic = Diagnostic::new_simple(
                            Range::new(start_position, end_position),
                            format!("parser package.toml failed: {}", e.message),
                        );
                        self.client.publish_diagnostics(param.uri.clone(), vec![diagnostic], None).await;
                    }
                }
            }
        }

        debug!("watched files updated");
    }

    pub(crate) async fn handle_execute_command(&self, _: ExecuteCommandParams) -> Option<serde_json::Value> {
        debug!("command executed!");

        match self.client.apply_edit(WorkspaceEdit::default()).await {
            Ok(res) if res.applied => self.client.log_message(MessageType::INFO, "applied").await,
            Ok(_) => self.client.log_message(MessageType::INFO, "rejected").await,
            Err(err) => self.client.log_message(MessageType::ERROR, err).await,
        }

        None
    }

    // ── Internal helpers ────────────────────────────────────────────────

    pub(crate) async fn on_change<'a>(&self, params: TextDocumentItem<'a>) {
        debug!("on_change: {}", params.uri);

        let file_path = params.uri.path();
        let Some(mut project) = self.get_file_project(&file_path) else {
            debug!("No project found for file: {}, skipping on_change", file_path);
            return;
        };

        if file_path.ends_with("package.toml") {
            debug!("Skipping package.toml in on_change");
            return;
        }

        let module_ident = module_unique_ident(&project.root, &file_path);
        debug!("will build, module ident: {}", module_ident);

        let module_index = project.build(&file_path, &module_ident, Some(params.text.to_string())).await;
        debug!("build success");

        let all_diagnostics = {
            let module_db = project.module_db.lock().unwrap();
            let mut result: Vec<(String, Vec<Diagnostic>)> = Vec::new();

            if let Some(m) = module_db.get(module_index) {
                let diags = Self::build_diagnostics(m);
                result.push((m.path.clone(), diags));

                for &ref_index in &m.references {
                    if let Some(dep_m) = module_db.get(ref_index) {
                        let dep_diags = Self::build_diagnostics(dep_m);
                        result.push((dep_m.path.clone(), dep_diags));
                    }
                }
            }

            result
        };

        for (path, diagnostics) in all_diagnostics {
            if path == file_path {
                self.client.publish_diagnostics(params.uri.clone(), diagnostics, params.version).await;
            } else if let Ok(uri) = Url::from_file_path(&path) {
                self.client.publish_diagnostics(uri, diagnostics, None).await;
            }
        }
    }

    /// Build LSP diagnostics from a module's analyzer errors.
    pub(crate) fn build_diagnostics(m: &crate::project::Module) -> Vec<Diagnostic> {
        let mut seen_positions = std::collections::HashSet::new();

        m.analyzer_errors
            .iter()
            .filter(|error| error.end > 0)
            .filter(|error| {
                let position_key = (error.start, error.end);
                seen_positions.insert(position_key)
            })
            .filter_map(|error| {
                let start_position = offset_to_position(error.start, &m.rope)?;
                let end_position = offset_to_position(error.end, &m.rope)?;
                let severity = if error.is_warning {
                    DiagnosticSeverity::HINT
                } else {
                    DiagnosticSeverity::ERROR
                };
                Some(Diagnostic {
                    range: Range::new(start_position, end_position),
                    severity: Some(severity),
                    message: error.message.clone(),
                    tags: if error.is_warning {
                        Some(vec![DiagnosticTag::UNNECESSARY])
                    } else {
                        None
                    },
                    ..Default::default()
                })
            })
            .collect()
    }
}
