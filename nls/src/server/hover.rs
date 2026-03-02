//! Hover request handler: show type signatures and documentation on hover.

use tower_lsp::lsp_types::*;

use crate::analyzer::common::{AstFnDef, SelfKind, TypeKind};
use crate::analyzer::lexer::{Token, TokenType};
use crate::analyzer::symbol::SymbolKind;
use crate::project::Project;

use super::navigation::{resolve_cursor, resolve_symbol};
use super::Backend;

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_hover(
        &self,
        params: HoverParams,
    ) -> Option<Hover> {
        let uri = &params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;
        build_hover(&project, file_path, position)
    }
}

// ─── Hover construction ─────────────────────────────────────────────────────────

/// Build a `Hover` response for the symbol under the cursor.
fn build_hover(project: &Project, file_path: &str, position: Position) -> Option<Hover> {
    let cursor = resolve_cursor(project, file_path, position)?;
    let symbol = resolve_symbol(project, &cursor)?;

    // Extract doc comment only for globally declared symbols.
    // Global symbols have a module prefix ("module.ident") in their ident;
    // local variables/constants inside function bodies do not.
    let is_global = match &symbol.kind {
        SymbolKind::Fn(_) | SymbolKind::Type(_) => true,
        SymbolKind::Var(_) | SymbolKind::Const(_) => symbol.ident.contains('.'),
    };
    let doc_comment = if is_global {
        extract_doc_comment_for_symbol(project, &symbol.module_path, &symbol.kind)
    } else {
        None
    };

    let content = format_hover_content(&symbol.kind, &cursor.word, doc_comment.as_deref());

    Some(Hover {
        contents: HoverContents::Markup(MarkupContent {
            kind: MarkupKind::Markdown,
            value: content,
        }),
        range: None,
    })
}

// ─── Formatting ─────────────────────────────────────────────────────────────────

/// Produce markdown hover text for a resolved symbol.
fn format_hover_content(kind: &SymbolKind, word: &str, doc: Option<&str>) -> String {
    let signature = match kind {
        SymbolKind::Fn(fndef_mutex) => {
            let fndef = fndef_mutex.lock().unwrap();
            format_fn_hover(&fndef)
        }
        SymbolKind::Var(var_mutex) => {
            let var = var_mutex.lock().unwrap();
            format!("```n\nvar {}: {}\n```", display_name(&var.ident, word), type_display_name(&var.type_))
        }
        SymbolKind::Const(const_mutex) => {
            let c = const_mutex.lock().unwrap();
            format!("```n\nconst {}: {}\n```", display_name(&c.ident, word), type_display_name(&c.type_))
        }
        SymbolKind::Type(typedef_mutex) => {
            let typedef = typedef_mutex.lock().unwrap();
            format_type_hover(&typedef, word)
        }
    };

    match doc {
        Some(d) if !d.is_empty() => format!("{}\n\n---\n\n{}", signature, d),
        _ => signature,
    }
}

/// Format a function definition for hover display.
fn format_fn_hover(fndef: &AstFnDef) -> String {
    let name = if fndef.fn_name.is_empty() {
        &fndef.symbol_name
    } else {
        &fndef.fn_name
    };

    let params = format_params(fndef);
    let ret = type_display_name(&fndef.return_type);

    let mut sig = format!("fn {}({}): {}", name, params, ret);

    if fndef.is_errable {
        sig.push('!');
    }

    format!("```n\n{}\n```", sig)
}

/// Format function parameters as a comma-separated string.
fn format_params(fndef: &AstFnDef) -> String {
    let mut parts: Vec<String> = Vec::new();

    for param in &fndef.params {
        let p = param.lock().unwrap();

        if p.ident == "self" {
            let self_str = match fndef.self_kind {
                SelfKind::SelfRefT => "&self",
                SelfKind::SelfPtrT => "*self",
                _ => "self",
            };
            parts.push(self_str.to_string());
            continue;
        }

        let type_str = type_display_name(&p.type_);
        parts.push(format!("{}: {}", p.ident, type_str));
    }

    if fndef.rest_param {
        parts.push("...".to_string());
    }

    parts.join(", ")
}

/// Format a type definition for hover display.
fn format_type_hover(
    typedef: &crate::analyzer::common::TypedefStmt,
    word: &str,
) -> String {
    let name = display_name(&typedef.ident, word);
    let keyword = if typedef.is_interface {
        "interface"
    } else if typedef.is_enum {
        "enum"
    } else if typedef.is_tagged_union {
        "union"
    } else {
        "type"
    };

    let generics = if typedef.params.is_empty() {
        String::new()
    } else {
        let gs: Vec<&str> = typedef.params.iter().map(|p| p.ident.as_str()).collect();
        format!("<{}>", gs.join(", "))
    };

    let body = format_type_body(&typedef.type_expr.kind);

    format!("```n\n{} {}{} = {}\n```", keyword, name, generics, body)
}

