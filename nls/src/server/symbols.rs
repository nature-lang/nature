//! Document symbols and workspace symbols.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::utils::offset_to_position;

use super::hover::format_type_display;
use super::Backend;

impl Backend {
    pub(crate) async fn handle_document_symbol(&self, params: DocumentSymbolParams) -> Option<DocumentSymbolResponse> {
        let uri = params.text_document.uri;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        Some(DocumentSymbolResponse::Nested(build_document_symbols(module)))
    }

    pub(crate) async fn handle_workspace_symbol(&self, params: WorkspaceSymbolParams) -> Option<Vec<SymbolInformation>> {
        let query = params.query;
        debug!("workspace_symbol query: '{}'", query);

        let mut results: Vec<SymbolInformation> = Vec::new();

        for entry in self.projects.iter() {
            let project = entry.value();

            // 1. Search the workspace index for lightweight matches
            let workspace_index = match project.workspace_index.lock() {
                Ok(idx) => idx,
                Err(_) => continue,
            };

            let matches = if query.is_empty() {
                workspace_index.symbols.values()
                    .flat_map(|v| v.iter())
                    .collect::<Vec<_>>()
            } else {
                workspace_index.find_symbols_by_prefix_case_insensitive(&query)
            };

            for indexed in matches {
                let kind = match indexed.kind {
                    crate::analyzer::workspace_index::IndexedSymbolKind::Type => SymbolKind::STRUCT,
                    crate::analyzer::workspace_index::IndexedSymbolKind::Function => SymbolKind::FUNCTION,
                    crate::analyzer::workspace_index::IndexedSymbolKind::Variable => SymbolKind::VARIABLE,
                    crate::analyzer::workspace_index::IndexedSymbolKind::Constant => SymbolKind::CONSTANT,
                };

                let uri = match Url::from_file_path(&indexed.file_path) {
                    Ok(u) => u,
                    Err(_) => continue,
                };

                #[allow(deprecated)]
                results.push(SymbolInformation {
                    name: indexed.name.clone(),
                    kind,
                    tags: None,
                    deprecated: None,
                    location: Location::new(uri, Range::new(Position::new(0, 0), Position::new(0, 0))),
                    container_name: Some(indexed.file_path.clone()),
                });
            }

            // 2. Also search fully-analyzed symbols for better position info
            if let Ok(_symbol_table) = project.symbol_table.lock() {
                if let Ok(module_db) = project.module_db.lock() {
                    for m in module_db.iter() {
                        for stmt in &m.stmts {
                            let (name, sym_kind, sym_start, sym_end) = match &stmt.node {
                                crate::analyzer::common::AstNode::FnDef(f) => {
                                    if let Ok(fndef) = f.lock() {
                                        if fndef.fn_name.is_empty() || fndef.is_test {
                                            continue;
                                        }
                                        (fndef.fn_name.clone(), SymbolKind::FUNCTION, fndef.symbol_start, fndef.symbol_end)
                                    } else { continue; }
                                }
                                crate::analyzer::common::AstNode::VarDef(v, _) => {
                                    if let Ok(var) = v.lock() {
                                        (var.ident.clone(), SymbolKind::VARIABLE, var.symbol_start, var.symbol_end)
                                    } else { continue; }
                                }
                                crate::analyzer::common::AstNode::ConstDef(c) => {
                                    if let Ok(cd) = c.lock() {
                                        (cd.ident.clone(), SymbolKind::CONSTANT, cd.symbol_start, cd.symbol_end)
                                    } else { continue; }
                                }
                                crate::analyzer::common::AstNode::Typedef(t) => {
                                    if let Ok(td) = t.lock() {
                                        (td.ident.clone(), SymbolKind::STRUCT, td.symbol_start, td.symbol_end)
                                    } else { continue; }
                                }
                                _ => continue,
                            };

                            if !query.is_empty() && !name.to_lowercase().contains(&query.to_lowercase()) {
                                continue;
                            }

                            let uri = match Url::from_file_path(&m.path) {
                                Ok(u) => u,
                                Err(_) => continue,
                            };

                            if results.iter().any(|r| r.name == name && r.location.uri == uri) {
                                if sym_start > 0 {
                                    if let Some(existing) = results.iter_mut().find(|r| r.name == name && r.location.uri == uri) {
                                        if let (Some(start), Some(end)) = (
                                            offset_to_position(sym_start, &m.rope),
                                            offset_to_position(sym_end, &m.rope),
                                        ) {
                                            existing.location.range = Range::new(start, end);
                                            existing.kind = sym_kind;
                                        }
                                    }
                                }
                                continue;
                            }

                            let range = if sym_start > 0 {
                                if let (Some(start), Some(end)) = (
                                    offset_to_position(sym_start, &m.rope),
                                    offset_to_position(sym_end, &m.rope),
                                ) {
                                    Range::new(start, end)
                                } else {
                                    Range::new(Position::new(0, 0), Position::new(0, 0))
                                }
                            } else {
                                Range::new(Position::new(0, 0), Position::new(0, 0))
                            };

                            #[allow(deprecated)]
                            results.push(SymbolInformation {
                                name,
                                kind: sym_kind,
                                tags: None,
                                deprecated: None,
                                location: Location::new(uri, range),
                                container_name: Some(m.path.clone()),
                            });
                        }
                    }
                }
            }
        }

        if results.is_empty() { None } else { Some(results) }
    }
}

