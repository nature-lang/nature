use super::{BlockKind, Formatter, Token, TokenType};

impl<'a> Formatter<'a> {
    pub(super) fn format(&mut self) -> Result<String, String> {
        for idx in 0..self.tokens.len() {
            self.current_index = idx;
            let token = self.tokens[idx].clone();
            let next_token = self.tokens.get(idx + 1).cloned();
            let token_type = token.token_type.clone();

            if token_type == TokenType::Eof {
                break;
            }

            if token_type == TokenType::For {
                self.in_for_header = true;
            }

            if token_type == TokenType::Enum {
                self.pending_block_kind = BlockKind::Enum;
            }

            if token_type == TokenType::Match {
                self.pending_block_kind = BlockKind::Match;
            }

            if token_type == TokenType::StmtEof {
                if self.in_for_header {
                    self.output.push(';');
                    self.output.push(' ');
                    self.newline = false;
                } else {
                    self.ensure_newline();
                }
                self.prev_token_type = Some(TokenType::StmtEof);
                continue;
            }

            if token_type == TokenType::LineComment {
                self.emit_line_comment(&token);
                self.prev_token_type = Some(TokenType::LineComment);
                continue;
            }

            if token_type == TokenType::BlockComment {
                self.emit_block_comment(&token);
                self.prev_token_type = Some(TokenType::BlockComment);
                continue;
            }

            if token_type == TokenType::RightCurly {
                self.emit_right_curly(next_token.as_ref());
                self.prev_token_type = Some(TokenType::RightCurly);
                continue;
            }

            if token_type == TokenType::LeftCurly {
                self.emit_left_curly(next_token.as_ref());
                self.prev_token_type = Some(TokenType::LeftCurly);
                continue;
            }

            if token_type == TokenType::Comma && self.in_enum_block() {
                self.emit_separator(&token_type);
                self.emit_token_text(&token);

                if !matches!(next_token.as_ref().map(|token| &token.token_type), Some(TokenType::RightCurly)) {
                    self.output.push('\n');
                    self.newline = true;
                }

                self.prev_token_type = Some(TokenType::Comma);
                continue;
            }

            if token_type == TokenType::GreaterEqual && self.is_generic_angle_token(self.current_index) {
                self.emit_separator(&token_type);
                self.emit_token_text_raw("> =");
                self.prev_token_type = Some(TokenType::GreaterEqual);
                continue;
            }

            self.emit_separator(&token_type);
            self.emit_token_text(&token);
            if self.should_break_after_match_arm_value(&token_type, next_token.as_ref()) {
                self.output.push('\n');
                self.newline = true;
            }
            self.prev_token_type = Some(token_type);
        }

        if !self.output.ends_with('\n') {
            self.output.push('\n');
        }

        Ok(std::mem::take(&mut self.output))
    }

    fn emit_left_curly(&mut self, next_token: Option<&Token>) {
        self.emit_separator(&TokenType::LeftCurly);
        self.emit_token_text_raw("{");
        self.in_for_header = false;
        let block_kind = std::mem::replace(&mut self.pending_block_kind, BlockKind::Other);
        self.block_stack.push(block_kind);

        if let Some(next) = next_token {
            if next.token_type == TokenType::RightCurly {
                self.inline_empty_block = true;
                self.newline = false;
                return;
            }
        }

        self.output.push('\n');
        self.newline = true;
        self.indent += 1;
    }

    fn emit_right_curly(&mut self, next_token: Option<&Token>) {
        let _ = self.block_stack.pop();

        if self.inline_empty_block {
            self.output.push('}');
            self.inline_empty_block = false;

            if let Some(next) = next_token {
                if next.token_type == TokenType::Else || next.token_type == TokenType::ElseIf {
                    self.output.push(' ');
                    self.newline = false;
                    return;
                }
            }

            self.output.push('\n');
            self.newline = true;
            return;
        }

        if self.indent > 0 {
            self.indent -= 1;
        }

        if !self.newline {
            self.output.push('\n');
        }
        self.newline = true;
        self.emit_indent();
        self.output.push('}');

        if let Some(next) = next_token {
            if next.token_type == TokenType::Else || next.token_type == TokenType::ElseIf {
                self.output.push(' ');
                self.newline = false;
                return;
            }
        }

        self.output.push('\n');
        self.newline = true;
    }

    fn emit_line_comment(&mut self, token: &Token) {
        if !self.newline {
            self.output.push_str("  ");
        } else {
            self.emit_indent();
        }
        let text = self.token_text(token).to_string();
        self.output.push_str(&text);
        self.output.push('\n');
        self.newline = true;
    }

    fn emit_block_comment(&mut self, token: &Token) {
        if !self.newline {
            self.output.push('\n');
        }
        self.newline = true;
        self.emit_indent();
        let text = self.token_text(token).to_string();
        self.output.push_str(&text);
        self.output.push('\n');
        self.newline = true;
    }

    fn emit_separator(&mut self, token_type: &TokenType) {
        if self.newline {
            self.emit_indent();
            return;
        }

        if let Some(prev_type) = &self.prev_token_type {
            if self.needs_line_break(prev_type, token_type) {
                self.output.push('\n');
                self.newline = true;
                self.emit_indent();
            } else if self.needs_space(prev_type, token_type) {
                self.output.push(' ');
            }
        }
    }
}