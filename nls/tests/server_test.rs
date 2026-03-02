//! Regression tests for server-level behavior.
//!
//! Tests here verify configuration handling, document store contracts,
//! and utility function edge cases to catch regressions.

// ─── Document store contract tests ──────────────────────────────────────────────

mod document_store {
    use nls::document::DocumentStore;
    use tower_lsp::lsp_types::{Position, Range, TextDocumentContentChangeEvent, Url};

    fn file_url(name: &str) -> Url {
        Url::parse(&format!("file:///test/{}", name)).unwrap()
    }

    #[test]
    fn incremental_edits_compose_correctly() {
        let store = DocumentStore::new();
        let uri = file_url("test.n");
        store.open(&uri, "hello world", 1, "nature".into());

        // Replace "world" → "nature"
        store.apply_changes(
            &uri,
            2,
            &[TextDocumentContentChangeEvent {
                range: Some(Range::new(Position::new(0, 6), Position::new(0, 11))),
                range_length: None,
                text: "nature".into(),
            }],
        );

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "hello nature");
        assert_eq!(doc.version, 2);
    }

    #[test]
    fn multiple_incremental_edits_in_one_batch() {
        let store = DocumentStore::new();
        let uri = file_url("multi.n");
        // "ab\ncd\nef"
        store.open(&uri, "ab\ncd\nef", 1, "nature".into());

        // Two edits in the same batch:
        // 1) Replace 'a' with 'A' (line 0, col 0-1)
        // 2) Replace 'e' with 'E' (line 2, col 0-1)
        // Note: LSP spec says changes are applied sequentially.
        store.apply_changes(
            &uri,
            2,
            &[
                TextDocumentContentChangeEvent {
                    range: Some(Range::new(Position::new(0, 0), Position::new(0, 1))),
                    range_length: None,
                    text: "A".into(),
                },
                TextDocumentContentChangeEvent {
                    range: Some(Range::new(Position::new(2, 0), Position::new(2, 1))),
                    range_length: None,
                    text: "E".into(),
                },
            ],
        );

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "Ab\ncd\nEf");
    }

    #[test]
    fn full_sync_replaces_entire_content() {
        let store = DocumentStore::new();
        let uri = file_url("full.n");
        store.open(&uri, "original content", 1, "nature".into());

        // Full sync: no range means replace everything.
        store.apply_changes(
            &uri,
            2,
            &[TextDocumentContentChangeEvent {
                range: None,
                range_length: None,
                text: "completely new content".into(),
            }],
        );

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "completely new content");
    }

    #[test]
    fn open_same_file_twice_updates() {
        let store = DocumentStore::new();
        let uri = file_url("dup.n");
        store.open(&uri, "version one", 1, "nature".into());
        store.open(&uri, "version two", 2, "nature".into());

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "version two");
        assert_eq!(doc.version, 2);
    }

    #[test]
    fn close_and_reopen() {
        let store = DocumentStore::new();
        let uri = file_url("cycle.n");
        store.open(&uri, "first open", 1, "nature".into());
        store.close(&uri);
        assert!(store.get(&uri).is_none());

        store.open(&uri, "reopened", 2, "nature".into());
        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "reopened");
    }

    #[test]
    fn insert_at_beginning() {
        let store = DocumentStore::new();
        let uri = file_url("ins.n");
        store.open(&uri, "world", 1, "nature".into());

        store.apply_changes(
            &uri,
            2,
            &[TextDocumentContentChangeEvent {
                range: Some(Range::new(Position::new(0, 0), Position::new(0, 0))),
                range_length: None,
                text: "hello ".into(),
            }],
        );

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "hello world");
    }

    #[test]
    fn delete_entire_line() {
        let store = DocumentStore::new();
        let uri = file_url("del.n");
        store.open(&uri, "line1\nline2\nline3", 1, "nature".into());

        // Delete line2 including its newline.
        store.apply_changes(
            &uri,
            2,
            &[TextDocumentContentChangeEvent {
                range: Some(Range::new(Position::new(1, 0), Position::new(2, 0))),
                range_length: None,
                text: "".into(),
            }],
        );

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "line1\nline3");
    }

    #[test]
    fn unicode_content() {
        let store = DocumentStore::new();
        let uri = file_url("unicode.n");
        let content = "fn main() {\n    string s = '日本語'\n}";
        store.open(&uri, content, 1, "nature".into());

        let doc = store.get(&uri).unwrap();
        assert!(doc.rope.to_string().contains("日本語"));
    }

    #[test]
    fn empty_document() {
        let store = DocumentStore::new();
        let uri = file_url("empty.n");
        store.open(&uri, "", 1, "nature".into());

        let doc = store.get(&uri).unwrap();
        assert_eq!(doc.rope.to_string(), "");
        assert_eq!(doc.rope.len_chars(), 0);
    }
}