// ─── Document Symbol helpers ────────────────────────────────────────────────────

/// Build a flat list of `DocumentSymbol` entries from a module's top-level statements.
#[allow(deprecated)]
fn build_document_symbols(module: &crate::project::Module) -> Vec<DocumentSymbol> {
    let rope = &module.rope;
    let mut symbols: Vec<DocumentSymbol> = Vec::new();

    for stmt in &module.stmts {
        match &stmt.node {
            crate::analyzer::common::AstNode::FnDef(fndef_mutex) => {
                let fndef = match fndef_mutex.lock() {
                    Ok(f) => f,
                    Err(_) => continue,
                };
                if fndef.is_test || fndef.fn_name.is_empty() {
                    continue;
                }

                let name = fndef.fn_name.clone();
                let detail = Some(format_fn_signature_short(&fndef));

                let range = stmt_range(stmt.start, stmt.end, rope);
                let selection = symbol_selection_range(fndef.symbol_start, fndef.symbol_end, rope);

                let mut children = Vec::new();
                for param in &fndef.params {
                    if let Ok(p) = param.lock() {
                        if p.ident == "self" {
                            continue;
                        }
                        let p_range = symbol_selection_range(p.symbol_start, p.symbol_end, rope);
                        children.push(DocumentSymbol {
                            name: p.ident.clone(),
                            detail: Some(format_type_display(&p.type_)),
                            kind: SymbolKind::VARIABLE,
                            tags: None,
                            deprecated: None,
                            range: p_range,
                            selection_range: p_range,
                            children: None,
                        });
                    }
                }

                symbols.push(DocumentSymbol {
                    name,
                    detail,
                    kind: SymbolKind::FUNCTION,
                    tags: None,
                    deprecated: None,
                    range,
                    selection_range: selection,
                    children: if children.is_empty() { None } else { Some(children) },
                });
            }
            crate::analyzer::common::AstNode::VarDef(var_mutex, _) => {
                let var = match var_mutex.lock() {
                    Ok(v) => v,
                    Err(_) => continue,
                };
                let range = stmt_range(stmt.start, stmt.end, rope);
                let selection = symbol_selection_range(var.symbol_start, var.symbol_end, rope);
                symbols.push(DocumentSymbol {
                    name: var.ident.clone(),
                    detail: Some(format_type_display(&var.type_)),
                    kind: SymbolKind::VARIABLE,
                    tags: None,
                    deprecated: None,
                    range,
                    selection_range: selection,
                    children: None,
                });
            }
            crate::analyzer::common::AstNode::ConstDef(const_mutex) => {
                let c = match const_mutex.lock() {
                    Ok(c) => c,
                    Err(_) => continue,
                };
                let range = stmt_range(stmt.start, stmt.end, rope);
                let selection = symbol_selection_range(c.symbol_start, c.symbol_end, rope);
                symbols.push(DocumentSymbol {
                    name: c.ident.clone(),
                    detail: Some(format_type_display(&c.type_)),
                    kind: SymbolKind::CONSTANT,
                    tags: None,
                    deprecated: None,
                    range,
                    selection_range: selection,
                    children: None,
                });
            }
            crate::analyzer::common::AstNode::Typedef(typedef_mutex) => {
                let td = match typedef_mutex.lock() {
                    Ok(t) => t,
                    Err(_) => continue,
                };

                let kind = if td.is_enum {
                    SymbolKind::ENUM
                } else if td.is_interface {
                    SymbolKind::INTERFACE
                } else {
                    SymbolKind::STRUCT
                };

                let range = stmt_range(stmt.start, stmt.end, rope);
                let selection = symbol_selection_range(td.symbol_start, td.symbol_end, rope);

                let mut children: Vec<DocumentSymbol> = Vec::new();
                let mut methods: Vec<_> = td.method_table.iter().collect();
                methods.sort_by_key(|(name, _)| (*name).clone());
                for (method_name, method_mutex) in methods {
                    if let Ok(m) = method_mutex.lock() {
                        let m_detail = Some(format_fn_signature_short(&m));
                        let m_range = symbol_selection_range(m.symbol_start, m.symbol_end, rope);
                        children.push(DocumentSymbol {
                            name: method_name.clone(),
                            detail: m_detail,
                            kind: SymbolKind::METHOD,
                            tags: None,
                            deprecated: None,
                            range: m_range,
                            selection_range: m_range,
                            children: None,
                        });
                    }
                }

                symbols.push(DocumentSymbol {
                    name: td.ident.clone(),
                    detail: None,
                    kind,
                    tags: None,
                    deprecated: None,
                    range,
                    selection_range: selection,
                    children: if children.is_empty() { None } else { Some(children) },
                });
            }
            crate::analyzer::common::AstNode::Import(import_stmt) => {
                let name = import_stmt.as_name.clone();
                if name.is_empty() {
                    continue;
                }
                let range = stmt_range(stmt.start, stmt.end, rope);
                symbols.push(DocumentSymbol {
                    name,
                    detail: import_stmt.file.clone().or_else(|| {
                        import_stmt.ast_package.as_ref().map(|p| p.join("."))
                    }),
                    kind: SymbolKind::MODULE,
                    tags: None,
                    deprecated: None,
                    range,
                    selection_range: range,
                    children: None,
                });
            }
            _ => {}
        }
    }

    symbols
}

