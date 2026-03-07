use dashmap::DashMap;
use tower_lsp::{LspService, Server};

use nls::document::DocumentStore;
use nls::server::Backend;

#[tokio::main]
async fn main() {
    env_logger::init();

    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();

    let (service, socket) = LspService::new(|client| Backend {
        client,
        documents: DocumentStore::new(),
        projects: DashMap::new(),
        debounce_versions: DashMap::new(),
        config: DashMap::new(),
    });

    Server::new(stdin, stdout, socket).serve(service).await;
}
