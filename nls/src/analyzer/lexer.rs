use super::common::AnalyzerError;
use strum_macros::Display;
use tower_lsp::lsp_types::SemanticTokenType;

pub const LEGEND_TYPE: &[SemanticTokenType] = &[
    SemanticTokenType::FUNCTION,  // fn ident
    SemanticTokenType::VARIABLE,  // variable ident
    SemanticTokenType::STRING,    // string literal
    SemanticTokenType::COMMENT,   // comment
    SemanticTokenType::NUMBER,    // number literal
    SemanticTokenType::KEYWORD,   //  所有的语法关键字，比如 var, if, then, else, fn ...
    SemanticTokenType::OPERATOR,  // 运算符
    SemanticTokenType::PARAMETER, // function parameter ident
    SemanticTokenType::TYPE,      // 用于类型名称（如 int, float, string 等）
    SemanticTokenType::MACRO,     // 用于宏标识符
    SemanticTokenType::PROPERTY,  // struct property ident
    SemanticTokenType::NAMESPACE, // package ident
];

pub fn semantic_token_type_index(token_type: SemanticTokenType) -> usize {
    if token_type == SemanticTokenType::FUNCTION {
        return 0;
    }
    if token_type == SemanticTokenType::VARIABLE {
        return 1;
    }
    if token_type == SemanticTokenType::STRING {
        return 2;
    }
    if token_type == SemanticTokenType::COMMENT {
        return 3;
    }
    if token_type == SemanticTokenType::NUMBER {
        return 4;
    }
    if token_type == SemanticTokenType::KEYWORD {
        return 5;
    }
    if token_type == SemanticTokenType::OPERATOR {
        return 6;
    }
    if token_type == SemanticTokenType::PARAMETER {
        return 7;
    }
    if token_type == SemanticTokenType::TYPE {
        return 8;
    }
    if token_type == SemanticTokenType::MACRO {
        return 9;
    }
    if token_type == SemanticTokenType::PROPERTY {
        return 10;
    }
    if token_type == SemanticTokenType::NAMESPACE {
        return 11;
    }

    panic!("unknown semantic token type: {:?}", token_type)
}

#[derive(Debug, Clone, PartialEq, Display)]
pub enum TokenType {
    #[strum(serialize = "unknown")]
    Unknown = 0,
    // 单字符标记
    #[strum(serialize = "(")]
    LeftParen,
    #[strum(serialize = ")")]
    RightParen,
    #[strum(serialize = "[")]
    LeftSquare,
    #[strum(serialize = "]")]
    RightSquare,
    #[strum(serialize = "{")]
    LeftCurly,
    #[strum(serialize = "}")]
    RightCurly,
    #[strum(serialize = "<")]
    LeftAngle,
    #[strum(serialize = "<")]
    LessThan,
    #[strum(serialize = ">")]
    RightAngle,

    #[strum(serialize = ",")]
    Comma,
    #[strum(serialize = ".")]
    Dot,
    #[strum(serialize = "-")]
    Minus,
    #[strum(serialize = "+")]
    Plus,
    #[strum(serialize = "...")]
    Ellipsis,
    #[strum(serialize = "..")]
    Range,
    #[strum(serialize = ":")]
    Colon,
    #[strum(serialize = ";")]
    Semicolon,
    #[strum(serialize = "/")]
    Slash,
    #[strum(serialize = "*")]
    Star,
    #[strum(serialize = "*")]
    ImportStar,
    #[strum(serialize = "%")]
    Percent,
    #[strum(serialize = "?")]
    Question,
    #[strum(serialize = "->")]
    RightArrow,

    // 一到两个字符的标记
    #[strum(serialize = "!")]
    Not,
    #[strum(serialize = "!=")]
    NotEqual,
    #[strum(serialize = "=")]
    Equal,

    #[strum(serialize = "==")]
    EqualEqual,

    #[strum(serialize = ">=")]
    GreaterEqual,
    #[strum(serialize = "<=")]
    LessEqual,
    #[strum(serialize = "&&")]
    AndAnd,
    #[strum(serialize = "||")]
    OrOr,

    // 复合赋值运算符
    #[strum(serialize = "+=")]
    PlusEqual,
    #[strum(serialize = "-=")]
    MinusEqual,
    #[strum(serialize = "*=")]
    StarEqual,
    #[strum(serialize = "/=")]
    SlashEqual,
    #[strum(serialize = "%=")]
    PercentEqual,
    #[strum(serialize = "&=")]
    AndEqual,
    #[strum(serialize = "|=")]
    OrEqual,
    #[strum(serialize = "^=")]
    XorEqual,
    #[strum(serialize = "<<=")]
    LeftShiftEqual,
    #[strum(serialize = ">>=")]
    RightShiftEqual,