/// Build an LSP Range from start/end char offsets.
fn stmt_range(start: usize, end: usize, rope: &ropey::Rope) -> Range {
    let s = offset_to_position(start, rope).unwrap_or(Position::new(0, 0));
    let e = offset_to_position(end, rope).unwrap_or(s);
    Range::new(s, e)
}

/// Build a selection Range from symbol_start/symbol_end char offsets.
fn symbol_selection_range(start: usize, end: usize, rope: &ropey::Rope) -> Range {
    if start == 0 && end == 0 {
        return Range::new(Position::new(0, 0), Position::new(0, 0));
    }
    stmt_range(start, end, rope)
}

/// Format a short function signature like `(a: int, b: string): bool`.
fn format_fn_signature_short(fndef: &crate::analyzer::common::AstFnDef) -> String {
    let mut sig = String::from("(");
    let mut first = true;
    for param in &fndef.params {
        if let Ok(p) = param.lock() {
            if !first {
                sig.push_str(", ");
            }
            first = false;
            if p.ident == "self" {
                match fndef.self_kind {
                    crate::analyzer::common::SelfKind::SelfRefT => sig.push_str("&self"),
                    crate::analyzer::common::SelfKind::SelfPtrT => sig.push_str("*self"),
                    _ => sig.push_str("self"),
                }
            } else {
                sig.push_str(&format!("{}: {}", p.ident, format_type_display(&p.type_)));
            }
        }
    }
    sig.push(')');
    let ret = format_type_display(&fndef.return_type);
    if ret != "void" {
        sig.push_str(&format!(": {}", ret));
    }
    if fndef.is_errable {
        sig.push('!');
    }
    sig
}
