//! Semantic token request handlers (`textDocument/semanticTokens/*`).

use tower_lsp::lsp_types::*;

use crate::analyzer::lexer::Token;
use crate::project::Project;

use super::Backend;

impl Backend {
    pub(crate) async fn handle_semantic_tokens_full(
        &self,
        params: SemanticTokensParams,
    ) -> Option<SemanticTokensResult> {
        let uri = params.text_document.uri;
        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;
        let (rope, tokens) = module_semantic_tokens(&project, file_path)?;

        let data = encode_semantic_tokens(&tokens, &rope, None);
        Some(SemanticTokensResult::Tokens(SemanticTokens {
            result_id: None,
            data,
        }))
    }

    pub(crate) async fn handle_semantic_tokens_range(
        &self,
        params: SemanticTokensRangeParams,
    ) -> Option<SemanticTokensRangeResult> {
        let uri = params.text_document.uri;
        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;
        let (rope, tokens) = module_semantic_tokens(&project, file_path)?;

        let data = encode_semantic_tokens(&tokens, &rope, Some(params.range));
        Some(SemanticTokensRangeResult::Tokens(SemanticTokens {
            result_id: None,
            data,
        }))
    }
}

fn module_semantic_tokens(project: &Project, file_path: &str) -> Option<(ropey::Rope, Vec<Token>)> {
    let module_index = {
        let module_handled = project.module_handled.lock().ok()?;
        module_handled.get(file_path).copied()?
    };

    let module_db = project.module_db.lock().ok()?;
    let module = module_db.get(module_index)?;

    // sem_token_db carries parser-resolved token kinds. If unavailable,
    // fall back to lexer token types so highlighting still works.
    let tokens = if module.sem_token_db.is_empty() {
        module.token_db.clone()
    } else {
        module.sem_token_db.clone()
    };

    Some((module.rope.clone(), tokens))
}

fn position_to_char_offset(position: Position, rope: &ropey::Rope) -> Option<usize> {
    let line_start = rope.try_line_to_char(position.line as usize).ok()?;
    let offset = line_start + position.character as usize;
    if offset > rope.len_chars() {
        return None;
    }
    Some(offset)
}

fn token_in_range(token: &Token, range: Range, rope: &ropey::Rope) -> bool {
    let Some(range_start) = position_to_char_offset(range.start, rope) else {
        return false;
    };
    let Some(range_end) = position_to_char_offset(range.end, rope) else {
        return false;
    };
    token.start < range_end && token.end > range_start
}