    // 位运算
    #[strum(serialize = "~")]
    Tilde,
    #[strum(serialize = "&")]
    And,
    #[strum(serialize = "|")]
    Or,
    #[strum(serialize = "^")]
    Xor,
    #[strum(serialize = "<<")]
    LeftShift,
    #[strum(serialize = ">>")]
    RightShift,

    // 字面量
    #[strum(serialize = "ident_literal")]
    Ident,
    #[strum(serialize = "#")]
    Pound,
    #[strum(serialize = "macro_ident")]
    MacroIdent,
    #[strum(serialize = "label")]
    Label,
    #[strum(serialize = "string_literal")]
    StringLiteral,
    #[strum(serialize = "float_literal")]
    FloatLiteral,
    #[strum(serialize = "int_literal")]
    IntLiteral,

    // 类型
    #[strum(serialize = "string")]
    String,
    #[strum(serialize = "bool")]
    Bool,
    #[strum(serialize = "float")]
    Float,
    #[strum(serialize = "int")]
    Int,
    #[strum(serialize = "uint")]
    Uint,
    #[strum(serialize = "u8")]
    U8,
    #[strum(serialize = "u16")]
    U16,
    #[strum(serialize = "u32")]
    U32,
    #[strum(serialize = "u64")]
    U64,
    #[strum(serialize = "i8")]
    I8,
    #[strum(serialize = "i16")]
    I16,
    #[strum(serialize = "i32")]
    I32,
    #[strum(serialize = "i64")]
    I64,
    #[strum(serialize = "f32")]
    F32,
    #[strum(serialize = "f64")]
    F64,
    #[strum(serialize = "new")]
    New,

    // 内置复合类型
    #[strum(serialize = "arr")]
    Arr,
    #[strum(serialize = "vec")]
    Vec,
    #[strum(serialize = "map")]
    Map,
    #[strum(serialize = "tup")]
    Tup,
    #[strum(serialize = "set")]
    Set,
    #[strum(serialize = "chan")]
    Chan,

    // 关键字
    #[strum(serialize = "ptr")]
    Ptr,
    #[strum(serialize = "true")]
    True,
    #[strum(serialize = "false")]
    False,
    #[strum(serialize = "type")]
    Type,
    #[strum(serialize = "null")]
    Null,
    #[strum(serialize = "void")]
    Void,

    #[strum(serialize = "any")]
    Any,
    #[strum(serialize = "struct")]
    Struct,
    #[strum(serialize = "throw")]
    Throw,
    #[strum(serialize = "try")]
    Try,
    #[strum(serialize = "catch")]
    Catch,
    #[strum(serialize = "match")]
    Match,
    #[strum(serialize = "select")]
    Select,
    #[strum(serialize = "continue")]
    Continue,
    #[strum(serialize = "break")]
    Break,
    #[strum(serialize = "for")]
    For,
    #[strum(serialize = "in")]
    In,
    #[strum(serialize = "if")]
    If,
    #[strum(serialize = "else")]
    Else,
    #[strum(serialize = "else if")]
    ElseIf,
    #[strum(serialize = "var")]
    Var,

    #[strum(serialize = "const")]
    Const,

    #[strum(serialize = "let")]
    Let,
    #[strum(serialize = "is")]
    Is,
    #[strum(serialize = "sizeof")]
    Sizeof,
    #[strum(serialize = "reflect_hash")]
    ReflectHash,
    #[strum(serialize = "as")]
    As,
    #[strum(serialize = "fn")]
    Fn,
    #[strum(serialize = "import")]
    Import,

    #[strum(serialize = "return")]
    Return,

    #[strum(serialize = "interface")]
    Interface,

    #[strum(serialize = "go")]
    Go,

    #[strum(serialize = ";")]
    StmtEof,
    // #[strum(serialize = "\0")]
    #[strum(serialize = "line_comment")]
    LineComment,

    #[strum(serialize = "block_comment")]
    BlockComment,

    #[strum(serialize = "whitespace")]
    Whitespace,

    #[strum(serialize = "EOF")]
    Eof,
}

#[derive(Debug, Clone)]
pub enum LeftAngleType {
    FnArgs,
    TypeArgs,
    LogicLt,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub token_type: TokenType,
    pub semantic_token_type: usize,
    pub literal: String,
    pub line: usize,
    pub start: usize, // start index
    pub end: usize,   // end index
    pub length: usize,
}

