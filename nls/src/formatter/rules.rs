use super::{BlockKind, Formatter, Token, TokenType};
use std::collections::HashSet;

impl<'a> Formatter<'a> {
    pub(super) fn needs_line_break(&self, prev: &TokenType, current: &TokenType) -> bool {
        if self.in_for_header || self.current_index == 0 {
            return false;
        }

        let prev_token = &self.tokens[self.current_index - 1];
        let current_token = &self.tokens[self.current_index];

        if prev_token.line != current_token.line {
            return false;
        }

        matches!(prev, TokenType::RightParen)
            && matches!(current,
                TokenType::Ident
                | TokenType::IntLiteral
                | TokenType::FloatLiteral
                | TokenType::StringLiteral
                | TokenType::True
                | TokenType::False
                | TokenType::Null
                | TokenType::LeftSquare)
    }

    pub(super) fn needs_space(&self, prev: &TokenType, current: &TokenType) -> bool {
        if self.is_generic_angle_token(self.current_index) {
            return false;
        }

        if self.current_index > 0 && self.is_generic_angle_token(self.current_index - 1) {
            return self.should_space_after_generic_angle(current);
        }

        if matches!(current, TokenType::RightParen
            | TokenType::RightSquare
            | TokenType::RightCurly
            | TokenType::Comma
            | TokenType::Colon
            | TokenType::Dot
            | TokenType::Question
            | TokenType::StmtEof)
        {
            return false;
        }

        if matches!(prev, TokenType::LeftParen
            | TokenType::LeftSquare
            | TokenType::LeftCurly
            | TokenType::Dot)
        {
            return false;
        }

        if self.is_keyword(prev) && current == &TokenType::LeftParen {
            return true;
        }

        if self.is_prefix_operator_without_space(prev, current) {
            return false;
        }

        if self.is_operator(prev) || self.is_operator(current) {
            return true;
        }

        if self.is_word(prev) && self.is_word(current) {
            return true;
        }

        if self.is_keyword(prev) && self.is_word(current) {
            return true;
        }

        if self.is_keyword(prev) && self.is_keyword(current) {
            return true;
        }

        if self.is_word(prev) && self.is_keyword(current) {
            return true;
        }

        if matches!(prev, TokenType::RightParen | TokenType::RightSquare)
            && (self.is_keyword(current) || self.is_word(current))
        {
            return true;
        }

        if prev == &TokenType::Question && (self.is_word(current) || self.is_keyword(current)) {
            return true;
        }

        if self.is_word(prev) && current == &TokenType::LeftCurly {
            return true;
        }

        if self.is_keyword(prev) && current == &TokenType::LeftCurly {
            return true;
        }

        if prev == &TokenType::RightParen && current == &TokenType::LeftCurly {
            return true;
        }

        if prev == &TokenType::Not && current == &TokenType::LeftCurly {
            return true;
        }

        if prev == &TokenType::Colon
            && (self.is_word(current)
                || matches!(current, TokenType::LeftSquare | TokenType::LeftCurly | TokenType::LeftParen))
        {
            return true;
        }

        if prev == &TokenType::Comma {
            if self.in_enum_block() {
                return false;
            }

            if !matches!(current, TokenType::RightParen | TokenType::RightSquare | TokenType::RightCurly) {
                return true;
            }
        }

        false
    }

    fn is_prefix_operator_without_space(&self, prev: &TokenType, current: &TokenType) -> bool {
        if !matches!(prev, TokenType::Minus | TokenType::Not | TokenType::And | TokenType::Star) {
            return false;
        }

        if !matches!(current,
            TokenType::Ident
            | TokenType::MacroIdent
            | TokenType::Label
            | TokenType::IntLiteral
            | TokenType::FloatLiteral
            | TokenType::StringLiteral
            | TokenType::True
            | TokenType::False
            | TokenType::Null
            | TokenType::LeftParen
            | TokenType::LeftSquare)
        {
            return false;
        }

        if self.current_index < 2 {
            return true;
        }

        !self.token_ends_expression(&self.tokens[self.current_index - 2].token_type)
    }

    fn token_ends_expression(&self, token_type: &TokenType) -> bool {
        self.is_word(token_type)
            || matches!(token_type,
                TokenType::RightParen
                | TokenType::RightSquare
                | TokenType::RightCurly
                | TokenType::RightAngle)
    }

