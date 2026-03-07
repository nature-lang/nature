# NLS — Nature Language Server

LSP server and VSCode client for the [Nature](https://github.com/nature-lang/nature) programming language.

## Features

- Code completion (type members, imports, selective imports, auto-import, keywords)
- Hover (symbol info, doc comments, type display for ref/ptr wrappers)
- Go-to definition & find references
- Semantic highlighting
- Signature help
- Inlay hints (parameter names, inferred types)
- Code actions (import sorting)
- Document & workspace symbols
- Syntax and type checking with diagnostics
- Multi-workspace and package dependency support
- Platform-specific module resolution

## Project Structure

```
nls/
├── src/
│   ├── main.rs              # Entry point — starts the LSP server via tower-lsp
│   ├── lib.rs               # Library root, declares top-level modules
│   │
│   ├── analyzer.rs          # Analyzer orchestrator (build pipeline, module loading)
│   ├── analyzer/
│   │   ├── common.rs        # Shared AST types (Expr, Stmt, Type, AstNode, …)
│   │   ├── lexer.rs         # Lexer / tokeniser, semantic token definitions
│   │   ├── syntax.rs        # Parser (tokens → AST)
│   │   ├── symbol.rs        # Symbol table, scopes, symbol kinds
│   │   ├── typesys.rs       # Type-system helpers
│   │   ├── flow.rs          # Control-flow analysis
│   │   ├── generics.rs      # Generics resolution
│   │   ├── global_eval.rs   # Global-level constant evaluation
│   │   ├── workspace_index.rs # Cross-workspace symbol indexing
│   │   │
│   │   ├── completion/      # Code-completion provider
│   │   │   ├── mod.rs       #   Types, dispatcher, shared helpers
│   │   │   ├── members.rs   #   Type/struct member & variable completions
│   │   │   ├── imports.rs   #   Import, module, workspace & auto-import completions
│   │   │   └── context.rs   #   Cursor-context extraction + unit tests
│   │   │
│   │   └── semantic/        # Semantic analysis
│   │       ├── mod.rs       #   Semantic struct, type analysis, analyze() entry
│   │       ├── expressions.rs # Expression/statement analysis, constant folding
│   │       └── declarations.rs # Function declarations, type inference, var decls
│   │
│   ├── server/
│   │   ├── mod.rs           # Backend struct, LSP trait impl, request routing
│   │   ├── dispatch.rs      # didOpen / didChange / didSave handlers, debounce
│   │   ├── capabilities.rs  # ServerCapabilities construction
│   │   ├── config.rs        # Client configuration helpers
│   │   ├── completion.rs    # LSP completion request handler
│   │   ├── hover.rs         # Hover handler, type display
│   │   ├── navigation.rs    # Go-to definition, references, cursor context
│   │   ├── semantic_tokens.rs # Semantic tokens encoding
│   │   ├── signature_help.rs  # Signature help handler
│   │   ├── inlay_hints.rs   # Inlay-hint handler
│   │   ├── code_actions.rs  # Code actions (import sorting)
│   │   └── symbols.rs       # Document / workspace symbols
│   │
│   ├── document.rs          # Thread-safe document store (Rope-backed)
│   ├── project.rs           # Workspace / project state, Module struct
│   ├── package.rs           # package.toml parsing
│   └── utils.rs             # Position/offset conversion, identifier helpers
│
├── tests/                   # Integration tests
│   ├── analyzer_test.rs     # Analyzer pipeline tests
│   ├── build_pipeline_test.rs
│   ├── global_eval_test.rs
│   └── server_test.rs       # LSP server integration tests
│
├── client/                  # VSCode extension client (TypeScript)
├── syntaxes/                # TextMate grammar for syntax highlighting
└── assets/                  # Extension icons / assets
```

### Key architectural concepts

- **tower-lsp** — The server is built on `tower-lsp`. `Backend` (in `server/mod.rs`) implements the `LanguageServer` trait.
- **DocumentStore** — In-memory Rope store keyed by URI. Updated incrementally on `didChange`.
- **Project / Module** — Each workspace folder becomes a `Project` (stored in `DashMap`). A project contains multiple `Module`s, each with its own AST, scope, and semantic tokens.
- **Analysis pipeline** — `analyzer.rs` drives: lex → parse → global symbol registration → semantic analysis (per-module). Shared state lives in `SymbolTable` (protected by `Mutex`).
- **Lock ordering** — When both are needed, always lock `module_db` before `symbol_table` to avoid deadlocks.

## Building

```bash
# Debug build
cd nls
cargo build

# Release build
cargo build --release
```

The binary is output to `target/{debug,release}/nls`.

## Testing

```bash
# Unit tests (105 tests across all modules)
cargo test --lib

# Integration tests
cargo test --test analyzer_test
cargo test --test server_test
cargo test --test build_pipeline_test
cargo test --test global_eval_test

# All tests
cargo test

# Run a specific test by name
cargo test --lib completion::context::tests::detects_basic_struct_init

# With output (for debugging)
cargo test --lib -- --nocapture
```

## Debugging

### VSCode extension host

A launch configuration is provided in `.vscode/launch.json`. It:

1. Builds the NLS debug binary (`preLaunchTask: build-nls`)
2. Launches a new VSCode Extension Host window
3. Points `SERVER_PATH` to `target/debug/nls`
4. Enables `RUST_LOG=debug` and `RUST_BACKTRACE=1`

To use it: open the `nls/` folder in VSCode, then **Run → Start Debugging** (or <kbd>F5</kbd>).

### Logging

NLS uses `env_logger`. Set the `RUST_LOG` environment variable:

```bash
# See all NLS debug logs
RUST_LOG=debug ./target/debug/nls

# Filter to specific modules
RUST_LOG=nls::server=debug,nls::analyzer=info ./target/debug/nls
```

When running via the VSCode extension host, logs appear in the **Output** panel under the "Nature Language Server" channel.

### Manual testing against a Nature project

```bash
# 1. Build NLS
cargo build

# 2. Point VSCode to your local build
#    Set SERVER_PATH in your shell before launching VSCode,
#    or use the launch.json configuration above.
export SERVER_PATH="$(pwd)/target/debug/nls"
```

## Installation

### VSCode Extension

Search for **"nature language"** in the VSCode extension marketplace.

### Server

The `nls` binary should be placed at `/usr/local/nature/bin/nls`. The VSCode client automatically detects and launches it from that path.

> The nls binary is also bundled in the nature-lang release package. If you already have nature-lang installed at `/usr/local/nature`, no extra installation step is needed.

## License

MIT / Apache-2.0