impl Token {
    pub fn new(token_type: TokenType, literal: String, start: usize, end: usize, line: usize) -> Self {
        let mut length = literal.chars().count(); // vscode lsp 使用 utf16 编码 长度

        // string 的 length + 2  携带上 字符串的开始和结束符号
        if token_type == TokenType::StringLiteral {
            length += 2;
        }

        if token_type == TokenType::MacroIdent {
            length += 1;
        }

        let semantic_token_type = Self::get_semantic_token_type(&token_type);
        let semantic_token_type = semantic_token_type_index(semantic_token_type);

        Self {
            token_type,
            semantic_token_type,
            literal,
            line,
            start,
            end,
            length,
        }
    }

    fn get_semantic_token_type(token_type: &TokenType) -> SemanticTokenType {
        match token_type {
            TokenType::StringLiteral => SemanticTokenType::STRING,
            TokenType::IntLiteral | TokenType::FloatLiteral => SemanticTokenType::NUMBER,
            TokenType::LineComment => SemanticTokenType::COMMENT,
            TokenType::BlockComment => SemanticTokenType::COMMENT,
            TokenType::MacroIdent => SemanticTokenType::MACRO,
            TokenType::Label => SemanticTokenType::PROPERTY,
            // 所有类型相关的token
            TokenType::String
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
            | TokenType::Arr
            | TokenType::Vec
            | TokenType::Map
            | TokenType::Tup
            | TokenType::Set
            | TokenType::Chan
            | TokenType::Ptr => SemanticTokenType::TYPE,
            // 所有关键字
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
            | TokenType::Fn
            | TokenType::Import
            | TokenType::True
            | TokenType::False
            | TokenType::Null
            | TokenType::Type
            | TokenType::Struct
            | TokenType::Throw
            | TokenType::Try
            | TokenType::Catch
            | TokenType::Match
            | TokenType::Select
            | TokenType::Is
            | TokenType::As
            | TokenType::New
            | TokenType::Go => SemanticTokenType::KEYWORD,
            // 默认情况下标识符被视为变量
            TokenType::Ident => SemanticTokenType::VARIABLE,
            // 其他所有操作符
            _ => SemanticTokenType::OPERATOR,
        }
    }

    pub fn is_complex_assign(&self) -> bool {
        matches!(
            self.token_type,
            TokenType::PercentEqual
                | TokenType::MinusEqual
                | TokenType::PlusEqual
                | TokenType::SlashEqual
                | TokenType::StarEqual
                | TokenType::OrEqual
                | TokenType::AndEqual
                | TokenType::XorEqual
                | TokenType::LeftShiftEqual
                | TokenType::RightShiftEqual
        )
    }

    pub fn debug(&self) -> String {
        format!(
            "Token {{ type: {:?}, literal: '{}', start: {}, end: {}, length: {} }}",
            self.token_type, self.literal, self.start, self.end, self.length
        )
    }
}

#[derive(Debug)]
pub struct Lexer {
    source: Vec<char>,
    offset: usize,
    guard: usize, // char
    length: usize,
    line: usize,
    errors: Vec<AnalyzerError>,
    token_db: Vec<Token>,       // 所有的 token 都注册在这里,
    syntax_indexes: Vec<usize>, //  存储 tokens 索引
}

impl Lexer {
    pub fn new(source_string: String) -> Self {
        let source_chars = source_string.chars().collect::<Vec<char>>();
        Lexer {
            offset: 0,
            guard: 0,
            source: source_chars,
            length: 0,
            line: 1,
            errors: Vec::new(),
            token_db: Vec::new(),
            syntax_indexes: Vec::new(),
        }
    }

    fn push(&mut self, t: Token) {
        let index = self.token_db.len();
        self.token_db.push(t);
        self.syntax_indexes.push(index);
    }

    fn insert(&mut self, index: usize, t: Token) {
        self.token_db.insert(index, t); // 在指定索引后插入
        self.syntax_indexes.push(index);
    }

