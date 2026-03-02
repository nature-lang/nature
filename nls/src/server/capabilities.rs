//! `initialize` / `initialized` — capability advertisement and startup.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::analyzer::lexer::LEGEND_TYPE;
use crate::project::Project;

use super::Backend;

/// Pure function that constructs `ServerCapabilities`.
///
/// Extracted so it can be snapshot-tested without needing an LSP client.
pub fn build_server_capabilities() -> ServerCapabilities {
    ServerCapabilities {
        // ── Text sync (incremental) ─────────────────────────────────
        text_document_sync: Some(TextDocumentSyncCapability::Options(
            TextDocumentSyncOptions {
                open_close: Some(true),
                change: Some(TextDocumentSyncKind::INCREMENTAL),
                save: Some(TextDocumentSyncSaveOptions::SaveOptions(SaveOptions {
                    include_text: Some(true),
                })),
                ..Default::default()
            },
        )),

        // ── Semantic tokens ─────────────────────────────────────────
        semantic_tokens_provider: Some(
            SemanticTokensServerCapabilities::SemanticTokensRegistrationOptions(
                SemanticTokensRegistrationOptions {
                    text_document_registration_options: TextDocumentRegistrationOptions {
                        document_selector: Some(vec![DocumentFilter {
                            language: Some("n".into()),
                            scheme: Some("file".into()),
                            pattern: None,
                        }]),
                    },
                    semantic_tokens_options: SemanticTokensOptions {
                        work_done_progress_options: Default::default(),
                        legend: SemanticTokensLegend {
                            token_types: LEGEND_TYPE.into(),
                            token_modifiers: vec![],
                        },
                        range: Some(true),
                        full: Some(SemanticTokensFullOptions::Bool(true)),
                    },
                    static_registration_options: Default::default(),
                },
            ),
        ),

        // ── Workspace ───────────────────────────────────────────────
        workspace: Some(WorkspaceServerCapabilities {
            workspace_folders: Some(WorkspaceFoldersServerCapabilities {
                supported: Some(true),
                change_notifications: Some(OneOf::Left(true)),
            }),
            file_operations: None,
        }),

        // ── Navigation (Tier 3 + 7) ────────────────────────────────────
        definition_provider: Some(OneOf::Left(true)),
        type_definition_provider: Some(TypeDefinitionProviderCapability::Simple(true)),
        implementation_provider: Some(ImplementationProviderCapability::Simple(true)),
        references_provider: Some(OneOf::Left(true)),
        document_symbol_provider: Some(OneOf::Left(true)),
        workspace_symbol_provider: Some(OneOf::Left(true)),

        // ── Inline information (Tier 4) ──────────────────────────────
        hover_provider: Some(HoverProviderCapability::Simple(true)),
        inlay_hint_provider: Some(OneOf::Left(true)),
        signature_help_provider: Some(SignatureHelpOptions {
            trigger_characters: Some(vec!["(".into(), ",".into()]),
            retrigger_characters: None,
            work_done_progress_options: Default::default(),
        }),

        // ── Completions (Tier 5) ─────────────────────────────────────
        completion_provider: Some(CompletionOptions {
            trigger_characters: Some(vec![".".into()]),
            resolve_provider: None,
            work_done_progress_options: Default::default(),
            all_commit_characters: None,
            completion_item: None,
        }),

        // ── Code actions (Tier 6) ────────────────────────────────────
        code_action_provider: Some(CodeActionProviderCapability::Options(
            CodeActionOptions {
                code_action_kinds: Some(vec![
                    CodeActionKind::QUICKFIX,
                    CodeActionKind::new("source.organizeImports"),
                ]),
                work_done_progress_options: Default::default(),
                resolve_provider: None,
            },
        )),

        // ── Features not yet enabled ────────────────────────────────────
        // rename_provider: None,

        ..ServerCapabilities::default()
    }
}

impl Backend {
    /// Build the full `ServerCapabilities` returned to the client.
    pub(crate) async fn handle_initialize(&self, params: InitializeParams) -> ServerCapabilities {
        // Register workspace folders and create projects for each.
        if let Some(workspace_folders) = params.workspace_folders {
            for folder in workspace_folders {
                let Some(project_root) = folder
                    .uri
                    .to_file_path()
                    .ok()
                    .map(|p| p.to_string_lossy().to_string())
                else {
                    debug!("skipping workspace folder with non-file URI: {}", folder.uri);
                    continue;
                };

                let project = Project::new(project_root.clone()).await;
                project.start_queue_worker();
                debug!("project created: {}", project_root);
                self.projects.insert(project_root, project);
            }
        }

        build_server_capabilities()
    }

