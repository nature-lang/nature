use log::debug;

/// Extract prefix at cursor position from text
pub fn extract_prefix_at_position(text: &str, position: usize) -> String {
    if position == 0 {
        return String::new();
    }

    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return String::new();
    }

    let mut start = position;

    // Search backward for start of identifier
    while start > 0 {
        let ch = chars[start - 1];
        if ch.is_alphanumeric() || ch == '_' || ch == '.' {
            start -= 1;
        } else {
            break;
        }
    }

    // Extract prefix
    chars[start..position].iter().collect()
}

/// Extract last identifier part from something like "io.main.writer" to get "writer"
pub(crate) fn extract_last_ident_part(ident: &str) -> String {
    if let Some(dot_pos) = ident.rfind('.') {
        ident[dot_pos + 1..].to_string()
    } else {
        ident.to_string()
    }
}

/// Public version of extract_last_ident_part for use by other modules.
pub fn extract_last_ident_part_pub(ident: &str) -> String {
    extract_last_ident_part(ident)
}

/// Detect if in module member access context, returns (module_name, member_prefix)
pub fn extract_module_member_context(prefix: &str, _position: usize) -> Option<(String, String)> {
    if let Some(dot_pos) = prefix.rfind('.') {
        let module_name = prefix[..dot_pos].to_string();
        let member_prefix = prefix[dot_pos + 1..].to_string();

        if !module_name.is_empty() {
            return Some((module_name, member_prefix));
        }
    }

    None
}

/// Detect if cursor is inside a selective import `{...}` block.
/// Returns (module_path, item_prefix) where module_path is the import path before `.{`
/// Example: `import forest.app.create.{cre|` -> ("forest.app.create", "cre")
pub fn extract_selective_import_context(text: &str, position: usize) -> Option<(String, String)> {
    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return None;
    }

    // Extract any prefix at cursor
    let mut prefix_start = position;
    while prefix_start > 0 {
        let ch = chars[prefix_start - 1];
        if ch.is_alphanumeric() || ch == '_' {
            prefix_start -= 1;
        } else {
            break;
        }
    }
    let item_prefix: String = chars[prefix_start..position].iter().collect();

    // Look backward from prefix_start (or position) to find `{`
    let mut i = prefix_start;
    while i > 0 {
        i -= 1;
        let ch = chars[i];

        if ch == '{' {
            // Found `{` — now check if it's preceded by `.` and then an import path
            // Before `{` there should be a `.`
            let mut dot_pos = i;
            // skip whitespace between `.` and `{`
            while dot_pos > 0 && chars[dot_pos - 1].is_whitespace() {
                dot_pos -= 1;
            }
            if dot_pos == 0 || chars[dot_pos - 1] != '.' {
                return None;
            }
            let dot_idx = dot_pos - 1;

            // Before the `.` should be the module path (ident chars and dots)
            let path_end = dot_idx;
            let mut path_start = path_end;
            while path_start > 0 {
                let ch = chars[path_start - 1];
                if ch.is_alphanumeric() || ch == '_' || ch == '.' {
                    path_start -= 1;
                } else {
                    break;
                }
            }

            if path_start >= path_end {
                return None;
            }

            let module_path: String = chars[path_start..path_end].iter().collect();

            // Check that `import` keyword precedes the module path
            let mut kw_end = path_start;
            while kw_end > 0 && chars[kw_end - 1].is_whitespace() {
                kw_end -= 1;
            }
            let mut kw_start = kw_end;
            while kw_start > 0 && chars[kw_start - 1].is_alphabetic() {
                kw_start -= 1;
            }
            let keyword: String = chars[kw_start..kw_end].iter().collect();
            if keyword != "import" {
                return None;
            }

            debug!("Detected selective import context: module='{}', prefix='{}'", module_path, item_prefix);
            return Some((module_path, item_prefix));
        } else if ch == '}' || ch == ';' {
            // `}` means we're past/outside the braces already
            return None;
        }
        // Skip over commas, spaces, other ident chars (already-typed items)
    }

    None
}