    pub fn scan(&mut self) -> (Vec<Token>, Vec<usize>, Vec<AnalyzerError>) {
        while !self.at_eof() {
            let line = self.line;
            self.skip_space();
            if self.at_eof() {
                break;
            }

            if line != self.line && !self.syntax_indexes.is_empty() {
                let last_index = *self.syntax_indexes.last().unwrap();
                let prev_token = &self.token_db[last_index];

                // has newline 后如果上一个字符可以接受语句结束符， 则在上一个字符的后面插入语句结束符(不用考虑会 影响 white_token 和 comment token, 这些 token 不会进入到 sytax_indexes 中)
                // white_token 和 comment_token 在 token_db 中延顺后移即可
                if self.need_stmt_end(prev_token) {
                    self.insert(
                        last_index + 1,
                        Token::new(TokenType::StmtEof, ";".to_string(), prev_token.end, prev_token.end + 1, prev_token.line),
                    );
                }
            }

            let next_token = self.item();
            self.push(next_token);
        }

        self.offset = self.guard;
        self.push(Token::new(TokenType::Eof, "EOF".to_string(), self.offset, self.offset + 1, self.line));

        (self.token_db.clone(), self.syntax_indexes.clone(), self.errors.clone())
    }

    fn ident_advance(&mut self) -> String {
        while !self.at_eof() && (self.is_alpha(self.peek_guard()) || self.is_number(self.peek_guard())) {
            self.guard_advance();
        }
        self.gen_word()
    }

    fn ident(&self, word: &str, _: usize) -> TokenType {
        match word {
            "as" => TokenType::As,
            "any" => TokenType::Any,
            "arr" => TokenType::Arr,
            "bool" => TokenType::Bool,
            "break" => TokenType::Break,
            "catch" => TokenType::Catch,
            "chan" => TokenType::Chan,
            "continue" => TokenType::Continue,
            "else" => TokenType::Else,
            "false" => TokenType::False,
            "float" => TokenType::Float,
            "fn" => TokenType::Fn,
            "for" => TokenType::For,
            "go" => TokenType::Go,
            "if" => TokenType::If,
            "import" => TokenType::Import,
            "in" => TokenType::In,
            "int" => TokenType::Int,
            "is" => TokenType::Is,
            "let" => TokenType::Let,
            "map" => TokenType::Map,
            "match" => TokenType::Match,
            "select" => TokenType::Select,
            // "new" => TokenType::New, // new 可用于关键字
            "null" => TokenType::Null,
            "ptr" => TokenType::Ptr,
            "return" => TokenType::Return,
            "set" => TokenType::Set,
            "string" => TokenType::String,
            "struct" => TokenType::Struct,
            "throw" => TokenType::Throw,
            "true" => TokenType::True,
            "try" => TokenType::Try,
            "tup" => TokenType::Tup,
            "type" => TokenType::Type,
            "uint" => TokenType::Uint,
            "var" => TokenType::Var,
            "const" => TokenType::Const,
            "vec" => TokenType::Vec,
            "void" => TokenType::Void,
            "interface" => TokenType::Interface,
            // 数字类型
            "f32" => TokenType::F32,
            "f64" => TokenType::F64,
            "i8" => TokenType::I8,
            "i16" => TokenType::I16,
            "i32" => TokenType::I32,
            "i64" => TokenType::I64,
            "u8" => TokenType::U8,
            "u16" => TokenType::U16,
            "u32" => TokenType::U32,
            "u64" => TokenType::U64,
            _ => TokenType::Ident,
        }
    }

    fn skip_space(&mut self) {
        while !self.at_eof() {
            match self.peek_guard() {
                ' ' | '\r' | '\t' => {
                    self.guard_advance();
                }
                '\n' => {
                    self.guard_advance();
                }
                '/' => {
                    if let Some(next_char) = self.peek_next() {
                        if next_char == '/' {
                            let comment_start = self.guard;
                            let comment_line = self.line; // advance 如果遇到 \n, self.line 会自动加 1

                            // 跳过 //
                            self.guard_advance();
                            self.guard_advance();

                            // 收集注释内容
                            while !self.at_eof() && self.peek_guard() != '\n' {
                                self.guard_advance();
                            }

                            // 生成注释 token (utf8 编码)
                            let comment: String = self.source[comment_start..self.guard].iter().collect();

                            let token = Token::new(TokenType::LineComment, comment, comment_start, self.guard, comment_line);
                            self.token_db.push(token);
                        } else if next_char == '*' {
                            let mut line_start = self.guard;
                            let mut current_line = self.line;

                            while !self.block_comment_end() {
                                if self.at_eof() {
                                    // 直到完美结尾都没有找到注释闭合符号, 不需要做任何错误恢复，已经到达了文件的末尾
                                    self.errors.push(AnalyzerError {
                                        start: self.offset,
                                        end: self.guard,
                                        message: String::from("Unterminated comment"),
                                    });
                                    return; // 直接返回，避免 advance 溢出
                                }

                                // 当遇到换行符时，生成当前行的注释token
                                if self.peek_guard() == '\n' {
                                    let line_content: String = self.source[line_start..self.guard].iter().collect();
                                    self.token_db
                                        .push(Token::new(TokenType::BlockComment, line_content, line_start, self.guard, current_line));

                                    self.guard_advance(); // 跳过换行符
                                    line_start = self.guard; // 更新下一行的起始位置
                                    current_line = self.line; // 更新当前行号
                                } else {
                                    self.guard_advance();
                                }
                            }

                            // 处理最后一行（包含 */）
                            if line_start < self.guard + 2 {
                                self.guard_advance(); // *
                                self.guard_advance(); // /

                                let line_content: String = self.source[line_start..self.guard].iter().collect();
                                self.token_db
                                    .push(Token::new(TokenType::BlockComment, line_content, line_start, self.guard, current_line));
                            }
                        } else {
                            // 非空白字符或者注释，直接返回
                            return;
                        }
                    }
                }
                _ => {
                    return;
                }
            }
        }
    }

