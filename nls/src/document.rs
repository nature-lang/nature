//! Thread-safe document store backed by [`DashMap`] and [`ropey::Rope`].
//!
//! Each open file is tracked as a [`Document`] containing the full source text
//! as a rope plus metadata (version, language id).  The [`DocumentStore`]
//! provides a concurrent-safe façade used by the LSP dispatch layer.

use dashmap::DashMap;
use ropey::Rope;
use tower_lsp::lsp_types::{Position, TextDocumentContentChangeEvent, Url};

// ─── Document ───────────────────────────────────────────────────────────────────

/// A single open document.
#[derive(Debug, Clone)]
pub struct Document {
    /// Full source text.
    pub rope: Rope,
    /// Editor-assigned version (monotonically increasing per file).
    pub version: i32,
    /// Language identifier (e.g. `"nature"` / `"n"`).
    pub language_id: String,
}

impl Document {
    pub fn new(text: &str, version: i32, language_id: String) -> Self {
        Self {
            rope: Rope::from_str(text),
            version,
            language_id,
        }
    }

    /// Return the full source text as a `String`.
    pub fn text(&self) -> String {
        self.rope.to_string()
    }
}

// ─── DocumentStore ──────────────────────────────────────────────────────────────

/// Concurrent map of open documents keyed by file path (string).
///
/// We key by **file path** (`String`) rather than `Url` so that look-ups from
/// other subsystems (project, analyzer) that work with plain paths don't need
/// to round-trip through URL parsing.
#[derive(Debug, Default)]
pub struct DocumentStore {
    docs: DashMap<String, Document>,
}

impl DocumentStore {
    pub fn new() -> Self {
        Self {
            docs: DashMap::new(),
        }
    }

    // ── Lifecycle ───────────────────────────────────────────────────────

    /// Track a newly opened document.
    pub fn open(&self, uri: &Url, text: &str, version: i32, language_id: String) {
        let path = uri.path().to_string();
        self.docs
            .insert(path, Document::new(text, version, language_id));
    }

    /// Stop tracking a closed document.
    pub fn close(&self, uri: &Url) {
        let path = uri.path().to_string();
        self.docs.remove(&path);
    }

    // ── Queries ─────────────────────────────────────────────────────────

    /// Retrieve a clone of the document for the given URI, if open.
    pub fn get(&self, uri: &Url) -> Option<Document> {
        let path = uri.path().to_string();
        self.docs.get(&path).map(|d| d.clone())
    }

    /// Retrieve a clone of the document by file path.
    pub fn get_by_path(&self, path: &str) -> Option<Document> {
        self.docs.get(path).map(|d| d.clone())
    }

    /// Get the rope for a file path (convenience accessor).
    pub fn get_rope(&self, path: &str) -> Option<Rope> {
        self.docs.get(path).map(|d| d.rope.clone())
    }

    /// Get the full source text for a file path.
    pub fn get_text(&self, path: &str) -> Option<String> {
        self.docs.get(path).map(|d| d.rope.to_string())
    }

    /// Check whether a document is currently open.
    pub fn is_open(&self, path: &str) -> bool {
        self.docs.contains_key(path)
    }

    /// Number of open documents.
    pub fn len(&self) -> usize {
        self.docs.len()
    }

    /// Whether the store is empty.
    pub fn is_empty(&self) -> bool {
        self.docs.is_empty()
    }

    // ── Incremental sync ────────────────────────────────────────────────

    /// Apply a batch of LSP content-change events to a document.
    ///
    /// Supports both **full** replacements (no range) and **incremental** edits
    /// (with a range).  Returns the full text after all changes are applied, or
    /// `None` if the document isn't tracked.
    pub fn apply_changes(
        &self,
        uri: &Url,
        version: i32,
        changes: &[TextDocumentContentChangeEvent],
    ) -> Option<String> {
        let path = uri.path().to_string();
        let mut entry = self.docs.get_mut(&path)?;

        for change in changes {
            if let Some(range) = change.range {
                // Incremental edit.
                let start = position_to_char_idx(&entry.rope, range.start);
                let end = position_to_char_idx(&entry.rope, range.end);

                let start = start.min(entry.rope.len_chars());
                let end = end.min(entry.rope.len_chars());

                if start < end {
                    entry.rope.remove(start..end);
                }
                if !change.text.is_empty() {
                    entry.rope.insert(start, &change.text);
                }
            } else {
                // Full replacement.
                entry.rope = Rope::from_str(&change.text);
            }
        }

        entry.version = version;
        Some(entry.rope.to_string())
    }
}

