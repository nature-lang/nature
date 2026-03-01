use crate::analyzer::common::AnalyzerError;
use crate::project::Module;
use ropey::Rope;
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use tower_lsp::lsp_types::Position;

pub fn offset_to_position(offset: usize, rope: &Rope) -> Option<Position> {
    let line = rope.try_char_to_line(offset).ok()?;
    let first_char_of_line = rope.try_line_to_char(line).ok()?;
    let column = offset - first_char_of_line;
    Some(Position::new(line as u32, column as u32))
}

pub fn position_to_offset(position: Position, rope: &Rope) -> Option<usize> {
    let line_char_offset = rope.try_line_to_char(position.line as usize).ok()?;
    let slice = rope.slice(0..line_char_offset + position.character as usize);
    Some(slice.len_bytes())
}

pub fn format_global_ident(prefix: String, ident: String) -> String {
    // 如果 prefix 为空，则直接返回 ident
    if prefix.is_empty() {
        return ident;
    }

    format!("{prefix}.{ident}")
}

pub fn format_impl_ident(impl_ident: String, key: String) -> String {
    format!("{impl_ident}.{key}")
}

pub fn format_generics_ident(ident: String, hash: u64) -> String {
    assert!(hash != 0, "hash must not be 0");

    if ident.contains('#') {
        return ident;
    }

    format!("{ident}#{}", hash)
}

pub fn calculate_hash<T: Hash>(t: &T) -> u64 {
    let mut hasher = DefaultHasher::new();
    t.hash(&mut hasher);
    hasher.finish()
}

pub fn align_up(n: u64, align: u64) -> u64 {
    if align == 0 {
        return n;
    }
    (n + align - 1) & !(align - 1)
}

pub fn errors_push(m: &mut Module, e: AnalyzerError) {
    // if m.index == 16 {
    //  panic!("TODO analyzer error");
    // }
    m.analyzer_errors.push(e);
}

// ---------------------------------------------------------------------------
// Pure helpers (also used by main.rs)
// ---------------------------------------------------------------------------

/// Returns true if the character can appear in an identifier.
pub fn is_ident_char(c: char) -> bool {
    c.is_alphanumeric() || c == '_'
}

/// Extract the word (identifier) at a given char offset in a `&str`.
/// Returns (word, start_offset, end_offset) or None if not on an identifier.
pub fn extract_word_at_offset(text: &str, offset: usize) -> Option<(String, usize, usize)> {
    let chars: Vec<char> = text.chars().collect();
    if offset > chars.len() {
        return None;
    }

    let mut start = offset;
    while start > 0 && is_ident_char(chars[start - 1]) {
        start -= 1;
    }

    let mut end = offset;
    while end < chars.len() && is_ident_char(chars[end]) {
        end += 1;
    }

    if start == end {
        return None;
    }

    let word: String = chars[start..end].iter().collect();
    Some((word, start, end))
}

/// Extract the word (identifier) at a given char offset directly from a `Rope`.
/// Avoids an O(n) `rope.to_string()` copy by operating on individual chars.
/// Returns (word, start_offset, end_offset) or None if not on an identifier.
pub fn extract_word_at_offset_rope(rope: &Rope, offset: usize) -> Option<(String, usize, usize)> {
    let len = rope.len_chars();
    if offset > len {
        return None;
    }

    let mut start = offset;
    while start > 0 {
        let ch = rope.char(start - 1);
        if !is_ident_char(ch) {
            break;
        }
        start -= 1;
    }

    let mut end = offset;
    while end < len {
        let ch = rope.char(end);
        if !is_ident_char(ch) {
            break;
        }
        end += 1;
    }

    if start == end {
        return None;
    }

    let word: String = rope.slice(start..end).chars().collect();
    Some((word, start, end))
}