    fn gen_word(&self) -> String {
        self.source[self.offset..self.guard].iter().collect()
    }

    fn is_string(&self, s: char) -> bool {
        matches!(s, '"' | '`' | '\'')
    }

    fn is_float(&mut self, word: &str) -> bool {
        let dot_count = word.chars().filter(|&c| c == '.').count();
        let has_e = word.chars().any(|c| c == 'e' || c == 'E');

        // 结尾不能是 .
        if word.ends_with('.') {
            self.errors.push(AnalyzerError {
                start: self.offset,
                end: self.guard,
                message: String::from("floating-point numbers cannot end with '.'"),
            });
            return false;
        }

        // 如果有科学计数法标记，则认为是浮点数
        if has_e {
            return true;
        }

        if dot_count == 0 {
            return false;
        }

        if dot_count > 1 {
            self.errors.push(AnalyzerError {
                start: self.offset,
                end: self.guard,
                message: String::from("floating-point number contains multiple '.'"),
            });
            return false;
        }

        true
    }

    fn is_alpha(&self, c: char) -> bool {
        c.is_ascii_alphabetic() || c == '_'
    }

    fn is_number(&self, c: char) -> bool {
        c.is_ascii_digit()
    }

    fn is_hex_number(&self, c: char) -> bool {
        c.is_ascii_hexdigit()
    }

    fn is_oct_number(&self, c: char) -> bool {
        ('0'..='7').contains(&c)
    }

    fn is_bin_number(&self, c: char) -> bool {
        c == '0' || c == '1'
    }

    fn at_eof(&self) -> bool {
        self.guard >= self.source.len()
    }

    fn guard_advance(&mut self) -> char {
        let c = self.peek_guard();
        if c == '\n' {
            self.line += 1;
        }

        // guard == source.len() 表示已经到达文件 末尾 EOF, 此时不能继续 guard + 1
        if self.guard >= self.source.len() {
            panic!("cannot advance, guard index {} exceeds source length {}", self.guard, self.source.len());
        }

        // 返回 utf8 char 实际占用的字符数量
        self.guard += 1;
        self.length += 1;

        c // c 实现了 copy， 所以这里不会发生所有权转移，顶多就是 clone
    }

    fn match_char(&mut self, expected: char) -> bool {
        if self.at_eof() {
            return false;
        }

        if self.peek_guard() != expected {
            return false;
        }

        self.guard_advance();
        true
    }