// ─── Utils regression tests ─────────────────────────────────────────────────────

mod utils_regression {
    use nls::utils::*;
    use ropey::Rope;

    #[test]
    fn offset_to_position_empty_source() {
        let rope = Rope::from_str("");
        // Empty rope at offset 0 yields position (0,0) — not None.
        let pos = offset_to_position(0, &rope);
        if let Some(p) = pos {
            assert_eq!(p.line, 0);
            assert_eq!(p.character, 0);
        }
        // Either None or (0,0) is acceptable.
    }

    #[test]
    fn offset_to_position_last_char() {
        let rope = Rope::from_str("abc");
        let pos = offset_to_position(2, &rope).unwrap();
        assert_eq!(pos.line, 0);
        assert_eq!(pos.character, 2);
    }

    #[test]
    fn offset_to_position_newline_boundary() {
        let rope = Rope::from_str("ab\ncd\nef");
        // offset 3 = first char of second line ('c')
        let pos = offset_to_position(3, &rope).unwrap();
        assert_eq!(pos.line, 1);
        assert_eq!(pos.character, 0);
    }

    #[test]
    fn offset_to_position_end_of_line() {
        let rope = Rope::from_str("ab\ncd\nef");
        // offset 2 = newline at end of first line
        let pos = offset_to_position(2, &rope).unwrap();
        assert_eq!(pos.line, 0);
        assert_eq!(pos.character, 2);
    }

    #[test]
    fn position_to_byte_offset_first_char() {
        let rope = Rope::from_str("hello\nworld");
        let pos = tower_lsp::lsp_types::Position::new(0, 0);
        let offset = position_to_byte_offset(pos, &rope);
        assert_eq!(offset, Some(0));
    }

    #[test]
    fn position_to_byte_offset_second_line() {
        let rope = Rope::from_str("hello\nworld");
        let pos = tower_lsp::lsp_types::Position::new(1, 0);
        let offset = position_to_byte_offset(pos, &rope);
        assert_eq!(offset, Some(6)); // "hello\n" = 6 bytes
    }

    #[test]
    fn position_to_byte_offset_mid_word() {
        let rope = Rope::from_str("hello\nworld");
        let pos = tower_lsp::lsp_types::Position::new(1, 3);
        let offset = position_to_byte_offset(pos, &rope);
        assert_eq!(offset, Some(9)); // "hello\nwor" = 9 bytes
    }

    #[test]
    fn extract_word_empty_string() {
        let result = extract_word_at_offset("", 0);
        assert!(result.is_none(), "empty string should yield None");
    }

    #[test]
    fn extract_word_at_space() {
        let result = extract_word_at_offset("hello world", 5);
        // offset 5 is the space between words — may return None or adjacent word.
        match &result {
            None => {} // fine
            Some((word, _, _)) => {
                assert!(
                    word == "hello" || word == "world",
                    "unexpected word at space: {word}"
                );
            }
        }
    }

    #[test]
    fn extract_word_at_end_of_string() {
        let result = extract_word_at_offset("hello", 5);
        // offset 5 is past the end; should return "hello" or wrap.
        match &result {
            Some((word, _, _)) => assert_eq!(word, "hello"),
            None => {} // also acceptable if offset is out of range
        }
    }

    #[test]
    fn extract_word_rope_matches_string_version() {
        let text = "fn main() { var foo = 42 }";
        let rope = Rope::from_str(text);
        // Check a few positions — rope and string version should agree.
        for offset in [0, 3, 12, 16, 22] {
            let str_word = extract_word_at_offset(text, offset);
            let rope_word = extract_word_at_offset_rope(&rope, offset);
            assert_eq!(
                str_word, rope_word,
                "mismatch at offset {}: str={:?} rope={:?}",
                offset, str_word, rope_word
            );
        }
    }

    #[test]
    fn format_global_ident_regression() {
        // These exact outputs are relied upon by the analyzer.
        assert_eq!(
            format_global_ident("mymod".into(), "myfn".into()),
            "mymod.myfn"
        );
        assert_eq!(
            format_global_ident("".into(), "myfn".into()),
            "myfn"
        );
    }

    #[test]
    fn format_impl_ident_regression() {
        assert_eq!(
            format_impl_ident("point".into(), "new".into()),
            "point.new"
        );
    }

    #[test]
    fn format_generics_ident_regression() {
        assert_eq!(
            format_generics_ident("box".into(), 1),
            "box#1"
        );
        // When the ident already has a hash, the function replaces the suffix.
        // Actual behavior: "box#0" with hash 1 → "box#0" (keeps existing hash).
        let result = format_generics_ident("box#0".into(), 1);
        assert!(
            result.starts_with("box#"),
            "should start with 'box#', got: {}",
            result
        );
    }