    /// Called after the client ACKs `initialize`.  Register file watchers and
    /// pull initial configuration.
    pub(crate) async fn handle_initialized(&self) {
        debug!("initialized");

        // Register file watchers for .n files and package.toml.
        let registrations = vec![Registration {
            id: "nature-file-watcher".into(),
            method: "workspace/didChangeWatchedFiles".into(),
            register_options: serde_json::to_value(DidChangeWatchedFilesRegistrationOptions {
                watchers: vec![
                    FileSystemWatcher {
                        glob_pattern: GlobPattern::String("**/*.n".into()),
                        kind: Some(WatchKind::Create | WatchKind::Delete),
                    },
                    FileSystemWatcher {
                        glob_pattern: GlobPattern::String("**/package.toml".into()),
                        kind: Some(WatchKind::all()),
                    },
                ],
            })
            .ok(),
        }];

        if let Err(e) = self.client.register_capability(registrations).await {
            debug!("failed to register file watchers: {:?}", e);
        }

        // Pull initial configuration.
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
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    /// If someone accidentally removes `text_document_sync`, this fails.
    #[test]
    fn caps_has_text_document_sync() {
        let caps = build_server_capabilities();
        let sync = caps
            .text_document_sync
            .expect("text_document_sync must be set");
        match sync {
            TextDocumentSyncCapability::Options(opts) => {
                assert_eq!(opts.open_close, Some(true));
                assert_eq!(opts.change, Some(TextDocumentSyncKind::INCREMENTAL));
                // save must include text
                match opts.save {
                    Some(TextDocumentSyncSaveOptions::SaveOptions(save)) => {
                        assert_eq!(save.include_text, Some(true));
                    }
                    other => panic!("expected SaveOptions, got {:?}", other),
                }
            }
            other => panic!("expected Options variant, got {:?}", other),
        }
    }

    /// Semantic tokens must be enabled with full + range support.
    #[test]
    fn caps_has_semantic_tokens() {
        let caps = build_server_capabilities();
        let provider = caps
            .semantic_tokens_provider
            .expect("semantic_tokens_provider must be set");
        match provider {
            SemanticTokensServerCapabilities::SemanticTokensRegistrationOptions(reg) => {
                let opts = &reg.semantic_tokens_options;
                assert_eq!(opts.full, Some(SemanticTokensFullOptions::Bool(true)));
                assert_eq!(opts.range, Some(true));
                assert!(
                    !opts.legend.token_types.is_empty(),
                    "legend must have token types"
                );
            }
            other => panic!("expected registration options, got {:?}", other),
        }
    }

    /// Semantic tokens document filter must target "nature" language.
    #[test]
    fn caps_semantic_tokens_filter_is_nature() {
        let caps = build_server_capabilities();
        if let Some(SemanticTokensServerCapabilities::SemanticTokensRegistrationOptions(reg)) =
            caps.semantic_tokens_provider
        {
            let filters = reg
                .text_document_registration_options
                .document_selector
                .expect("document_selector must be set");
            assert_eq!(filters.len(), 1);
            assert_eq!(filters[0].language.as_deref(), Some("n"));
            assert_eq!(filters[0].scheme.as_deref(), Some("file"));
        } else {
            panic!("semantic_tokens_provider missing or wrong variant");
        }
    }

    /// Workspace folders must be supported.
    #[test]
    fn caps_has_workspace_folders() {
        let caps = build_server_capabilities();
        let workspace = caps.workspace.expect("workspace must be set");
        let folders = workspace
            .workspace_folders
            .expect("workspace_folders must be set");
        assert_eq!(folders.supported, Some(true));
        assert_eq!(
            folders.change_notifications,
            Some(OneOf::Left(true))
        );
    }

    /// Features we haven't enabled yet must still be None.
    #[test]
    fn caps_disabled_features_are_none() {
        let caps = build_server_capabilities();
        assert!(caps.definition_provider.is_some(), "definition must be enabled");
        assert!(caps.references_provider.is_some(), "references must be enabled");
        assert!(caps.document_symbol_provider.is_some(), "document symbols must be enabled");
        assert!(caps.workspace_symbol_provider.is_some(), "workspace symbols must be enabled");
        assert!(caps.hover_provider.is_some(), "hover must be enabled");
        assert!(caps.inlay_hint_provider.is_some(), "inlay hints must be enabled");
        assert!(caps.signature_help_provider.is_some(), "signature help must be enabled");
        assert!(caps.completion_provider.is_some(), "completion must be enabled");
        assert!(caps.rename_provider.is_none(), "rename not yet enabled");
        assert!(caps.code_action_provider.is_some(), "code actions must be enabled");
    }
}
