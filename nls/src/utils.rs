//! Position / offset conversion and identifier extraction helpers.
//!
//! All functions in this module are **pure** — they operate on a [`Rope`] or
//! `&str` and carry no other state, making them trivial to unit-test.

use crate::analyzer::common::AnalyzerError;
use ropey::Rope;
use tower_lsp::lsp_types::Position;

// ─── Offset ↔ Position ─────────────────────────────────────────────────────────

/// Convert a char offset in a rope to an LSP `Position` (0-based line/col).
///
/// Returns `None` if the offset is out of range.
pub fn offset_to_position(offset: usize, rope: &Rope) -> Option<Position> {
    let line = rope.try_char_to_line(offset).ok()?;
    let line_start = rope.try_line_to_char(line).ok()?;
    let column = offset - line_start;
    Some(Position::new(line as u32, column as u32))
}

/// Convert an LSP `Position` to a *byte* offset in the rope.
///
/// This matches the old behaviour used by the analyzer: the result is a byte
/// offset suitable for slicing into `source: String`.
pub fn position_to_byte_offset(position: Position, rope: &Rope) -> Option<usize> {
    let line_char = rope.try_line_to_char(position.line as usize).ok()?;
    let slice = rope.slice(0..line_char + position.character as usize);
    Some(slice.len_bytes())
}

/// Convert an LSP `Position` to a *char* offset in the rope.
///
/// Returns `None` if the position is out of range.
pub fn position_to_char_offset(position: Position, rope: &Rope) -> Option<usize> {
    let line_start = rope.try_line_to_char(position.line as usize).ok()?;
    let offset = line_start + position.character as usize;
    if offset > rope.len_chars() {
        return None;
    }
    Some(offset)
}

// ─── Identifier extraction ──────────────────────────────────────────────────────

/// Returns `true` if `c` can appear in a Nature identifier.
pub fn is_ident_char(c: char) -> bool {
    c.is_alphanumeric() || c == '_'
}

/// Extract the word (identifier) at `offset` in a `&str`.
///
/// Returns `(word, start, end)` or `None` when the cursor isn't on an ident.
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

/// Like [`extract_word_at_offset`] but operates directly on a [`Rope`],
/// avoiding an O(n) `to_string()` copy.
pub fn extract_word_at_offset_rope(rope: &Rope, offset: usize) -> Option<(String, usize, usize)> {
    let len = rope.len_chars();
    if offset > len {
        return None;
    }

    let mut start = offset;
    while start > 0 && is_ident_char(rope.char(start - 1)) {
        start -= 1;
    }

    let mut end = offset;
    while end < len && is_ident_char(rope.char(end)) {
        end += 1;
    }

    if start == end {
        return None;
    }

    let word: String = rope.slice(start..end).chars().collect();
    Some((word, start, end))
}

// ─── Diagnostic helpers ─────────────────────────────────────────────────────────

/// Extract a symbol name from diagnostic messages like:
///   `"symbol 'foo' not found"`
///   `"type 'Bar' not found"`
///   `"ident 'qux' undeclared"`
pub fn extract_symbol_from_diagnostic(message: &str) -> Option<String> {
    let start = message.find('\'')?;
    let end = message[start + 1..].find('\'')?;
    let symbol = &message[start + 1..start + 1 + end];
    if !symbol.is_empty()
        && (message.contains("not found")
            || message.contains("undeclared")
            || message.contains("not defined"))
    {
        Some(symbol.to_string())
    } else {
        None
    }
}

// ─── Formatting helpers ─────────────────────────────────────────────────────────

pub fn format_global_ident(prefix: String, ident: String) -> String {
    if prefix.is_empty() {
        return ident;
    }
    format!("{prefix}.{ident}")
}

pub fn format_impl_ident(impl_ident: String, key: String) -> String {
    format!("{impl_ident}.{key}")
}

/// Append a generics hash to an identifier: `"foo"` + `42` → `"foo#42"`.
///
/// Panics if `hash` is `0`.  If the ident already contains `#`, returns it
/// unchanged.
pub fn format_generics_ident(ident: String, hash: u64) -> String {
    assert!(hash != 0, "hash must not be 0");
    if ident.contains('#') {
        return ident;
    }
    format!("{ident}#{hash}")
}

/// Round `n` up to the next multiple of `align`.
pub fn align_up(n: u64, align: u64) -> u64 {
    if align == 0 {
        return n;
    }
    (n + align - 1) & !(align - 1)
}

/// Push an analyzer error onto a module's error list.
pub fn errors_push(m: &mut crate::project::Module, e: AnalyzerError) {
    m.analyzer_errors.push(e);
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    // ── offset_to_position ──────────────────────────────────────────────

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
        assert_eq!(offset_to_position(4, &rope), Some(Position::new(1, 0)));
        assert_eq!(offset_to_position(8, &rope), Some(Position::new(2, 0)));
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
        assert!(!is_ident_char('\n'));
    }

    // ── extract_word_at_offset ──────────────────────────────────────────

    #[test]
    fn word_at_offset_simple() {
        let text = "fn hello_world() {}";
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
    fn word_at_offset_out_of_range() {
        assert_eq!(extract_word_at_offset("hi", 100), None);
    }

    #[test]
    fn word_at_offset_empty() {
        assert_eq!(extract_word_at_offset("", 0), None);
    }

    // ── extract_word_at_offset_rope ─────────────────────────────────────

    #[test]
    fn rope_word_matches_str_word() {
        let text = "fn hello_world() {}";
        let rope = Rope::from_str(text);
        assert_eq!(
            extract_word_at_offset(text, 5),
            extract_word_at_offset_rope(&rope, 5)
        );
    }

    #[test]
    fn rope_word_multiline() {
        let rope = Rope::from_str("var x = 10\nvar y = 20");
        let result = extract_word_at_offset_rope(&rope, 15);
        assert_eq!(result, Some(("y".to_string(), 15, 16)));
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
    fn diagnostic_no_match() {
        assert_eq!(
            extract_symbol_from_diagnostic("syntax error: unexpected token"),
            None
        );
    }

    // ── format helpers ──────────────────────────────────────────────────

    #[test]
    fn global_ident_with_prefix() {
        assert_eq!(format_global_ident("pkg".into(), "func".into()), "pkg.func");
    }

    #[test]
    fn global_ident_empty_prefix() {
        assert_eq!(format_global_ident("".into(), "func".into()), "func");
    }

    #[test]
    fn generics_ident() {
        assert_eq!(format_generics_ident("foo".into(), 42), "foo#42");
    }

    #[test]
    fn generics_ident_already_has_hash() {
        assert_eq!(format_generics_ident("foo#7".into(), 42), "foo#7");
    }

    #[test]
    fn align_up_basic() {
        assert_eq!(align_up(5, 8), 8);
        assert_eq!(align_up(8, 8), 8);
        assert_eq!(align_up(0, 8), 0);
        assert_eq!(align_up(5, 0), 5);
    }
}