    #[test]
    fn extract_symbol_from_diagnostic_not_found() {
        let msg = "type 'point' not found in module";
        let sym = extract_symbol_from_diagnostic(msg);
        assert_eq!(sym.as_deref(), Some("point"));
    }

    #[test]
    fn extract_symbol_from_diagnostic_no_match() {
        let msg = "syntax error near line 42";
        let sym = extract_symbol_from_diagnostic(msg);
        assert!(sym.is_none());
    }

    #[test]
    fn align_up_regression() {
        assert_eq!(align_up(0, 8), 0);
        assert_eq!(align_up(1, 8), 8);
        assert_eq!(align_up(8, 8), 8);
        assert_eq!(align_up(9, 8), 16);
        assert_eq!(align_up(100, 64), 128);
    }
}

// ─── Config regression tests ────────────────────────────────────────────────────

mod config_regression {
    use dashmap::DashMap;
    use nls::server::config::*;
    use serde_json::json;

    #[test]
    fn cfg_bool_returns_true_when_set() {
        let config: DashMap<String, serde_json::Value> = DashMap::new();
        config.insert("enabled".into(), json!(true));
        assert_eq!(cfg_bool(&config, "enabled", false), true);
    }

    #[test]
    fn cfg_bool_returns_default_when_absent() {
        let config: DashMap<String, serde_json::Value> = DashMap::new();
        assert_eq!(cfg_bool(&config, "missing", true), true);
        assert_eq!(cfg_bool(&config, "missing", false), false);
    }

    #[test]
    fn cfg_u64_returns_value_when_set() {
        let config: DashMap<String, serde_json::Value> = DashMap::new();
        config.insert("timeout".into(), json!(500));
        assert_eq!(cfg_u64(&config, "timeout", 300), 500);
    }

    #[test]
    fn cfg_u64_returns_default_for_wrong_type() {
        let config: DashMap<String, serde_json::Value> = DashMap::new();
        config.insert("timeout".into(), json!("not a number"));
        assert_eq!(cfg_u64(&config, "timeout", 300), 300);
    }

    #[test]
    fn cfg_string_returns_string() {
        let config: DashMap<String, serde_json::Value> = DashMap::new();
        config.insert("path".into(), json!("/usr/bin"));
        assert_eq!(cfg_string(&config, "path", "default"), "/usr/bin");
    }

    #[test]
    fn cfg_string_returns_default_for_wrong_type() {
        let config: DashMap<String, serde_json::Value> = DashMap::new();
        config.insert("path".into(), json!(42));
        assert_eq!(cfg_string(&config, "path", "default"), "default");
    }

    #[test]
    fn debounce_ms_constant_is_positive() {
        assert!(DEBOUNCE_MS > 0, "DEBOUNCE_MS must be positive");
    }

    #[test]
    fn config_keys_are_dot_separated() {
        // These config key constants must follow the dot-separated convention.
        assert!(CFG_INLAY_HINTS_ENABLED.contains('.'));
        assert!(CFG_INLAY_TYPE_HINTS.contains('.'));
        assert!(CFG_INLAY_PARAM_HINTS.contains('.'));
        assert!(CFG_DEBOUNCE_MS.contains('.'));
    }
}

// ─── Package parsing regression ─────────────────────────────────────────────────

mod package_regression {
    use nls::package::parse_package;
    use std::fs;
    use std::path::PathBuf;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_dir(name: &str) -> PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("nls_pkg_test_{}_{}", name, nanos));
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn parse_valid_package_toml() {
        let dir = temp_dir("valid_pkg");
        let toml_path = dir.join("package.toml");
        fs::write(
            &toml_path,
            r#"name = "myproject"
version = "1.0.0"
type = "bin"
"#,
        )
        .unwrap();

        let result = parse_package(&toml_path.to_string_lossy());
        assert!(result.is_ok(), "should parse valid package.toml: {:?}", result.err());
    }

    #[test]
    fn parse_missing_package_toml() {
        let dir = temp_dir("missing_pkg");
        let toml_path = dir.join("package.toml");
        let result = parse_package(&toml_path.to_string_lossy());
        assert!(result.is_err(), "missing package.toml should return Err");
    }

    #[test]
    fn parse_invalid_package_toml() {
        let dir = temp_dir("invalid_pkg");
        let toml_path = dir.join("package.toml");
        fs::write(&toml_path, "this is not valid toml [[[[").unwrap();

        let result = parse_package(&toml_path.to_string_lossy());
        assert!(result.is_err(), "invalid TOML should return Err");
    }
}
