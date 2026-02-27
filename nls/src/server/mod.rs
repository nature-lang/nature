//! LSP server implementation split into focused modules.

pub mod capabilities;
pub mod code_actions;
pub mod completion;
pub mod config;
pub mod dispatch;
pub mod hover;
pub mod inlay_hints;
pub mod navigation;
pub mod semantic_tokens;
pub mod signature;
pub mod symbols;

use std::sync::atomic::AtomicU64;
use std::sync::Arc;

use dashmap::DashMap;
use ropey::Rope;
use serde_json::Value;
use tower_lsp::jsonrpc::Result;
use tower_lsp::lsp_types::*;
use tower_lsp::{Client, LanguageServer};

use crate::project::Project;

/// The central LSP backend state shared across all handlers.
#[derive(Debug)]
pub struct Backend {
    pub client: Client,
    pub projects: DashMap<String, Project>,
    /// Per-file monotonic version counter used for debouncing `did_change`.
    pub debounce_versions: DashMap<String, Arc<AtomicU64>>,
    /// Live document content keyed by file path (incremental sync).
    pub documents: DashMap<String, Rope>,
    /// User / workspace configuration settings.
    pub config: DashMap<String, serde_json::Value>,
}

/// Internal helper carrying document info between `did_*` handlers and `on_change`.
pub(crate) struct TextDocumentItem<'a> {
    pub uri: Url,
    pub text: &'a str,
    pub version: Option<i32>,
}

// ─── LanguageServer trait ───────────────────────────────────────────────────────

#[tower_lsp::async_trait]
impl LanguageServer for Backend {
    async fn initialize(&self, params: InitializeParams) -> Result<InitializeResult> {
        let capabilities = self.handle_initialize(params).await;
        Ok(InitializeResult {
            server_info: None,
            capabilities,
            ..Default::default()
        })
    }

    async fn initialized(&self, _: InitializedParams) {
        self.handle_initialized().await;
    }

    async fn shutdown(&self) -> Result<()> {
        Ok(())
    }

    async fn did_open(&self, params: DidOpenTextDocumentParams) {
        self.handle_did_open(params).await;
    }

    async fn did_change(&self, params: DidChangeTextDocumentParams) {
        self.handle_did_change(params).await;
    }

    async fn did_save(&self, params: DidSaveTextDocumentParams) {
        self.handle_did_save(params).await;
    }

    async fn did_close(&self, params: DidCloseTextDocumentParams) {
        self.handle_did_close(params).await;
    }

    async fn goto_definition(&self, params: GotoDefinitionParams) -> Result<Option<GotoDefinitionResponse>> {
        Ok(self.handle_goto_definition(params).await)
    }

    async fn references(&self, params: ReferenceParams) -> Result<Option<Vec<Location>>> {
        Ok(self.handle_references(params).await)
    }

    async fn document_symbol(&self, params: DocumentSymbolParams) -> Result<Option<DocumentSymbolResponse>> {
        Ok(self.handle_document_symbol(params).await)
    }

    async fn signature_help(&self, params: SignatureHelpParams) -> Result<Option<SignatureHelp>> {
        Ok(self.handle_signature_help(params).await)
    }

    async fn semantic_tokens_full(&self, params: SemanticTokensParams) -> Result<Option<SemanticTokensResult>> {
        Ok(self.handle_semantic_tokens_full(params).await)
    }

    async fn semantic_tokens_range(&self, params: SemanticTokensRangeParams) -> Result<Option<SemanticTokensRangeResult>> {
        Ok(self.handle_semantic_tokens_range(params).await)
    }

    async fn hover(&self, params: HoverParams) -> Result<Option<Hover>> {
        Ok(self.handle_hover(params).await)
    }

    async fn completion(&self, params: CompletionParams) -> Result<Option<CompletionResponse>> {
        Ok(self.handle_completion(params).await)
    }

    async fn prepare_rename(&self, params: TextDocumentPositionParams) -> Result<Option<PrepareRenameResponse>> {
        Ok(self.handle_prepare_rename(params).await)
    }

    async fn rename(&self, params: RenameParams) -> Result<Option<WorkspaceEdit>> {
        Ok(self.handle_rename(params).await)
    }

    async fn symbol(&self, params: WorkspaceSymbolParams) -> Result<Option<Vec<SymbolInformation>>> {
        Ok(self.handle_workspace_symbol(params).await)
    }

    async fn inlay_hint(&self, params: InlayHintParams) -> Result<Option<Vec<InlayHint>>> {
        Ok(self.handle_inlay_hint(params).await)
    }

    async fn code_action(&self, params: CodeActionParams) -> Result<Option<CodeActionResponse>> {
        Ok(self.handle_code_action(params).await)
    }

    async fn did_change_configuration(&self, params: DidChangeConfigurationParams) {
        self.handle_did_change_configuration(params).await;
    }

    async fn did_change_workspace_folders(&self, _: DidChangeWorkspaceFoldersParams) {
        log::debug!("workspace folders changed!");
    }

    async fn did_change_watched_files(&self, params: DidChangeWatchedFilesParams) {
        self.handle_did_change_watched_files(params).await;
    }

    async fn execute_command(&self, params: ExecuteCommandParams) -> Result<Option<Value>> {
        Ok(self.handle_execute_command(params).await)
    }
}

// ─── Shared helpers ─────────────────────────────────────────────────────────────

impl Backend {
    /// Find the project that best matches a file path (longest prefix match).
    pub(crate) fn get_file_project(&self, file_path: &str) -> Option<Project> {
        let mut best_match: Option<(usize, Project)> = None;

        for entry in self.projects.iter() {
            let workspace_uri = entry.key();
            if file_path.starts_with(workspace_uri.as_str()) {
                let uri_len = workspace_uri.len();
                if best_match.as_ref().map_or(true, |(best_len, _)| uri_len > *best_len) {
                    best_match = Some((uri_len, entry.value().clone()));
                }
            }
        }

        best_match.map(|(_, project)| project)
    }

    /// Walk up from `start_dir` looking for a `package.toml` file.
    pub(crate) fn find_package_dir(&self, start_dir: &std::path::Path) -> Option<std::path::PathBuf> {
        let mut current_dir = Some(start_dir.to_path_buf());
        while let Some(dir) = current_dir {
            if dir.join("package.toml").exists() {
                return Some(dir);
            }
            current_dir = dir.parent().map(|p| p.to_path_buf());
        }
        None
    }
}