    fn item(&mut self) -> Token {
        // reset by guard
        self.offset = self.guard;
        self.length = 0;

        // 检查标识符
        if self.is_alpha(self.peek_guard()) {
            let word = self.ident_advance();
            let token = Token::new(self.ident(&word, self.length), word, self.offset, self.guard, self.line);
            return token;
        }

        // 检查宏标识符
        if self.match_char('@') {
            let word = self.ident_advance();
            let word = word[1..].to_string(); // 跳过 @ 字符
            return Token::new(TokenType::MacroIdent, word, self.offset, self.guard, self.line);
        }

        // 检查函数标签
        if self.match_char('#') {
            let word = self.ident_advance();
            let word = word[1..].to_string(); // 跳过 # 字符
            return Token::new(TokenType::Label, word, self.offset, self.guard, self.line);
        }

        // 检查数字
        if self.is_number(self.peek_guard()) {
            let mut word: String;

            let mut may_be_float = false;

            // 处理 0 开头的特殊数字格式
            if self.peek_guard() == '0' {
                if let Some(next_char) = self.peek_next() {
                    match next_char {
                        'x' | 'X' => {
                            word = self.hex_number_advance();
                            word = format!("0x{}", &word[2..]);
                        }
                        'o' | 'O' => {
                            let num = self.oct_number_advance();
                            // let decimal = self.number_convert(&num, 8);
                            // word = decimal.to_string();
                            word = num;
                            word = format!("0o{}", &word[2..]);
                        }
                        'b' | 'B' => {
                            let num = self.bin_number_advance();
                            // let decimal = self.number_convert(&num, 2);
                            // word = decimal.to_string();
                            word = num;
                            word = format!("0b{}", &word[2..]);
                        }
                        _ => {
                            word = self.number_advance();
                            may_be_float = true;
                        }
                    }
                } else {
                    word = self.number_advance();
                    may_be_float = true;
                }
            } else {
                word = self.number_advance();
                may_be_float = true;
            }

            // 判断数字类型
            let token_type = if may_be_float && self.is_float(&word) {
                TokenType::FloatLiteral
            } else {
                TokenType::IntLiteral
            };

            return Token::new(token_type, word, self.offset, self.guard, self.line);
        }

        // 检查字符串
        if self.is_string(self.peek_guard()) {
            let str = self.string_advance(self.peek_guard());
            return Token::new(TokenType::StringLiteral, str, self.offset, self.guard, self.line);
        }

        // 处理特殊字符
        let special_type = self.special_char();
        assert!(special_type != TokenType::Eof, "special characters are not recognized");

        // 检查 import xxx as * 的特殊情况
        if special_type == TokenType::Star && !self.syntax_indexes.is_empty() {
            let prev_token = &self.token_db[*self.syntax_indexes.last().unwrap()];
            if prev_token.token_type == TokenType::As {
                return Token::new(TokenType::ImportStar, self.gen_word(), self.offset, self.guard, self.line);
            }
        }

        Token::new(special_type, self.gen_word(), self.offset, self.guard, self.line)
    }

    fn block_comment_end(&self) -> bool {
        if self.guard + 1 >= self.source.len() {
            return false;
        }

        self.source[self.guard] == '*' && self.source[self.guard + 1] == '/'
    }

    fn special_char(&mut self) -> TokenType {
        let c = self.guard_advance();
        match c {
            '(' => TokenType::LeftParen,
            ')' => TokenType::RightParen,
            '[' => TokenType::LeftSquare,
            ']' => TokenType::RightSquare,
            '{' => TokenType::LeftCurly,
            '}' => TokenType::RightCurly,
            ':' => TokenType::Colon,
            ';' => TokenType::StmtEof,
            ',' => TokenType::Comma,
            '?' => TokenType::Question,
            '%' => {
                if self.match_char('=') {
                    TokenType::PercentEqual
                } else {
                    TokenType::Percent
                }
            }
            '-' => {
                if self.match_char('=') {
                    TokenType::MinusEqual
                } else if self.match_char('>') {
                    TokenType::RightArrow
                } else {
                    TokenType::Minus
                }
            }
            '+' => {
                if self.match_char('=') {
                    TokenType::PlusEqual
                } else {
                    TokenType::Plus
                }
            }
            '/' => {
                if self.match_char('=') {
                    TokenType::SlashEqual
                } else {
                    TokenType::Slash
                }
            }
            '*' => {
                if self.match_char('=') {
                    TokenType::StarEqual
                } else {
                    TokenType::Star
                }
            }
            '.' => {
                if self.match_char('.') {
                    if self.match_char('.') {
                        return TokenType::Ellipsis;
                    } else {
                        return TokenType::Range;
                    }
                } else {
                    TokenType::Dot
                }
            }
            '!' => {
                if self.match_char('=') {
                    TokenType::NotEqual
                } else {
                    TokenType::Not
                }
            }
            '=' => {
                if self.match_char('=') {
                    TokenType::EqualEqual
                } else {
                    TokenType::Equal
                }
            }
            '<' => {
                if self.match_char('<') {
                    if self.match_char('=') {
                        TokenType::LeftShiftEqual
                    } else {
                        TokenType::LeftShift
                    }
                } else if self.match_char('=') {
                    TokenType::LessEqual
                } else {
                    TokenType::LeftAngle
                }
            }
            '>' => {
                if self.match_char('=') {
                    return TokenType::GreaterEqual;
                }

                if self.peek_guard_optional() == Some('>') && self.peek_next() == Some('=') {
                    self.guard_advance();
                    self.guard_advance();
                    return TokenType::RightShiftEqual;
                }

                return TokenType::RightAngle;
            }
            '&' => {
                if self.match_char('&') {
                    TokenType::AndAnd
                } else if self.match_char('=') {
                    TokenType::AndEqual
                } else {
                    TokenType::And
                }
            }
            '|' => {
                if self.match_char('|') {
                    TokenType::OrOr
                } else if self.match_char('=') {
                    TokenType::OrEqual
                } else {
                    TokenType::Or
                }
            }
            '~' => TokenType::Tilde,
            '^' => {
                if self.match_char('=') {
                    TokenType::XorEqual
                } else {
                    TokenType::Xor
                }
            }
            _ => {
                self.errors.push(AnalyzerError {
                    start: self.offset,
                    end: self.guard,
                    message: String::from("Unexpected character"),
                });
                TokenType::Unknown
            }
        }
    }

