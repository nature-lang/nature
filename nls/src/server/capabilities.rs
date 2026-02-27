//! `initialize` and `initialized` — capability advertisement and startup.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::analyzer::lexer::LEGEND_TYPE;
use crate::project::Project;

use super::Backend;

impl Backend {
    /// Build the full `InitializeResult` with all server capabilities.
    pub(crate) async fn handle_initialize(&self, params: InitializeParams) -> ServerCapabilities {
        // Register workspace folders
        if let Some(workspace_folders) = params.workspace_folders {
            for folder in workspace_folders {
                let Some(project_root) = folder
                    .uri
                    .to_file_path()
                    .ok()
                    .map(|p| p.to_string_lossy().to_string())
                else {
                    debug!("Skipping workspace folder with non-file URI: {}", folder.uri);
                    continue;
                };
                let project = Project::new(project_root.clone()).await;
                project.backend_handle_queue();
                debug!("project new success root: {}", project_root);
                self.projects.insert(project_root, project);
            }
        }

        ServerCapabilities {
            inlay_hint_provider: Some(OneOf::Left(true)),
            text_document_sync: Some(TextDocumentSyncCapability::Options(TextDocumentSyncOptions {
                open_close: Some(true),
                change: Some(TextDocumentSyncKind::INCREMENTAL),
                save: Some(TextDocumentSyncSaveOptions::SaveOptions(SaveOptions {
                    include_text: Some(true),
                })),
                ..Default::default()
            })),
            completion_provider: Some(CompletionOptions {
                resolve_provider: Some(false),
                trigger_characters: Some(vec![".".to_string()]),
                work_done_progress_options: Default::default(),
                all_commit_characters: None,
                completion_item: None,
            }),
            execute_command_provider: Some(ExecuteCommandOptions {
                commands: vec!["dummy.do_something".to_string()],
                work_done_progress_options: Default::default(),
            }),
            workspace: Some(WorkspaceServerCapabilities {
                workspace_folders: Some(WorkspaceFoldersServerCapabilities {
                    supported: Some(true),
                    change_notifications: Some(OneOf::Left(true)),
                }),
                file_operations: None,
            }),
            semantic_tokens_provider: Some(
                SemanticTokensServerCapabilities::SemanticTokensRegistrationOptions(
                    SemanticTokensRegistrationOptions {
                        text_document_registration_options: TextDocumentRegistrationOptions {
                            document_selector: Some(vec![DocumentFilter {
                                language: Some("n".to_string()),
                                scheme: Some("file".to_string()),
                                pattern: None,
                            }]),
                        },
                        semantic_tokens_options: SemanticTokensOptions {
                            work_done_progress_options: WorkDoneProgressOptions::default(),
                            legend: SemanticTokensLegend {
                                token_types: LEGEND_TYPE.into(),
                                token_modifiers: vec![],
                            },
                            range: Some(true),
                            full: Some(SemanticTokensFullOptions::Bool(true)),
                        },
                        static_registration_options: StaticRegistrationOptions::default(),
                    },
                ),
            ),
            definition_provider: Some(OneOf::Left(true)),
            references_provider: Some(OneOf::Left(true)),
            rename_provider: Some(OneOf::Right(RenameOptions {
                prepare_provider: Some(true),
                work_done_progress_options: Default::default(),
            })),
            hover_provider: Some(HoverProviderCapability::Simple(true)),
            workspace_symbol_provider: Some(OneOf::Left(true)),
            document_symbol_provider: Some(OneOf::Left(true)),
            signature_help_provider: Some(SignatureHelpOptions {
                trigger_characters: Some(vec!["(".to_string(), ",".to_string()]),
                retrigger_characters: None,
                work_done_progress_options: Default::default(),
            }),
            code_action_provider: Some(CodeActionProviderCapability::Options(CodeActionOptions {
                code_action_kinds: Some(vec![CodeActionKind::QUICKFIX]),
                work_done_progress_options: Default::default(),
                resolve_provider: None,
            })),
            ..ServerCapabilities::default()
        }
    }

    /// Register dynamic capabilities and pull initial configuration.
    pub(crate) async fn handle_initialized(&self) {
        debug!("initialized");

        // Register file watchers for .n source files and package.toml
        let registrations = vec![Registration {
            id: "nature-file-watcher".to_string(),
            method: "workspace/didChangeWatchedFiles".to_string(),
            register_options: Some(
                serde_json::to_value(DidChangeWatchedFilesRegistrationOptions {
                    watchers: vec![
                        FileSystemWatcher {
                            glob_pattern: GlobPattern::String("**/*.n".to_string()),
                            kind: Some(WatchKind::Create | WatchKind::Delete),
                        },
                        FileSystemWatcher {
                            glob_pattern: GlobPattern::String("**/package.toml".to_string()),
                            kind: Some(WatchKind::all()),
                        },
                    ],
                })
                .ok(),
            )
            .flatten(),
        }];

        if let Err(e) = self.client.register_capability(registrations).await {
            debug!("Failed to register file watchers: {:?}", e);
        }

        // Pull initial configuration from the client
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
}
