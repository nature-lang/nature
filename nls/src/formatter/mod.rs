use crate::analyzer::common::AnalyzerError;
use crate::analyzer::lexer::{Lexer, Token, TokenType};
use std::collections::HashSet;

mod engine;
mod rules;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum BlockKind {
    Other,
    Enum,
    Match,
}

/// Formatting options for the Nature formatter.
#[derive(Debug, Clone)]
pub struct FormatterOptions {
    pub line_width: usize,
}

impl Default for FormatterOptions {
    fn default() -> Self {
        Self { line_width: 120 }
    }
}

/// Format a Nature source file using the canonical style.
///
/// Returns the formatted text or an error message when the source cannot be
/// lexed cleanly.
pub fn format_source(source: &str) -> Result<String, String> {
    format_with_options(source, FormatterOptions::default())
}

/// Format source text with explicit options.
pub fn format_with_options(
    source: &str,
    _options: FormatterOptions,
) -> Result<String, String> {
    format_with_options_and_errors(source, _options).map_err(|errors| {
        errors
            .first()
            .map(|error| error.message.clone())
            .unwrap_or_else(|| "unknown formatter error".to_string())
    })
}

/// Format source text and return all lexer errors if formatting cannot proceed.
pub fn format_source_with_errors(source: &str) -> Result<String, Vec<AnalyzerError>> {
    format_with_options_and_errors(source, FormatterOptions::default())
}

fn format_with_options_and_errors(
    source: &str,
    _options: FormatterOptions,
) -> Result<String, Vec<AnalyzerError>> {
    let mut lexer = Lexer::new(source.to_string());
    let (tokens, _, errors) = lexer.scan();
    if !errors.is_empty() {
        return Err(errors);
    }

    let mut formatter = Formatter::new(source, tokens);
    formatter.format().map_err(|message| vec![AnalyzerError::new(0, 0, message)])
}

struct Formatter<'a> {
    source: &'a str,
    byte_offsets: Vec<usize>,
    tokens: Vec<Token>,
    generic_angle_indexes: HashSet<usize>,
    block_stack: Vec<BlockKind>,
    output: String,
    indent: usize,
    in_for_header: bool,
    newline: bool,
    prev_token_type: Option<TokenType>,
    current_index: usize,
    pending_block_kind: BlockKind,
    inline_empty_block: bool,
}

impl<'a> Formatter<'a> {
    fn new(source: &'a str, tokens: Vec<Token>) -> Self {
        let mut byte_offsets = Vec::with_capacity(source.chars().count() + 1);
        for (byte_idx, _) in source.char_indices() {
            byte_offsets.push(byte_idx);
        }
        byte_offsets.push(source.len());

        let generic_angle_indexes = Self::detect_generic_angle_indexes(&tokens);

        Self {
            source,
            byte_offsets,
            tokens,
            generic_angle_indexes,
            block_stack: Vec::new(),
            output: String::with_capacity(source.len()),
            indent: 0,
            in_for_header: false,
            newline: true,
            prev_token_type: None,
            current_index: 0,
            pending_block_kind: BlockKind::Other,
            inline_empty_block: false,
        }
    }

    fn emit_indent(&mut self) {
        if self.newline {
            for _ in 0..self.indent {
                self.output.push_str("    ");
            }
            self.newline = false;
        }
    }

    fn emit_token_text(&mut self, token: &Token) {
        let text = self.token_text(token).to_string();
        self.output.push_str(&text);
    }

    fn emit_token_text_raw(&mut self, raw: &str) {
        self.output.push_str(raw);
    }

    fn token_text(&self, token: &Token) -> &str {
        let start = self.char_to_byte(token.start);
        let end = self.char_to_byte(token.end);
        &self.source[start..end]
    }

    fn char_to_byte(&self, char_idx: usize) -> usize {
        if char_idx >= self.byte_offsets.len() {
            *self.byte_offsets.last().unwrap_or(&self.source.len())
        } else {
            self.byte_offsets[char_idx]
        }
    }

    fn ensure_newline(&mut self) {
        if !self.newline {
            self.output.push('\n');
            self.newline = true;
        }
    }
}

#[cfg(test)]
mod tests;