    fn string_advance(&mut self, close_char: char) -> String {
        // 添加这两个变量定义（参考块注释的处理方式）
        let mut line_start = self.guard;
        let mut current_line = self.line;

        // 跳过开始的 " ' `
        self.guard_advance();
        let mut result = String::new();

        let is_raw_string = close_char == '`';

        // 结束判断
        if self.at_eof() {
            self.errors.push(AnalyzerError {
                start: self.offset,
                end: self.guard,
                message: String::from("string not terminated"),
            });
            return result;
        }

        let escape_char = '\\';

        while self.peek_guard() != close_char {
            let mut guard_char = self.peek_guard(); // utf8 char

            if guard_char == '\n' {
                if is_raw_string {
                    // 为当前行生成字符串token
                    let line_content: String = self.source[line_start..self.guard].iter().collect();

                    self.token_db
                        .push(Token::new(TokenType::StringLiteral, line_content, line_start, self.guard, current_line));

                    line_start = self.guard + 1; // 跳过 /n
                    current_line = self.line + 1; // 跳过 /n
                } else {
                    self.errors.push(AnalyzerError {
                        start: self.offset,
                        end: self.guard,
                        message: String::from("string not terminated"),
                    });
                    return result; // 返回已经解析的字符串
                }
            }

            // 处理转义字符
            if guard_char == escape_char && !is_raw_string {
                self.guard_advance();

                guard_char = self.peek_guard();

                // 将转义字符转换为实际字符
                guard_char = match guard_char {
                    'n' => '\n',
                    't' => '\t',
                    'r' => '\r',
                    'b' => '\x08',
                    'f' => '\x0C',
                    'a' => '\x07',
                    'v' => '\x0B',
                    '0' => '\0',
                    '\\' | '\'' | '"' => guard_char,
                    'x' => {
                        if self.guard + 2 >= self.source.len() {
                            self.errors.push(AnalyzerError {
                                start: self.offset,
                                end: self.guard + 1,
                                message: String::from("incomplete hex escape sequence"),
                            });
                            guard_char
                        } else {
                            let hex_chars: String = self.source[self.guard + 1..self.guard + 3].iter().collect();

                            if hex_chars.chars().all(|c| self.is_hex_number(c)) {
                                match u8::from_str_radix(&hex_chars, 16) {
                                    Ok(value) => {
                                        self.guard += 2; // 跳过两个十六进制字符
                                        value as char
                                    }
                                    Err(_) => {
                                        self.errors.push(AnalyzerError {
                                            start: self.offset,
                                            end: self.guard + 3,
                                            message: format!("invalid hex escape sequence \\x{}", hex_chars),
                                        });
                                        guard_char
                                    }
                                }
                            } else {
                                self.errors.push(AnalyzerError {
                                    start: self.offset,
                                    end: self.guard + 3,
                                    message: format!("invalid hex escape sequence \\x{}", hex_chars),
                                });
                                guard_char
                            }
                        }
                    }
                    _ => {
                        self.errors.push(AnalyzerError {
                            start: self.offset,
                            end: self.guard + 1,
                            message: format!("unknown escape char '{}'", guard_char),
                        });
                        guard_char
                    }
                };
            }

            result.push(guard_char);
            self.guard_advance();

            // 结束判断
            if self.at_eof() {
                self.errors.push(AnalyzerError {
                    start: self.offset,
                    end: self.guard,
                    message: String::from("string not terminated"),
                });
                return result;
            }
        }

        // 处理最后一行（如果是原始字符串）
        if is_raw_string && line_start < self.guard {
            let line_content: String = self.source[line_start..self.guard].iter().collect();
            self.token_db
                .push(Token::new(TokenType::StringLiteral, line_content, line_start, self.guard, current_line));
        }

        // must close char, 跳过结束引号，但不计入 token 长度
        self.guard_advance();

        result
    }