    pub(super) fn in_enum_block(&self) -> bool {
        matches!(self.block_stack.last(), Some(BlockKind::Enum))
    }

    fn in_match_block(&self) -> bool {
        matches!(self.block_stack.last(), Some(BlockKind::Match))
    }

    fn should_space_after_generic_angle(&self, current: &TokenType) -> bool {
        let prev = &self.tokens[self.current_index - 1].token_type;

        if prev == &TokenType::LeftAngle {
            return false;
        }

        matches!(current,
            TokenType::Ident
            | TokenType::MacroIdent
            | TokenType::Label
            | TokenType::String
            | TokenType::Bool
            | TokenType::Int
            | TokenType::Uint
            | TokenType::Float
            | TokenType::U8
            | TokenType::U16
            | TokenType::U32
            | TokenType::U64
            | TokenType::I8
            | TokenType::I16
            | TokenType::I32
            | TokenType::I64
            | TokenType::F32
            | TokenType::F64
            | TokenType::Void
            | TokenType::Any
            | TokenType::Null
            | TokenType::True
            | TokenType::False
                | TokenType::Equal
            | TokenType::LeftCurly)
    }

    pub(super) fn is_generic_angle_token(&self, index: usize) -> bool {
        self.generic_angle_indexes.contains(&index)
    }

    pub(super) fn detect_generic_angle_indexes(tokens: &[Token]) -> HashSet<usize> {
        let mut generic_angle_indexes = HashSet::new();

        for idx in 0..tokens.len() {
            if tokens[idx].token_type != TokenType::LeftAngle {
                continue;
            }

            if !Self::can_open_generic_args(tokens, idx) {
                continue;
            }

            if let Some(close_idx) = Self::find_generic_close(tokens, idx) {
                generic_angle_indexes.insert(idx);
                generic_angle_indexes.insert(close_idx);
            }
        }

        generic_angle_indexes
    }

    fn can_open_generic_args(tokens: &[Token], idx: usize) -> bool {
        if idx == 0 {
            return false;
        }

        let prev = &tokens[idx - 1].token_type;
        matches!(prev,
            TokenType::Ident
            | TokenType::Chan
            | TokenType::RightParen
            | TokenType::RightSquare
            | TokenType::RightAngle)
    }

    fn find_generic_close(tokens: &[Token], left_idx: usize) -> Option<usize> {
        let current_line = tokens[left_idx].line;
        let mut depth = 1;
        let mut pos = left_idx + 1;

        while pos < tokens.len() {
            let token = &tokens[pos];

            if token.token_type == TokenType::Eof
                || token.token_type == TokenType::StmtEof
                || token.line != current_line
            {
                return None;
            }

            match token.token_type {
                TokenType::LeftAngle => depth += 1,
                TokenType::GreaterEqual if depth == 1 => {
                    return Self::generic_close_is_valid(tokens, left_idx, pos).then_some(pos);
                }
                TokenType::RightAngle => {
                    depth -= 1;
                    if depth == 0 {
                        return Self::generic_close_is_valid(tokens, left_idx, pos).then_some(pos);
                    }
                }
                _ => {}
            }

            pos += 1;
        }

        None
    }

    fn generic_close_is_valid(tokens: &[Token], left_idx: usize, right_idx: usize) -> bool {
        if right_idx <= left_idx + 1 {
            return false;
        }

        let next = tokens.get(right_idx + 1).map(|token| &token.token_type);
        matches!(next,
            Some(TokenType::Dot)
            | Some(TokenType::Ident)
            | Some(TokenType::Comma)
            | Some(TokenType::RightParen)
            | Some(TokenType::RightSquare)
            | Some(TokenType::RightCurly)
            | Some(TokenType::Question)
            | Some(TokenType::Or)
            | Some(TokenType::LeftCurly)
            | Some(TokenType::Equal)
            | Some(TokenType::StmtEof)
            | Some(TokenType::Eof)
            | None)
    }

