use dashmap::DashMap;
use nls::server::Backend;
use tower_lsp::{LspService, Server};

#[tokio::main]
async fn main() {
    env_logger::init();

    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();

    let (service, socket) = LspService::build(|client| Backend {
        client,
        projects: DashMap::new(),
        debounce_versions: DashMap::new(),
        documents: DashMap::new(),
        config: DashMap::new(),
        semantic_token_cache: DashMap::new(),
    })
    .finish();

    Server::new(stdin, stdout, socket).serve(service).await;
}