    fn need_stmt_end(&self, prev_token: &Token) -> bool {
        matches!(
            prev_token.token_type,
            TokenType::ImportStar
                | TokenType::IntLiteral
                | TokenType::StringLiteral
                | TokenType::FloatLiteral
                | TokenType::Ident
                | TokenType::Break
                | TokenType::Continue
                | TokenType::Return
                | TokenType::True
                | TokenType::False
                | TokenType::RightParen
                | TokenType::RightSquare
                | TokenType::RightCurly
                | TokenType::RightAngle
                | TokenType::Bool
                | TokenType::Float
                | TokenType::F32
                | TokenType::F64
                | TokenType::Int
                | TokenType::I8
                | TokenType::I16
                | TokenType::I32
                | TokenType::I64
                | TokenType::Uint
                | TokenType::U8
                | TokenType::U16
                | TokenType::U32
                | TokenType::U64
                | TokenType::String
                | TokenType::Void
                | TokenType::Null
                | TokenType::Not
                | TokenType::Question
                | TokenType::Label
        )
    }

    // 添加缺失的数字处理函数
    fn hex_number_advance(&mut self) -> String {
        // 跳过开始的 0x
        self.guard_advance();
        self.guard_advance();

        while !self.at_eof() && self.is_hex_number(self.peek_guard()) {
            self.guard_advance();
        }

        self.gen_word()
    }

    fn oct_number_advance(&mut self) -> String {
        // 跳过开始的 0o
        self.guard_advance();
        self.guard_advance();

        while !self.at_eof() && self.is_oct_number(self.peek_guard()) {
            self.guard_advance();
        }
        self.gen_word()
    }

    fn bin_number_advance(&mut self) -> String {
        // 跳过开始的 0b
        self.guard_advance();
        self.guard_advance();

        while !self.at_eof() && self.is_bin_number(self.peek_guard()) {
            self.guard_advance();
        }
        self.gen_word()
    }

    fn number_advance(&mut self) -> String {
        // 处理整数部分
        while !self.at_eof() && self.is_number(self.peek_guard()) {
            self.guard_advance();
        }

        // 处理小数点部分
        if !self.at_eof() && self.peek_guard() == '.' && self.peek_next().map_or(false, |c| self.is_number(c)) {
            self.guard_advance(); // 跳过小数点

            // 处理小数点后的数字
            while !self.at_eof() && self.is_number(self.peek_guard()) {
                self.guard_advance();
            }
        }

        // 处理科学计数法部分
        if !self.at_eof() && (self.peek_guard() == 'e' || self.peek_guard() == 'E') {
            let has_exponent =
                // 下一个字符是数字
                self.peek_next().map_or(false, |c| self.is_number(c)) ||
                    // 或者下一个字符是+/-，且再下一个字符是数字
                    (self.peek_next().map_or(false, |c| c == '+' || c == '-') &&
                        self.source.get(self.guard + 2).map_or(false, |&c| self.is_number(c)));

            if has_exponent {
                self.guard_advance(); // 跳过 'e' 或 'E'

                // 处理可能的正负号
                if !self.at_eof() && (self.peek_guard() == '+' || self.peek_guard() == '-') {
                    self.guard_advance();
                }

                // 处理指数部分的数字
                while !self.at_eof() && self.is_number(self.peek_guard()) {
                    self.guard_advance();
                }
            }
        }

        self.gen_word()
    }

    fn peek_guard(&self) -> char {
        if self.guard >= self.source.len() {
            panic!("unexpected end of file: guard index {} exceeds source length {}", self.guard, self.source.len());
        }

        // self.source[self.guard]
        self.source.get(self.guard).copied().unwrap()
    }

    fn peek_guard_optional(&self) -> Option<char> {
        self.source.get(self.guard).copied()
    }

    fn peek_next(&self) -> Option<char> {
        self.source.get(self.guard + 1).copied()
    }

    pub fn debug_tokens(tokens: &[Token]) {
        println!("=== Tokens Debug ===");
        for (i, token) in tokens.iter().enumerate() {
            println!("[{}] {}", i, token.debug());
        }
        println!("=== End Tokens ===");
    }
}