    fn is_operator(&self, token_type: &TokenType) -> bool {
        matches!(token_type,
            TokenType::Plus
            | TokenType::Minus
            | TokenType::Star
            | TokenType::Slash
            | TokenType::Percent
            | TokenType::Equal
            | TokenType::EqualEqual
            | TokenType::NotEqual
            | TokenType::GreaterEqual
            | TokenType::LessEqual
            | TokenType::LessThan
            | TokenType::RightAngle
            | TokenType::AndAnd
            | TokenType::OrOr
            | TokenType::And
            | TokenType::Or
            | TokenType::Xor
            | TokenType::LeftShift
            | TokenType::RightShift
            | TokenType::PlusEqual
            | TokenType::MinusEqual
            | TokenType::StarEqual
            | TokenType::SlashEqual
            | TokenType::PercentEqual
            | TokenType::AndEqual
            | TokenType::OrEqual
            | TokenType::XorEqual
            | TokenType::LeftShiftEqual
            | TokenType::RightShiftEqual
            | TokenType::RightArrow
            | TokenType::LeftAngle)
    }

    fn is_word(&self, token_type: &TokenType) -> bool {
        matches!(token_type,
            TokenType::Ident
            | TokenType::MacroIdent
            | TokenType::Label
            | TokenType::StringLiteral
            | TokenType::IntLiteral
            | TokenType::FloatLiteral
            | TokenType::String
            | TokenType::Bool
            | TokenType::Int
            | TokenType::Uint
            | TokenType::Float
            | TokenType::U8
            | TokenType::U16
            | TokenType::U32
            | TokenType::U64
            | TokenType::I8
            | TokenType::I16
            | TokenType::I32
            | TokenType::I64
            | TokenType::F32
            | TokenType::F64
            | TokenType::Void
            | TokenType::Any
            | TokenType::Null
            | TokenType::True
            | TokenType::False)
    }

    fn is_keyword(&self, token_type: &TokenType) -> bool {
        matches!(token_type,
            TokenType::If
            | TokenType::Else
            | TokenType::ElseIf
            | TokenType::For
            | TokenType::In
            | TokenType::Break
            | TokenType::Continue
            | TokenType::Return
            | TokenType::Let
            | TokenType::Var
            | TokenType::Const
            | TokenType::Test
            | TokenType::Fn
            | TokenType::Import
            | TokenType::Type
            | TokenType::Struct
            | TokenType::Interface
            | TokenType::Enum
            | TokenType::Union
            | TokenType::Throw
            | TokenType::Try
            | TokenType::Catch
            | TokenType::Match
            | TokenType::Select
            | TokenType::Is
            | TokenType::As
            | TokenType::New
            | TokenType::Go)
    }

    pub(super) fn should_break_after_match_arm_value(
        &self,
        current: &TokenType,
        next_token: Option<&Token>,
    ) -> bool {
        if !self.in_match_block() || !self.token_ends_expression(current) {
            return false;
        }

        let Some(next_token) = next_token else {
            return false;
        };

        if !self.can_start_match_arm(&next_token.token_type) {
            return false;
        }

        self.next_tokens_form_match_arm(self.current_index + 1)
    }

    fn can_start_match_arm(&self, token_type: &TokenType) -> bool {
        matches!(token_type,
            TokenType::Ident
            | TokenType::MacroIdent
            | TokenType::Label
            | TokenType::StringLiteral
            | TokenType::IntLiteral
            | TokenType::FloatLiteral
            | TokenType::True
            | TokenType::False
            | TokenType::Null
            | TokenType::LeftParen
            | TokenType::LeftSquare
            | TokenType::LeftCurly
            | TokenType::Minus
            | TokenType::Not)
    }

    fn next_tokens_form_match_arm(&self, start_index: usize) -> bool {
        let mut paren_depth = 0usize;
        let mut square_depth = 0usize;
        let mut curly_depth = 0usize;

        for token in self.tokens.iter().skip(start_index) {
            match token.token_type {
                TokenType::LeftParen => paren_depth += 1,
                TokenType::RightParen => {
                    if paren_depth == 0 {
                        return false;
                    }
                    paren_depth -= 1;
                }
                TokenType::LeftSquare => square_depth += 1,
                TokenType::RightSquare => {
                    if square_depth == 0 {
                        return false;
                    }
                    square_depth -= 1;
                }
                TokenType::LeftCurly => curly_depth += 1,
                TokenType::RightCurly => {
                    if paren_depth == 0 && square_depth == 0 && curly_depth == 0 {
                        return false;
                    }
                    curly_depth = curly_depth.saturating_sub(1);
                }
                TokenType::StmtEof if paren_depth == 0 && square_depth == 0 && curly_depth == 0 => {
                    return false;
                }
                TokenType::RightArrow if paren_depth == 0 && square_depth == 0 && curly_depth == 0 => {
                    return true;
                }
                _ => {}
            }
        }

        false
    }
}