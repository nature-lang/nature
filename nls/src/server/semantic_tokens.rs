//! Semantic token providers (full and range).

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

use log::debug;
use tower_lsp::lsp_types::*;

use crate::analyzer::lexer::TokenType;

use super::Backend;

impl Backend {
    pub(crate) async fn handle_semantic_tokens_full(&self, params: SemanticTokensParams) -> Option<SemanticTokensResult> {
        let file_path = params.text_document.uri.path().to_string();
        debug!("semantic_token_full");

        let project = self.get_file_project(&file_path)?;

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path.as_str())?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let m = module_db.get(module_index)?;

        let im_complete_tokens = &m.sem_token_db;

        // Compute a hash of the token database to detect changes
        let mut hasher = DefaultHasher::new();
        im_complete_tokens.len().hash(&mut hasher);
        for token in im_complete_tokens.iter() {
            token.start.hash(&mut hasher);
            token.end.hash(&mut hasher);
            token.length.hash(&mut hasher);
            (token.semantic_token_type.clone() as u32).hash(&mut hasher);
        }
        let token_hash = hasher.finish();

        // Check cache
        if let Some(cached) = self.semantic_token_cache.get(&file_path) {
            if cached.0 == token_hash {
                debug!("semantic_token_full: returning cached result ({} tokens)", cached.1.len());
                return Some(SemanticTokensResult::Tokens(SemanticTokens {
                    result_id: None,
                    data: cached.1.clone(),
                }));
            }
        }

        let rope = m.rope.clone();
        let im_complete_tokens = im_complete_tokens.clone();

        let mut pre_line = 0;
        let mut pre_line_start = 0;
        let mut pre_start = 0;
        let semantic_tokens: Vec<SemanticToken> = im_complete_tokens
            .iter()
            .filter_map(|token| {
                let line = rope.try_char_to_line(token.start).ok()? as u32;
                let _end_line = rope.try_char_to_line(token.end).ok()? as u32;
                let line_first = rope.try_line_to_char(line as usize).ok()? as u32;
                let line_start = token.start as u32 - line_first;

                if token.start < pre_start && token.token_type == TokenType::StringLiteral {
                    return None;
                }

                let delta_line = line - pre_line;
                let delta_start = if delta_line == 0 { line_start - pre_line_start } else { line_start };

                let ret = Some(SemanticToken {
                    delta_line,
                    delta_start,
                    length: token.length as u32,
                    token_type: token.semantic_token_type.clone() as u32,
                    token_modifiers_bitset: 0,
                });
                pre_line = line;
                pre_line_start = line_start;
                pre_start = token.start;
                ret
            })
            .collect::<Vec<_>>();

        // Cache the result
        self.semantic_token_cache.insert(file_path, (token_hash, semantic_tokens.clone()));

        Some(SemanticTokensResult::Tokens(SemanticTokens {
            result_id: None,
            data: semantic_tokens,
        }))
    }

    pub(crate) async fn handle_semantic_tokens_range(&self, params: SemanticTokensRangeParams) -> Option<SemanticTokensRangeResult> {
        let file_path = params.text_document.uri.path();

        let project = self.get_file_project(&file_path)?;

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let m = module_db.get(module_index)?;

        let im_complete_tokens = m.sem_token_db.clone();
        let rope = m.rope.clone();

        let mut pre_line = 0;
        let mut pre_start = 0;
        let semantic_tokens = im_complete_tokens
            .iter()
            .filter_map(|token| {
                let line = rope.try_byte_to_line(token.start).ok()? as u32;
                let first = rope.try_line_to_char(line as usize).ok()? as u32;
                let start = rope.try_byte_to_char(token.start).ok()? as u32 - first;
                let ret = Some(SemanticToken {
                    delta_line: line - pre_line,
                    delta_start: if start >= pre_start { start - pre_start } else { start },
                    length: token.length as u32,
                    token_type: token.semantic_token_type.clone() as u32,
                    token_modifiers_bitset: 0,
                });
                pre_line = line;
                pre_start = start;
                ret
            })
            .collect::<Vec<_>>();

        Some(SemanticTokensRangeResult::Tokens(SemanticTokens { result_id: None, data: semantic_tokens }))
    }
}