/// Produce a concise representation of a type body for hover.
fn format_type_body(kind: &TypeKind) -> String {
    match kind {
        TypeKind::Struct(_, _, props) => {
            if props.is_empty() {
                return "{}".to_string();
            }
            let fields: Vec<String> = props
                .iter()
                .take(8)
                .map(|p| format!("  {}: {}", p.name, type_display_name(&p.type_)))
                .collect();
            let ellipsis = if props.len() > 8 { "\n  ..." } else { "" };
            format!("{{\n{}{}\n}}", fields.join("\n"), ellipsis)
        }
        TypeKind::Enum(_, variants) => {
            let vs: Vec<&str> = variants.iter().take(12).map(|v| v.name.as_str()).collect();
            let ellipsis = if variants.len() > 12 { ", ..." } else { "" };
            format!("{{{}{}}}", vs.join(", "), ellipsis)
        }
        TypeKind::TaggedUnion(_, elements) => {
            let vs: Vec<String> = elements.iter().take(8).map(|e| e.tag.clone()).collect();
            let ellipsis = if elements.len() > 8 { " | ..." } else { "" };
            vs.join(" | ") + ellipsis
        }
        other => other.to_string(),
    }
}

/// Return a user-friendly type display name, preferring `ident` over the kind string.
/// Strips module-qualified prefixes (e.g. "forest example main.MyFn" → "MyFn").
fn type_display_name(t: &crate::analyzer::common::Type) -> String {
    if !t.ident.is_empty() {
        // Strip module prefix: take everything after the last '.'
        return t.ident.rsplit('.').next().unwrap_or(&t.ident).to_string();
    }
    t.to_string()
}

/// Strip module prefix from an ident, falling back to `word` if needed.
fn display_name<'a>(qualified: &'a str, word: &'a str) -> &'a str {
    qualified.rsplit('.').next().unwrap_or(word)
}

// ─── Doc comment extraction ─────────────────────────────────────────────────────

/// Look up the defining module for a symbol and extract any doc comment above it.
fn extract_doc_comment_for_symbol(
    project: &Project,
    module_path: &str,
    kind: &SymbolKind,
) -> Option<String> {
    let symbol_pos = symbol_def_start(kind);

    let db = project.module_db.lock().ok()?;
    let module = db.iter().find(|m| m.path == module_path)?;

    extract_doc_comment(&module.token_db, symbol_pos)
}

/// Get the `symbol_start` char offset from a `SymbolKind`.
fn symbol_def_start(kind: &SymbolKind) -> usize {
    match kind {
        SymbolKind::Fn(f) => f.lock().unwrap().symbol_start,
        SymbolKind::Var(v) => v.lock().unwrap().symbol_start,
        SymbolKind::Const(c) => c.lock().unwrap().symbol_start,
        SymbolKind::Type(t) => t.lock().unwrap().symbol_start,
    }
}

/// Scan `token_db` backwards from `symbol_pos` to collect contiguous doc-comment
/// lines immediately preceding the definition.
fn extract_doc_comment(token_db: &[Token], symbol_pos: usize) -> Option<String> {
    // Find the token at or just before the symbol position.
    let token_idx = token_db.iter().rposition(|t| t.start <= symbol_pos)?;
    let def_line = token_db[token_idx].line;

    let mut comments: Vec<String> = Vec::new();
    let mut expected_line = def_line; // first comment must be on expected_line - 1

    for i in (0..=token_idx).rev() {
        let token = &token_db[i];

        // Skip tokens on the definition line itself (keywords, ident, etc.).
        if token.line >= expected_line && !comments.is_empty() {
            continue;
        }
        if token.line == def_line {
            continue;
        }

        match token.token_type {
            TokenType::LineComment | TokenType::BlockComment => {
                if token.line == expected_line - 1 {
                    expected_line = token.line;
                    comments.push(token.literal.clone());
                } else {
                    break;
                }
            }
            _ => break,
        }
    }

    if comments.is_empty() {
        return None;
    }

    // Collected bottom-up, reverse to top-down order.
    comments.reverse();

    let cleaned: Vec<String> = comments
        .iter()
        .map(|c| strip_comment_marker(c))
        .collect();

    Some(cleaned.join("\n"))
}