/// Detect if cursor is inside a struct initialization, returns (type_name, field_prefix)
/// Handles cases like: `config{ [cursor] }` or `config{ value: 42, [cursor] }`
pub fn extract_struct_init_context(text: &str, position: usize) -> Option<(String, String)> {
    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return None;
    }

    // Look backward to find the opening brace and type name
    let mut i = position;

    // First, extract any prefix at the cursor position
    let mut field_prefix_end = position;
    while field_prefix_end > 0 {
        let ch = chars[field_prefix_end - 1];
        if ch.is_alphanumeric() || ch == '_' {
            field_prefix_end -= 1;
        } else {
            break;
        }
    }
    let field_prefix: String = chars[field_prefix_end..position].iter().collect();

    // Look for opening brace
    while i > 0 {
        i -= 1;
        let ch = chars[i];

        if ch == '{' {
            // Found opening brace, now look for the type name before it
            let mut type_end = i;

            // Skip whitespace between type name and brace
            while type_end > 0 && chars[type_end - 1].is_whitespace() {
                type_end -= 1;
            }

            // Extract type name
            let mut type_start = type_end;
            while type_start > 0 {
                let ch = chars[type_start - 1];
                if ch.is_alphanumeric() || ch == '_' || ch == '.' {
                    type_start -= 1;
                } else {
                    break;
                }
            }

            if type_start < type_end {
                let type_name: String = chars[type_start..type_end].iter().collect();

                // Reject block keywords used as the "type name"
                // (e.g. `if cond {`, `for x in list {`, `else {`, `match val {`)
                let block_keywords = ["if", "else", "for", "match", "while", "catch"];
                if block_keywords.contains(&type_name.as_str()) {
                    return None;
                }

                // Check the character immediately before the type name
                // (after skipping whitespace). A `:` means this is a return-type
                // annotation (`fn foo(): string {`), not a struct init.
                // A `)` means `fn foo() {` — also not a struct init.
                let mut before = type_start;
                while before > 0 && chars[before - 1].is_whitespace() {
                    before -= 1;
                }
                if before > 0 {
                    let prev_char = chars[before - 1];
                    if prev_char == ':' || prev_char == ')' {
                        return None;
                    }
                }

                // Avoid treating test/fn blocks as struct initializations
                let mut kw_end = type_start;
                while kw_end > 0 && chars[kw_end - 1].is_whitespace() {
                    kw_end -= 1;
                }
                let mut kw_start = kw_end;
                while kw_start > 0 {
                    let ch = chars[kw_start - 1];
                    if ch.is_alphanumeric() || ch == '_' {
                        kw_start -= 1;
                    } else {
                        break;
                    }
                }
                if kw_start < kw_end {
                    let maybe_kw: String = chars[kw_start..kw_end].iter().collect();
                    if maybe_kw == "test" || maybe_kw == "fn" {
                        return None;
                    }
                }

                debug!("Detected struct init context: type='{}', field_prefix='{}'", type_name, field_prefix);
                return Some((type_name, field_prefix));
            }

            return None;
        } else if ch == '}' || ch == ';' {
            // We've left the struct initialization context
            return None;
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::extract_struct_init_context;

    #[test]
    fn detects_basic_struct_init() {
        let text = "MyStruct{ na";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("MyStruct".into(), "na".into())));
    }

    #[test]
    fn detects_struct_init_with_space() {
        let text = "MyStruct { name";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("MyStruct".into(), "name".into())));
    }

    #[test]
    fn empty_prefix() {
        let text = "MyStruct{ ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("MyStruct".into(), "".into())));
    }

    #[test]
    fn rejects_fn_body_brace() {
        // fn MyCustomStruct.toString(): string { ... }
        let text = "fn MyCustomStruct.toString(): string { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, None);
    }

    #[test]
    fn rejects_fn_no_return_type() {
        let text = "fn foo() { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, None);
    }

    #[test]
    fn rejects_if_block() {
        // "if" is a block keyword, but the extracted "type name" is
        // "condition" (the word before {). The if-keyword check works
        // when `if` directly precedes `{`, like `if {`.
        let text = "if { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, None);
    }

    #[test]
    fn if_with_condition_falls_through() {
        // `if condition {` extracts "condition" as struct name.
        // The completion system's fallthrough handles this gracefully.
        let text = "if condition { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("condition".into(), "".into())));
    }

    #[test]
    fn rejects_else_block() {
        let text = "else { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, None);
    }

    #[test]
    fn rejects_for_block() {
        // `for {` is rejected by the block keyword check.
        let text = "for { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, None);
    }

    #[test]
    fn for_with_expr_falls_through() {
        // `for x in list {` extracts "list" as struct name.
        // The completion fallthrough handles this.
        let text = "for x in list { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("list".into(), "".into())));
    }

    #[test]
    fn rejects_test_block() {
        let text = "test my_test { ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, None);
    }

    #[test]
    fn stops_at_closing_brace() {
        let text = "MyStruct{ name: 1 }; OtherStruct{ ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("OtherStruct".into(), "".into())));
    }

    #[test]
    fn stops_at_semicolon() {
        let text = "var x = 1; MyStruct{ ";
        let result = extract_struct_init_context(text, text.len());
        assert_eq!(result, Some(("MyStruct".into(), "".into())));
    }
}