// ─── Helpers ────────────────────────────────────────────────────────────────────

/// Convert an LSP `Position` (line, character — both 0-based) to a rope char
/// index, clamping to valid bounds.
fn position_to_char_idx(rope: &Rope, pos: Position) -> usize {
    let line = (pos.line as usize).min(rope.len_lines().saturating_sub(1));
    let line_start = rope.line_to_char(line);
    let line_len = rope.line(line).len_chars();
    let col = (pos.character as usize).min(line_len);
    line_start + col
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use tower_lsp::lsp_types::Range;

    fn test_uri(name: &str) -> Url {
        Url::parse(&format!("file:///tmp/{name}.n")).unwrap()
    }

    // ── Lifecycle ───────────────────────────────────────────────────────

    #[test]
    fn open_and_get() {
        let store = DocumentStore::new();
        let uri = test_uri("a");
        store.open(&uri, "hello world", 1, "nature".into());

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.text(), "hello world");
        assert_eq!(doc.version, 1);
        assert_eq!(doc.language_id, "nature");
    }

    #[test]
    fn close_removes_document() {
        let store = DocumentStore::new();
        let uri = test_uri("b");
        store.open(&uri, "content", 1, "nature".into());
        assert!(store.is_open(uri.path()));

        store.close(&uri);
        assert!(!store.is_open(uri.path()));
        assert!(store.get(&uri).is_none());
    }

    #[test]
    fn get_by_path() {
        let store = DocumentStore::new();
        let uri = test_uri("c");
        store.open(&uri, "fn main() {}", 1, "nature".into());

        let doc = store.get_by_path(uri.path()).unwrap();
        assert_eq!(doc.text(), "fn main() {}");
    }

    #[test]
    fn get_missing_returns_none() {
        let store = DocumentStore::new();
        assert!(store.get_by_path("/nonexistent").is_none());
    }

    #[test]
    fn len_and_is_empty() {
        let store = DocumentStore::new();
        assert!(store.is_empty());
        assert_eq!(store.len(), 0);

        store.open(&test_uri("x"), "", 1, "n".into());
        assert!(!store.is_empty());
        assert_eq!(store.len(), 1);
    }

    // ── Full replacement ────────────────────────────────────────────────

    #[test]
    fn apply_full_replacement() {
        let store = DocumentStore::new();
        let uri = test_uri("full");
        store.open(&uri, "old content", 1, "nature".into());

        let changes = vec![TextDocumentContentChangeEvent {
            range: None,
            range_length: None,
            text: "new content".into(),
        }];
        let text = store.apply_changes(&uri, 2, &changes).unwrap();
        assert_eq!(text, "new content");

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.version, 2);
    }

    // ── Incremental edits ───────────────────────────────────────────────

    #[test]
    fn apply_incremental_insert() {
        let store = DocumentStore::new();
        let uri = test_uri("inc_ins");
        store.open(&uri, "helo world", 1, "nature".into());

        // Insert 'l' at position (0, 3) → "hello world"
        let changes = vec![TextDocumentContentChangeEvent {
            range: Some(Range::new(Position::new(0, 3), Position::new(0, 3))),
            range_length: None,
            text: "l".into(),
        }];
        let text = store.apply_changes(&uri, 2, &changes).unwrap();
        assert_eq!(text, "hello world");
    }

    #[test]
    fn apply_incremental_delete() {
        let store = DocumentStore::new();
        let uri = test_uri("inc_del");
        store.open(&uri, "helllo world", 1, "nature".into());

        // Delete one 'l' at (0,3)..(0,4) → "hello world"
        let changes = vec![TextDocumentContentChangeEvent {
            range: Some(Range::new(Position::new(0, 3), Position::new(0, 4))),
            range_length: None,
            text: "".into(),
        }];
        let text = store.apply_changes(&uri, 2, &changes).unwrap();
        assert_eq!(text, "hello world");
    }

    #[test]
    fn apply_incremental_replace() {
        let store = DocumentStore::new();
        let uri = test_uri("inc_rep");
        store.open(&uri, "fn foo() {}", 1, "nature".into());

        // Replace "foo" (3..6) with "bar"
        let changes = vec![TextDocumentContentChangeEvent {
            range: Some(Range::new(Position::new(0, 3), Position::new(0, 6))),
            range_length: None,
            text: "bar".into(),
        }];
        let text = store.apply_changes(&uri, 2, &changes).unwrap();
        assert_eq!(text, "fn bar() {}");
    }

    #[test]
    fn apply_multiline_edit() {
        let store = DocumentStore::new();
        let uri = test_uri("multiline");
        store.open(&uri, "line1\nline2\nline3", 1, "nature".into());

        // Replace "line2" (line 1, chars 0..5) with "replaced"
        let changes = vec![TextDocumentContentChangeEvent {
            range: Some(Range::new(Position::new(1, 0), Position::new(1, 5))),
            range_length: None,
            text: "replaced".into(),
        }];
        let text = store.apply_changes(&uri, 2, &changes).unwrap();
        assert_eq!(text, "line1\nreplaced\nline3");
    }

    #[test]
    fn apply_multiple_changes_in_batch() {
        let store = DocumentStore::new();
        let uri = test_uri("batch");
        store.open(&uri, "aaa bbb ccc", 1, "nature".into());

        // Two changes in one batch: replace "aaa" → "xxx", then after that
        // the text is "xxx bbb ccc"; replace "ccc" is now at (0,8..11)
        let changes = vec![
            TextDocumentContentChangeEvent {
                range: Some(Range::new(Position::new(0, 0), Position::new(0, 3))),
                range_length: None,
                text: "xxx".into(),
            },
            TextDocumentContentChangeEvent {
                range: Some(Range::new(Position::new(0, 8), Position::new(0, 11))),
                range_length: None,
                text: "zzz".into(),
            },
        ];
        let text = store.apply_changes(&uri, 2, &changes).unwrap();
        assert_eq!(text, "xxx bbb zzz");
    }

    #[test]
    fn apply_changes_to_missing_doc_returns_none() {
        let store = DocumentStore::new();
        let uri = test_uri("missing");
        let changes = vec![TextDocumentContentChangeEvent {
            range: None,
            range_length: None,
            text: "text".into(),
        }];
        assert!(store.apply_changes(&uri, 1, &changes).is_none());
    }

    // ── position_to_char_idx ────────────────────────────────────────────

    #[test]
    fn position_to_char_idx_basic() {
        let rope = Rope::from_str("abc\ndef\nghi");
        assert_eq!(position_to_char_idx(&rope, Position::new(0, 0)), 0);
        assert_eq!(position_to_char_idx(&rope, Position::new(0, 2)), 2);
        assert_eq!(position_to_char_idx(&rope, Position::new(1, 0)), 4); // 'd'
        assert_eq!(position_to_char_idx(&rope, Position::new(2, 1)), 9); // 'h'
    }

    #[test]
    fn position_to_char_idx_clamps_column() {
        let rope = Rope::from_str("ab\ncd");
        // Column 99 on a 3-char line (including \n) should clamp
        let idx = position_to_char_idx(&rope, Position::new(0, 99));
        assert!(idx <= rope.len_chars());
    }

    #[test]
    fn position_to_char_idx_clamps_line() {
        let rope = Rope::from_str("only one line");
        // Line 99 should clamp to last line
        let idx = position_to_char_idx(&rope, Position::new(99, 0));
        assert!(idx <= rope.len_chars());
    }
}