/// Remove `//`, `/* */`, or leading `*` markers from a comment token literal.
fn strip_comment_marker(comment: &str) -> String {
    let trimmed = comment.trim();
    if let Some(rest) = trimmed.strip_prefix("//") {
        // Line comment: strip `//` and optional leading space.
        rest.strip_prefix(' ').unwrap_or(rest).to_string()
    } else if let Some(rest) = trimmed.strip_prefix("/*") {
        // First line of a block comment.
        let rest = rest.strip_suffix("*/").unwrap_or(rest);
        rest.trim().to_string()
    } else if let Some(rest) = trimmed.strip_suffix("*/") {
        // Last line of a block comment.
        let rest = rest.trim();
        let rest = rest.strip_prefix('*').unwrap_or(rest);
        rest.trim_start().to_string()
    } else {
        // Middle line of a block comment — strip optional leading `*`.
        let rest = trimmed.strip_prefix('*').unwrap_or(trimmed);
        rest.strip_prefix(' ').unwrap_or(rest).to_string()
    }
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::common::{AstFnDef, Type, VarDeclExpr};
    use std::sync::{Arc, Mutex};

    #[test]
    fn hover_var() {
        let var = Arc::new(Mutex::new(VarDeclExpr {
            ident: "mymod.count".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 5,
            type_: Type::new(TypeKind::Int64),
            be_capture: false,
            heap_ident: None,
        }));
        let content = format_hover_content(&SymbolKind::Var(var), "count", None);
        assert!(content.contains("var count: i64"), "got: {}", content);
    }

    #[test]
    fn hover_fn_simple() {
        let fndef = Arc::new(Mutex::new(AstFnDef {
            fn_name: "add".into(),
            return_type: Type::new(TypeKind::Int64),
            ..AstFnDef::default()
        }));
        let content = format_hover_content(&SymbolKind::Fn(fndef), "add", None);
        assert!(content.contains("fn add(): i64"), "got: {}", content);
    }

    #[test]
    fn hover_fn_with_params() {
        let param_a = Arc::new(Mutex::new(VarDeclExpr {
            ident: "a".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 0,
            type_: Type::new(TypeKind::Int64),
            be_capture: false,
            heap_ident: None,
        }));
        let param_b = Arc::new(Mutex::new(VarDeclExpr {
            ident: "b".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 0,
            type_: Type::new(TypeKind::String),
            be_capture: false,
            heap_ident: None,
        }));
        let fndef = Arc::new(Mutex::new(AstFnDef {
            fn_name: "greet".into(),
            params: vec![param_a, param_b],
            return_type: Type::new(TypeKind::Void),
            ..AstFnDef::default()
        }));
        let content = format_hover_content(&SymbolKind::Fn(fndef), "greet", None);
        assert!(content.contains("a: i64, b: string"), "got: {}", content);
    }

    #[test]
    fn display_name_strips_prefix() {
        assert_eq!(display_name("mymod.hello", "hello"), "hello");
        assert_eq!(display_name("hello", "hello"), "hello");
    }

    #[test]
    fn hover_with_doc_comment() {
        let var = Arc::new(Mutex::new(VarDeclExpr {
            ident: "mymod.count".into(),
            symbol_id: 0,
            symbol_start: 0,
            symbol_end: 5,
            type_: Type::new(TypeKind::Int64),
            be_capture: false,
            heap_ident: None,
        }));
        let content = format_hover_content(
            &SymbolKind::Var(var),
            "count",
            Some("The number of items"),
        );
        assert!(content.contains("var count: i64"), "got: {}", content);
        assert!(content.contains("---"), "should have separator: {}", content);
        assert!(content.contains("The number of items"), "should have doc: {}", content);
    }

    #[test]
    fn extract_line_comments() {
        let tokens = vec![
            Token::new(TokenType::LineComment, "// hello world".into(), 0, 14, 1),
            Token::new(TokenType::LineComment, "// second line".into(), 15, 29, 2),
            Token::new(TokenType::Ident, "fn".into(), 30, 32, 3),
            Token::new(TokenType::Ident, "foo".into(), 33, 36, 3),
        ];
        let doc = extract_doc_comment(&tokens, 30);
        assert_eq!(doc.as_deref(), Some("hello world\nsecond line"));
    }

    #[test]
    fn extract_no_comment_when_gap() {
        let tokens = vec![
            Token::new(TokenType::LineComment, "// orphan".into(), 0, 9, 1),
            // line 2 is blank (no tokens)
            Token::new(TokenType::Ident, "fn".into(), 20, 22, 3),
            Token::new(TokenType::Ident, "foo".into(), 23, 26, 3),
        ];
        let doc = extract_doc_comment(&tokens, 20);
        assert_eq!(doc, None);
    }

    #[test]
    fn strip_line_comment_markers() {
        assert_eq!(strip_comment_marker("// hello"), "hello");
        assert_eq!(strip_comment_marker("//hello"), "hello");
        assert_eq!(strip_comment_marker("/* single */"), "single");
        assert_eq!(strip_comment_marker(" * middle"), "middle");
    }
}
