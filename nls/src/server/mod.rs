//! LSP backend: shared state and `LanguageServer` trait implementation.
//!
//! Actual handler logic lives in focused submodules; this file only wires the
//! trait methods to those handlers and holds the shared [`Backend`] struct.

pub mod capabilities;
pub mod code_actions;
pub mod completion;
pub mod config;
pub mod dispatch;
pub mod hover;
pub mod inlay_hints;
pub mod navigation;
pub mod semantic_tokens;
pub mod signature_help;
pub mod symbols;

use std::sync::atomic::AtomicU64;
use std::sync::Arc;

use dashmap::DashMap;
use serde_json::Value;
use tower_lsp::jsonrpc::Result;
use tower_lsp::lsp_types::*;
use tower_lsp::{Client, LanguageServer};

use crate::document::DocumentStore;
use crate::project::Project;

/// The central LSP backend shared across all handlers.
#[derive(Debug)]
pub struct Backend {
    /// LSP client handle for sending notifications (diagnostics, logs, etc.).
    pub client: Client,
    /// Open documents with incremental sync.
    pub documents: DocumentStore,
    /// Active projects keyed by root path.
    pub projects: DashMap<String, Project>,
    /// Per-file monotonic counter for debouncing `did_change`.
    pub debounce_versions: DashMap<String, Arc<AtomicU64>>,
    /// User / workspace configuration settings (flat dot-separated keys).
    pub config: DashMap<String, Value>,
}

impl Backend {
    /// Find the project whose root best matches `file_path` (longest prefix).
    pub(crate) fn get_file_project(&self, file_path: &str) -> Option<Project> {
        let mut best: Option<(usize, Project)> = None;
        for entry in self.projects.iter() {
            let root = entry.key();
            if file_path.starts_with(root.as_str()) {
                let len = root.len();
                if best.as_ref().map_or(true, |(best_len, _)| len > *best_len) {
                    best = Some((len, entry.value().clone()));
                }
            }
        }
        best.map(|(_, p)| p)
    }

    /// Walk up from `start_dir` looking for `package.toml`.
    pub(crate) fn find_package_dir(&self, start_dir: &std::path::Path) -> Option<std::path::PathBuf> {
        let mut current = Some(start_dir.to_path_buf());
        while let Some(dir) = current {
            if dir.join("package.toml").exists() {
                return Some(dir);
            }
            current = dir.parent().map(|p| p.to_path_buf());
        }
        None
    }
}

// ─── LanguageServer trait ───────────────────────────────────────────────────────

#[tower_lsp::async_trait]
impl LanguageServer for Backend {
    async fn initialize(&self, params: InitializeParams) -> Result<InitializeResult> {
        let capabilities = self.handle_initialize(params).await;
        Ok(InitializeResult {
            server_info: Some(ServerInfo {
                name: "nls".into(),
                version: Some("0.1.0".into()),
            }),
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

    // ── Document sync ───────────────────────────────────────────────────

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

    async fn did_change_configuration(&self, params: DidChangeConfigurationParams) {
        self.handle_did_change_configuration(params).await;
    }

    async fn did_change_watched_files(&self, params: DidChangeWatchedFilesParams) {
        self.handle_did_change_watched_files(params).await;
    }

    // ── Navigation ──────────────────────────────────────────────────

    async fn goto_definition(
        &self,
        params: GotoDefinitionParams,
    ) -> Result<Option<GotoDefinitionResponse>> {
        Ok(self.handle_goto_definition(params).await)
    }

    async fn references(
        &self,
        params: ReferenceParams,
    ) -> Result<Option<Vec<Location>>> {
        Ok(self.handle_references(params).await)
    }

    async fn goto_type_definition(
        &self,
        params: GotoDefinitionParams,
    ) -> Result<Option<GotoDefinitionResponse>> {
        Ok(self.handle_goto_type_definition(params).await)
    }

    async fn goto_implementation(
        &self,
        params: GotoDefinitionParams,
    ) -> Result<Option<GotoDefinitionResponse>> {
        Ok(self.handle_goto_implementation(params).await)
    }

    async fn document_symbol(
        &self,
        params: DocumentSymbolParams,
    ) -> Result<Option<DocumentSymbolResponse>> {
        Ok(self.handle_document_symbol(params).await)
    }

    async fn symbol(
        &self,
        params: WorkspaceSymbolParams,
    ) -> Result<Option<Vec<SymbolInformation>>> {
        Ok(self.handle_workspace_symbol(params).await)
    }

    // ── Inline information (Tier 4) ──────────────────────────────────

    async fn hover(
        &self,
        params: HoverParams,
    ) -> Result<Option<Hover>> {
        Ok(self.handle_hover(params).await)
    }

    async fn inlay_hint(
        &self,
        params: InlayHintParams,
    ) -> Result<Option<Vec<InlayHint>>> {
        Ok(self.handle_inlay_hint(params).await)
    }

    async fn signature_help(
        &self,
        params: SignatureHelpParams,
    ) -> Result<Option<SignatureHelp>> {
        Ok(self.handle_signature_help(params).await)
    }

    // ── Completions (Tier 5) ─────────────────────────────────────────

    async fn completion(
        &self,
        params: CompletionParams,
    ) -> Result<Option<CompletionResponse>> {
        Ok(self.handle_completion(params).await)
    }

    // ── Code actions (Tier 6) ────────────────────────────────────────

    async fn code_action(
        &self,
        params: CodeActionParams,
    ) -> Result<Option<CodeActionResponse>> {
        Ok(self.handle_code_action(params).await)
    }

    // ── Semantic tokens ─────────────────────────────────────────────

    async fn semantic_tokens_full(
        &self,
        params: SemanticTokensParams,
    ) -> Result<Option<SemanticTokensResult>> {
        Ok(self.handle_semantic_tokens_full(params).await)
    }

    async fn semantic_tokens_range(
        &self,
        params: SemanticTokensRangeParams,
    ) -> Result<Option<SemanticTokensRangeResult>> {
        Ok(self.handle_semantic_tokens_range(params).await)
    }
}