fn encode_semantic_tokens(
    tokens: &[Token],
    rope: &ropey::Rope,
    range: Option<Range>,
) -> Vec<SemanticToken> {
    // Index of VARIABLE in LEGEND_TYPE (the default for unresolved idents).
    let variable_idx = crate::analyzer::lexer::semantic_token_type_index(
        tower_lsp::lsp_types::SemanticTokenType::VARIABLE,
    ) as u32;

    let mut sorted: Vec<&Token> = tokens
        .iter()
        // Only emit semantic tokens for identifiers where we add value
        // beyond the TextMate grammar (resolved kind: function, type, property,
        // namespace, macro, label). Let TM grammar handle keywords, comments,
        // strings, numbers, and operators — this avoids color flickering
        // during typing when stale semantic tokens don't match changed text.
        .filter(|token| {
            matches!(
                token.token_type,
                crate::analyzer::lexer::TokenType::Ident
                    | crate::analyzer::lexer::TokenType::MacroIdent
                    | crate::analyzer::lexer::TokenType::Label
            )
        })
        // Skip idents that are still at their default VARIABLE type — let TM
        // grammar color them so unresolved identifiers don't flicker.
        .filter(|token| token.semantic_token_type as u32 != variable_idx)
        .filter(|token| token.length > 0)
        .filter(|token| range.map(|r| token_in_range(token, r, rope)).unwrap_or(true))
        .collect();

    sorted.sort_by_key(|token| (token.line, token.start));

    let mut prev_line: u32 = 0;
    let mut prev_start: u32 = 0;
    let mut encoded: Vec<SemanticToken> = Vec::with_capacity(sorted.len());

    for token in sorted {
        // Lexer uses 1-based line numbering.
        let line = token.line.saturating_sub(1);
        let Some(line_start) = rope.try_line_to_char(line).ok() else {
            continue;
        };

        let start = token.start.saturating_sub(line_start);
        let line_u32 = line as u32;
        let start_u32 = start as u32;

        let (delta_line, delta_start) = if encoded.is_empty() {
            (line_u32, start_u32)
        } else if line_u32 == prev_line {
            (0, start_u32.saturating_sub(prev_start))
        } else {
            (line_u32.saturating_sub(prev_line), start_u32)
        };

        encoded.push(SemanticToken {
            delta_line,
            delta_start,
            length: token.length as u32,
            token_type: token.semantic_token_type as u32,
            token_modifiers_bitset: 0,
        });

        prev_line = line_u32;
        prev_start = start_u32;
    }

    encoded
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::lexer::{semantic_token_type_index, TokenType};
    use tower_lsp::lsp_types::SemanticTokenType;

    fn tok(
        token_type: TokenType,
        semantic: SemanticTokenType,
        literal: &str,
        start: usize,
        end: usize,
        line: usize,
    ) -> Token {
        let mut token = Token::new(token_type, literal.to_string(), start, end, line);
        token.semantic_token_type = semantic_token_type_index(semantic);
        token
    }

    #[test]
    fn encode_full_orders_and_deltas_tokens() {
        let rope = ropey::Rope::from_str("ab cd\nef");
        let tokens = vec![
            tok(
                TokenType::Ident,
                SemanticTokenType::NAMESPACE,
                "ef",
                6,
                8,
                2,
            ),
            tok(
                TokenType::Ident,
                SemanticTokenType::FUNCTION,
                "ab",
                0,
                2,
                1,
            ),
            tok(
                TokenType::Ident,
                SemanticTokenType::TYPE,
                "cd",
                3,
                5,
                1,
            ),
        ];

        let encoded = encode_semantic_tokens(&tokens, &rope, None);
        assert_eq!(encoded.len(), 3);

        // first token (line 0, col 0)
        assert_eq!(encoded[0].delta_line, 0);
        assert_eq!(encoded[0].delta_start, 0);
        assert_eq!(encoded[0].length, 2);

        // second token same line, starts at col 3 => delta_start 3
        assert_eq!(encoded[1].delta_line, 0);
        assert_eq!(encoded[1].delta_start, 3);
        assert_eq!(encoded[1].length, 2);

        // third token next line, starts at col 0
        assert_eq!(encoded[2].delta_line, 1);
        assert_eq!(encoded[2].delta_start, 0);
        assert_eq!(encoded[2].length, 2);
    }

    #[test]
    fn encode_range_filters_tokens() {
        let rope = ropey::Rope::from_str("ab cd\nef");
        let tokens = vec![
            tok(
                TokenType::Ident,
                SemanticTokenType::FUNCTION,
                "ab",
                0,
                2,
                1,
            ),
            tok(
                TokenType::Ident,
                SemanticTokenType::TYPE,
                "cd",
                3,
                5,
                1,
            ),
            tok(
                TokenType::Ident,
                SemanticTokenType::PROPERTY,
                "ef",
                6,
                8,
                2,
            ),
        ];

        // only second line (line index 1)
        let range = Range::new(Position::new(1, 0), Position::new(1, 2));
        let encoded = encode_semantic_tokens(&tokens, &rope, Some(range));
        assert_eq!(encoded.len(), 1);
        assert_eq!(encoded[0].delta_line, 1);
        assert_eq!(encoded[0].delta_start, 0);
    }
}