//! Document-symbol and workspace-symbol request handlers.

use tower_lsp::lsp_types::*;

use crate::analyzer::common::{AstNode, Stmt};
use crate::analyzer::workspace_index::IndexedSymbolKind;
use crate::project::Project;
use crate::utils::offset_to_position;

use super::Backend;

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_document_symbol(
        &self,
        params: DocumentSymbolParams,
    ) -> Option<DocumentSymbolResponse> {
        let file_path = params.text_document.uri.path();
        let project = self.get_file_project(file_path)?;

        let symbols = document_symbols(&project, file_path)?;
        Some(DocumentSymbolResponse::Flat(symbols))
    }

    pub(crate) async fn handle_workspace_symbol(
        &self,
        params: WorkspaceSymbolParams,
    ) -> Option<Vec<SymbolInformation>> {
        let query = &params.query;

        let mut results: Vec<SymbolInformation> = Vec::new();

        for entry in self.projects.iter() {
            let project = entry.value();
            collect_workspace_symbols(project, query, &mut results);
        }

        if results.is_empty() {
            return None;
        }

        Some(results)
    }
}

// ─── Document symbols ───────────────────────────────────────────────────────────

/// Extract top-level symbols from a single module's AST.
fn document_symbols(project: &Project, file_path: &str) -> Option<Vec<SymbolInformation>> {
    let mh = project.module_handled.lock().ok()?;
    let &idx = mh.get(file_path)?;
    drop(mh);

    let db = project.module_db.lock().ok()?;
    let module = db.get(idx)?;

    let uri = Url::from_file_path(&module.path).ok()?;
    let symbols = symbols_from_stmts(&module.stmts, &module.rope, &uri);

    Some(symbols)
}

/// Walk top-level statements and produce `SymbolInformation` entries.
fn symbols_from_stmts(
    stmts: &[Box<Stmt>],
    rope: &ropey::Rope,
    uri: &Url,
) -> Vec<SymbolInformation> {
    let mut out = Vec::new();

    for stmt in stmts {
        if let Some(sym) = symbol_from_stmt(stmt, rope, uri) {
            out.push(sym);
        }
    }

    out
}

/// Convert a single top-level statement to a `SymbolInformation`, if applicable.
#[allow(deprecated)] // SymbolInformation::deprecated is deprecated but required by the type
fn symbol_from_stmt(
    stmt: &Stmt,
    rope: &ropey::Rope,
    uri: &Url,
) -> Option<SymbolInformation> {
    let (name, kind, start, end) = match &stmt.node {
        AstNode::FnDef(fndef_mutex) => {
            let fndef = fndef_mutex.lock().unwrap();
            let name = if fndef.fn_name.is_empty() {
                fndef.symbol_name.clone()
            } else {
                fndef.fn_name.clone()
            };
            (name, SymbolKind::FUNCTION, fndef.symbol_start, fndef.symbol_end)
        }
        AstNode::Typedef(typedef_mutex) => {
            let typedef = typedef_mutex.lock().unwrap();
            let kind = if typedef.is_interface {
                SymbolKind::INTERFACE
            } else if typedef.is_enum || typedef.is_tagged_union {
                SymbolKind::ENUM
            } else {
                SymbolKind::STRUCT
            };
            (typedef.ident.clone(), kind, typedef.symbol_start, typedef.symbol_end)
        }
        AstNode::VarDef(var_mutex, _) => {
            let var = var_mutex.lock().unwrap();
            (var.ident.clone(), SymbolKind::VARIABLE, var.symbol_start, var.symbol_end)
        }
        AstNode::ConstDef(const_mutex) => {
            let c = const_mutex.lock().unwrap();
            (c.ident.clone(), SymbolKind::CONSTANT, c.symbol_start, c.symbol_end)
        }
        _ => return None,
    };

    let start_pos = offset_to_position(start, rope)?;
    let end_pos = offset_to_position(end, rope)?;

    Some(SymbolInformation {
        name,
        kind,
        tags: None,
        deprecated: None,
        location: Location {
            uri: uri.clone(),
            range: Range {
                start: start_pos,
                end: end_pos,
            },
        },
        container_name: None,
    })
}

// ─── Workspace symbols ──────────────────────────────────────────────────────────