/// Extract a symbol name from a diagnostic message like:
///   "symbol 'foo' not found"
///   "type 'Bar' not found"
///   "ident 'qux' undeclared"
pub fn extract_symbol_from_diagnostic(message: &str) -> Option<String> {
    if let Some(start) = message.find('\'') {
        if let Some(end) = message[start + 1..].find('\'') {
            let symbol = &message[start + 1..start + 1 + end];
            if !symbol.is_empty()
                && (message.contains("not found")
                    || message.contains("undeclared")
                    || message.contains("not defined"))
            {
                return Some(symbol.to_string());
            }
        }
    }
    None
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    // ── offset_to_position / position_to_offset ─────────────────────────

    #[test]
    fn offset_to_position_first_line() {
        let rope = Rope::from_str("hello world");
        assert_eq!(offset_to_position(0, &rope), Some(Position::new(0, 0)));
        assert_eq!(offset_to_position(5, &rope), Some(Position::new(0, 5)));
    }

    #[test]
    fn offset_to_position_multiline() {
        let rope = Rope::from_str("abc\ndef\nghi");
        assert_eq!(offset_to_position(0, &rope), Some(Position::new(0, 0)));
        assert_eq!(offset_to_position(3, &rope), Some(Position::new(0, 3))); // newline char
        assert_eq!(offset_to_position(4, &rope), Some(Position::new(1, 0))); // 'd'
        assert_eq!(offset_to_position(8, &rope), Some(Position::new(2, 0))); // 'g'
    }

    #[test]
    fn offset_to_position_unicode() {
        // '你' = 1 char, '好' = 1 char
        let rope = Rope::from_str("你好\n世界");
        assert_eq!(offset_to_position(0, &rope), Some(Position::new(0, 0)));
        assert_eq!(offset_to_position(2, &rope), Some(Position::new(0, 2))); // after '好'
        assert_eq!(offset_to_position(3, &rope), Some(Position::new(1, 0))); // '世'
    }

    #[test]
    fn offset_to_position_out_of_range() {
        let rope = Rope::from_str("ab");
        assert_eq!(offset_to_position(999, &rope), None);
    }

    // ── is_ident_char ───────────────────────────────────────────────────

    #[test]
    fn ident_chars() {
        assert!(is_ident_char('a'));
        assert!(is_ident_char('Z'));
        assert!(is_ident_char('0'));
        assert!(is_ident_char('_'));
        assert!(!is_ident_char(' '));
        assert!(!is_ident_char('.'));
        assert!(!is_ident_char('('));
        assert!(!is_ident_char('\n'));
    }

    // ── extract_word_at_offset (str version) ────────────────────────────

    #[test]
    fn word_at_offset_simple() {
        let text = "fn hello_world() {}";
        // cursor on 'h' (offset 3)
        let result = extract_word_at_offset(text, 3);
        assert_eq!(result, Some(("hello_world".to_string(), 3, 14)));
    }

    #[test]
    fn word_at_offset_beginning() {
        let text = "myVar = 42";
        let result = extract_word_at_offset(text, 0);
        assert_eq!(result, Some(("myVar".to_string(), 0, 5)));
    }

    #[test]
    fn word_at_offset_end() {
        let text = "var x = abc";
        let result = extract_word_at_offset(text, 10);
        assert_eq!(result, Some(("abc".to_string(), 8, 11)));
    }

    #[test]
    fn word_at_offset_on_space() {
        let text = "a b";
        // offset 1 is right after 'a', still resolves to 'a'
        assert_eq!(extract_word_at_offset(text, 1), Some(("a".to_string(), 0, 1)));
        // offset 2 is on 'b'
        assert_eq!(extract_word_at_offset(text, 2), Some(("b".to_string(), 2, 3)));
    }

    #[test]
    fn word_at_offset_out_of_range() {
        let text = "hi";
        assert_eq!(extract_word_at_offset(text, 100), None);
    }

    #[test]
    fn word_at_offset_empty() {
        assert_eq!(extract_word_at_offset("", 0), None);
    }

    #[test]
    fn word_at_offset_middle_of_word() {
        let text = "the_quick_brown_fox";
        let result = extract_word_at_offset(text, 8);
        assert_eq!(result, Some(("the_quick_brown_fox".to_string(), 0, 19)));
    }

    // ── extract_word_at_offset_rope ─────────────────────────────────────

    #[test]
    fn rope_word_matches_str_word() {
        let text = "fn hello_world() {}";
        let rope = Rope::from_str(text);
        let str_result = extract_word_at_offset(text, 5);
        let rope_result = extract_word_at_offset_rope(&rope, 5);
        assert_eq!(str_result, rope_result);
    }

    #[test]
    fn rope_word_at_offset_multiline() {
        let rope = Rope::from_str("var x = 10\nvar y = 20");
        // 'y' is at offset 15
        let result = extract_word_at_offset_rope(&rope, 15);
        assert_eq!(result, Some(("y".to_string(), 15, 16)));
    }

    #[test]
    fn rope_word_at_offset_on_space() {
        let rope = Rope::from_str("a b");
        // offset 1 is right after 'a', still resolves to 'a'
        assert_eq!(extract_word_at_offset_rope(&rope, 1), Some(("a".to_string(), 0, 1)));
        // offset 2 is on 'b'
        assert_eq!(extract_word_at_offset_rope(&rope, 2), Some(("b".to_string(), 2, 3)));
    }

    #[test]
    fn rope_word_at_offset_out_of_range() {
        let rope = Rope::from_str("hi");
        assert_eq!(extract_word_at_offset_rope(&rope, 100), None);
    }

    // ── extract_symbol_from_diagnostic ──────────────────────────────────

    #[test]
    fn diagnostic_symbol_not_found() {
        assert_eq!(
            extract_symbol_from_diagnostic("symbol 'foo' not found"),
            Some("foo".to_string())
        );
    }

    #[test]
    fn diagnostic_type_not_found() {
        assert_eq!(
            extract_symbol_from_diagnostic("type 'MyStruct' not found"),
            Some("MyStruct".to_string())
        );
    }

    #[test]
    fn diagnostic_undeclared() {
        assert_eq!(
            extract_symbol_from_diagnostic("ident 'bar' undeclared"),
            Some("bar".to_string())
        );
    }

    #[test]
    fn diagnostic_not_defined() {
        assert_eq!(
            extract_symbol_from_diagnostic("type 'Baz' not defined in module"),
            Some("Baz".to_string())
        );
    }

    #[test]
    fn diagnostic_no_match() {
        assert_eq!(
            extract_symbol_from_diagnostic("syntax error: unexpected token"),
            None
        );
    }

    #[test]
    fn diagnostic_empty_symbol() {
        assert_eq!(
            extract_symbol_from_diagnostic("symbol '' not found"),
            None
        );
    }

    // ── format_global_ident ─────────────────────────────────────────────

    #[test]
    fn global_ident_with_prefix() {
        assert_eq!(format_global_ident("pkg".to_string(), "func".to_string()), "pkg.func");
    }

    #[test]
    fn global_ident_empty_prefix() {
        assert_eq!(format_global_ident("".to_string(), "func".to_string()), "func");
    }
}