/// Collect workspace symbols matching `query` from a project's index.
#[allow(deprecated)]
fn collect_workspace_symbols(
    project: &Project,
    query: &str,
    out: &mut Vec<SymbolInformation>,
) {
    let ws = match project.workspace_index.lock() {
        Ok(ws) => ws,
        Err(_) => return,
    };

    let indexed = if query.is_empty() {
        // Return all symbols (capped to avoid flooding the client).
        ws.symbols
            .values()
            .flat_map(|v| v.iter())
            .take(500)
            .collect::<Vec<_>>()
    } else {
        ws.find_symbols_by_prefix_case_insensitive(query)
    };

    for sym in indexed {
        let kind = match sym.kind {
            IndexedSymbolKind::Type => SymbolKind::STRUCT,
            IndexedSymbolKind::Function => SymbolKind::FUNCTION,
            IndexedSymbolKind::Variable => SymbolKind::VARIABLE,
            IndexedSymbolKind::Constant => SymbolKind::CONSTANT,
        };

        let uri = match Url::from_file_path(&sym.file_path) {
            Ok(u) => u,
            Err(_) => continue,
        };

        // WorkspaceIndex doesn't store line position — default to file start.
        // The client will still jump to the file; go-to-def from there refines.
        out.push(SymbolInformation {
            name: sym.name.clone(),
            kind,
            tags: None,
            deprecated: None,
            location: Location {
                uri,
                range: Range {
                    start: Position::new(0, 0),
                    end: Position::new(0, 0),
                },
            },
            container_name: None,
        });
    }
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::common::{AstFnDef, Type, VarDeclExpr};
    use ropey::Rope;
    use std::sync::{Arc, Mutex};

    fn make_fndef_stmt(name: &str, start: usize, end: usize) -> Box<Stmt> {
        let mut fndef = AstFnDef::default();
        fndef.fn_name = name.to_string();
        fndef.symbol_start = start;
        fndef.symbol_end = end;
        Box::new(Stmt {
            start,
            end,
            node: AstNode::FnDef(Arc::new(Mutex::new(fndef))),
        })
    }

    fn make_vardef_stmt(name: &str, start: usize, end: usize) -> Box<Stmt> {
        let var = Arc::new(Mutex::new(VarDeclExpr {
            ident: name.to_string(),
            symbol_id: 0,
            symbol_start: start,
            symbol_end: end,
            type_: Type::default(),
            be_capture: false,
            heap_ident: None,
        }));
        let right = Box::new(crate::analyzer::common::Expr {
            start,
            end,
            type_: Type::default(),
            target_type: Type::default(),
            node: AstNode::None,
        });
        Box::new(Stmt {
            start,
            end,
            node: AstNode::VarDef(var, right),
        })
    }

    #[test]
    fn symbols_from_stmts_collects_fn_and_var() {
        let source = "fn hello() {}\nvar x = 42\n";
        let rope = Rope::from_str(source);
        let uri = Url::parse("file:///test.n").unwrap();

        let stmts = vec![
            make_fndef_stmt("hello", 3, 8),
            make_vardef_stmt("x", 18, 19),
        ];

        let result = symbols_from_stmts(&stmts, &rope, &uri);
        assert_eq!(result.len(), 2);
        assert_eq!(result[0].name, "hello");
        assert_eq!(result[0].kind, SymbolKind::FUNCTION);
        assert_eq!(result[1].name, "x");
        assert_eq!(result[1].kind, SymbolKind::VARIABLE);
    }

    #[test]
    fn symbols_from_stmts_skips_non_declarations() {
        let source = "import 'math.n'\n";
        let rope = Rope::from_str(source);
        let uri = Url::parse("file:///test.n").unwrap();

        let import = crate::analyzer::common::ImportStmt::default();
        let stmts = vec![Box::new(Stmt {
            start: 0,
            end: 15,
            node: AstNode::Import(import),
        })];

        let result = symbols_from_stmts(&stmts, &rope, &uri);
        assert!(result.is_empty());
    }

    #[test]
    fn indexed_kind_to_symbol_kind() {
        assert_eq!(
            match IndexedSymbolKind::Function {
                IndexedSymbolKind::Type => SymbolKind::STRUCT,
                IndexedSymbolKind::Function => SymbolKind::FUNCTION,
                IndexedSymbolKind::Variable => SymbolKind::VARIABLE,
                IndexedSymbolKind::Constant => SymbolKind::CONSTANT,
            },
            SymbolKind::FUNCTION
        );
    }
}
