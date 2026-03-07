//! Regression tests for the build pipeline.
//!
//! Each test creates a temp directory with `.n` source files, runs the full
//! `Project::build` pipeline, and asserts on concrete outcomes (error count,
//! error messages, module count, etc.).  If the analyzer, lexer, parser, or
//! type system changes behaviour, these tests will fail.

use nls::analyzer::module_unique_ident;
use nls::project::Project;
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

/// Create a unique temp directory for a test case.
fn temp_project(name: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_test_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

/// Helper: build a single-file project and return (module_index, errors).
async fn build_single_file(
    name: &str,
    code: &str,
) -> (usize, Vec<nls::analyzer::common::AnalyzerError>) {
    let root = temp_project(name);
    let file = root.join("main.n");
    fs::write(&file, code).unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let ident = module_unique_ident(&project.root, &file.to_string_lossy());
    let idx = project
        .build(&file.to_string_lossy(), &ident, Some(code.to_string()))
        .await;

    let db = project.module_db.lock().unwrap();
    let errors = db[idx].analyzer_errors.clone();
    (idx, errors)
}

// ─── Valid programs should produce zero errors ──────────────────────────────────

#[tokio::test]
async fn valid_empty_main() {
    let (_, errors) = build_single_file(
        "empty_main",
        r#"
fn main() {
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "expected no errors for empty main, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn valid_variable_declaration() {
    let (_, errors) = build_single_file(
        "var_decl",
        r#"
fn main() {
    int x = 42
    var y = 10
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "expected no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn valid_function_with_return() {
    let (_, errors) = build_single_file(
        "fn_return",
        r#"
fn add(int a, int b):int {
    return a + b
}

fn main() {
    int result = add(1, 2)
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "expected no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn valid_struct_definition() {
    let (_, errors) = build_single_file(
        "struct_def",
        r#"
type point = struct {
    int x
    int y
}

fn main() {
    var p = point{x: 1, y: 2}
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "expected no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn valid_if_else() {
    let (_, errors) = build_single_file(
        "if_else",
        r#"
fn main() {
    int x = 10
    if x > 5 {
        var y = 1
    } else {
        var z = 2
    }
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "expected no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn valid_for_loop() {
    let (_, errors) = build_single_file(
        "for_loop",
        r#"
fn main() {
    for int i = 0; i < 10; i += 1 {
        var x = i
    }
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "expected no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

// ─── Invalid programs should produce specific errors ────────────────────────────

#[tokio::test]
async fn error_global_non_empty_string() {
    let (_, errors) = build_single_file(
        "global_str_err",
        r#"
string s = 'hello world'

fn main() {
}
"#,
    )
    .await;
    assert!(
        !errors.is_empty(),
        "expected errors for non-empty global string initializer"
    );
    let has_global_str_error = errors
        .iter()
        .any(|e| e.message.contains("global string initializer must be empty"));
    assert!(
        has_global_str_error,
        "expected 'global string initializer must be empty' error, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn error_undeclared_variable() {
    let (_, errors) = build_single_file(
        "undeclared_var",
        r#"
fn main() {
    int x = undefined_var
}
"#,
    )
    .await;
    assert!(
        !errors.is_empty(),
        "expected error for undeclared variable"
    );
    // The analyzer should report *something* about an undefined symbol.
    let has_relevant_error = errors.iter().any(|e| {
        e.message.contains("not found")
            || e.message.contains("undeclared")
            || e.message.contains("undefined")
            || e.message.contains("not defined")
    });
    assert!(
        has_relevant_error,
        "expected an 'undefined/not found' error, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn error_type_mismatch() {
    let (_, errors) = build_single_file(
        "type_mismatch",
        r#"
fn main() {
    int x = 'hello'
}
"#,
    )
    .await;
    assert!(
        !errors.is_empty(),
        "expected type mismatch error"
    );
}

#[tokio::test]
async fn error_missing_return() {
    let (_, errors) = build_single_file(
        "missing_return",
        r#"
fn foo():int {
    var x = 42
}

fn main() {
}
"#,
    )
    .await;
    assert!(
        !errors.is_empty(),
        "expected missing return error"
    );
    let has_return_error = errors
        .iter()
        .any(|e| e.message.contains("return"));
    assert!(
        has_return_error,
        "expected a return-related error, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

// ─── Build pipeline structure ───────────────────────────────────────────────────

#[tokio::test]
async fn build_creates_module_entry() {
    let root = temp_project("module_entry");
    let file = root.join("main.n");
    fs::write(&file, "fn main() {}").unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let ident = module_unique_ident(&project.root, &file.to_string_lossy());
    let idx = project
        .build(&file.to_string_lossy(), &ident, Some("fn main() {}".into()))
        .await;

    // Module should be registered in the handled map.
    let handled = project.module_handled.lock().unwrap();
    assert!(handled.contains_key(&*file.to_string_lossy()));

    // Module DB should contain it.
    let db = project.module_db.lock().unwrap();
    assert!(idx < db.len());
    assert_eq!(db[idx].path, file.to_string_lossy().to_string());
}

#[tokio::test]
async fn rebuild_updates_source() {
    let root = temp_project("rebuild");
    let file = root.join("main.n");
    let code_v1 = "fn main() { int x = 1 }";
    let code_v2 = "fn main() { int x = 2 }";
    fs::write(&file, code_v1).unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let ident = module_unique_ident(&project.root, &file.to_string_lossy());

    // First build.
    let idx = project
        .build(&file.to_string_lossy(), &ident, Some(code_v1.into()))
        .await;
    {
        let db = project.module_db.lock().unwrap();
        assert_eq!(db[idx].source, code_v1);
    }

    // Rebuild with new content.
    let idx2 = project
        .build(&file.to_string_lossy(), &ident, Some(code_v2.into()))
        .await;
    {
        let db = project.module_db.lock().unwrap();
        assert_eq!(db[idx2].source, code_v2);
    }
    assert_eq!(idx, idx2, "rebuild should reuse the same module index");
}

#[tokio::test]
async fn lexer_produces_tokens() {
    let root = temp_project("tokens");
    let file = root.join("main.n");
    let code = "fn main() { int x = 42 }";
    fs::write(&file, code).unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let ident = module_unique_ident(&project.root, &file.to_string_lossy());
    let idx = project
        .build(&file.to_string_lossy(), &ident, Some(code.into()))
        .await;

    let db = project.module_db.lock().unwrap();
    let m = &db[idx];
    // The lexer should have produced tokens.
    assert!(
        !m.token_db.is_empty(),
        "expected lexer to produce tokens"
    );
    // The parser should have produced statements.
    assert!(
        !m.stmts.is_empty(),
        "expected parser to produce statements"
    );
}

// ─── Diagnostics builder ────────────────────────────────────────────────────────

#[tokio::test]
async fn build_diagnostics_deduplicates() {
    use nls::analyzer::common::AnalyzerError;
    use nls::project::Module;

    let mut m = Module::default();
    m.source = "fn main() {}".into();
    m.rope = ropey::Rope::from_str(&m.source);

    // Push two identical errors at the same position.
    m.analyzer_errors.push(AnalyzerError {
        start: 0,
        end: 2,
        message: "duplicate error".into(),
        is_warning: false,
    });
    m.analyzer_errors.push(AnalyzerError {
        start: 0,
        end: 2,
        message: "duplicate error".into(),
        is_warning: false,
    });

    let diagnostics = nls::server::Backend::build_diagnostics(&m);
    assert_eq!(
        diagnostics.len(),
        1,
        "duplicate errors at the same position should be deduplicated"
    );
}

#[tokio::test]
async fn build_diagnostics_warning_has_unnecessary_tag() {
    use nls::analyzer::common::AnalyzerError;
    use nls::project::Module;
    use tower_lsp::lsp_types::DiagnosticTag;

    let mut m = Module::default();
    m.source = "fn main() {}".into();
    m.rope = ropey::Rope::from_str(&m.source);
    m.analyzer_errors.push(AnalyzerError {
        start: 0,
        end: 2,
        message: "unused variable".into(),
        is_warning: true,
    });

    let diagnostics = nls::server::Backend::build_diagnostics(&m);
    assert_eq!(diagnostics.len(), 1);
    let tags = diagnostics[0].tags.as_ref().unwrap();
    assert!(
        tags.contains(&DiagnosticTag::UNNECESSARY),
        "warning diagnostics should have the UNNECESSARY tag"
    );
}

#[tokio::test]
async fn build_diagnostics_skips_zero_end() {
    use nls::analyzer::common::AnalyzerError;
    use nls::project::Module;

    let mut m = Module::default();
    m.source = "fn main() {}".into();
    m.rope = ropey::Rope::from_str(&m.source);
    m.analyzer_errors.push(AnalyzerError {
        start: 0,
        end: 0, // end == 0 should be filtered out
        message: "internal error".into(),
        is_warning: false,
    });

    let diagnostics = nls::server::Backend::build_diagnostics(&m);
    assert!(
        diagnostics.is_empty(),
        "errors with end == 0 should be skipped"
    );
}

// ─── Module helpers ─────────────────────────────────────────────────────────────

#[test]
fn module_new_sets_dir() {
    use nls::project::Module;

    let m = Module::new(
        "test".into(),
        "fn main() {}".into(),
        "/home/user/project/main.n".into(),
        0,
        0,
    );
    assert_eq!(m.dir, "/home/user/project");
    assert_eq!(m.path, "/home/user/project/main.n");
    assert_eq!(m.ident, "test");
    assert_eq!(m.source, "fn main() {}");
}

#[test]
fn module_default_is_empty() {
    use nls::project::Module;

    let m = Module::default();
    assert!(m.source.is_empty());
    assert!(m.path.is_empty());
    assert!(m.token_db.is_empty());
    assert!(m.stmts.is_empty());
    assert!(m.analyzer_errors.is_empty());
    assert!(m.references.is_empty());
    assert!(m.dependencies.is_empty());
}

// ─── module_unique_ident ────────────────────────────────────────────────────────

#[test]
fn module_unique_ident_strips_root() {
    let ident = module_unique_ident("/home/user/project", "/home/user/project/main.n");
    assert!(
        !ident.is_empty(),
        "module_unique_ident should produce a non-empty ident"
    );
    assert!(
        !ident.contains("/home/user/project/"),
        "ident should not contain the full root path"
    );
}

// ─── Diagnostic position & message regression ───────────────────────────────────

/// Verify that diagnostics for a known error have the correct severity.
#[tokio::test]
async fn diagnostic_severity_error_for_type_mismatch() {
    use tower_lsp::lsp_types::DiagnosticSeverity;

    let (_, errors) = build_single_file(
        "sev_type_mismatch",
        r#"
fn main() {
    int x = 'hello'
}
"#,
    )
    .await;
    assert!(!errors.is_empty(), "should have errors");
    // All type mismatch errors must be non-warnings (errors).
    for e in &errors {
        assert!(!e.is_warning, "type mismatch should be an error, not warning");
    }

    // Convert to LSP diagnostics and check severity.
    let mut m = nls::project::Module::default();
    m.source = "fn main() {\n    int x = 'hello'\n}\n".into();
    m.rope = ropey::Rope::from_str(&m.source);
    m.analyzer_errors = errors;
    let diagnostics = nls::server::Backend::build_diagnostics(&m);
    for d in &diagnostics {
        assert_eq!(
            d.severity,
            Some(DiagnosticSeverity::ERROR),
            "type mismatch diagnostic should be ERROR severity"
        );
    }
}

/// Diagnostics should have non-zero ranges (start != end).
#[tokio::test]
async fn diagnostic_ranges_are_non_empty() {
    let (_, errors) = build_single_file(
        "ranges_nonempty",
        r#"
fn main() {
    int x = undefined_var
}
"#,
    )
    .await;
    assert!(!errors.is_empty());

    let mut m = nls::project::Module::default();
    m.source = "fn main() {\n    int x = undefined_var\n}\n".into();
    m.rope = ropey::Rope::from_str(&m.source);
    m.analyzer_errors = errors;
    let diagnostics = nls::server::Backend::build_diagnostics(&m);
    assert!(!diagnostics.is_empty(), "should produce diagnostics");
    for d in &diagnostics {
        assert_ne!(
            d.range.start, d.range.end,
            "diagnostic range should not be zero-width: {:?}",
            d
        );
    }
}

/// Multiple distinct errors should each appear in the diagnostics list.
#[tokio::test]
async fn diagnostic_multiple_errors_are_preserved() {
    let (_, errors) = build_single_file(
        "multi_errors",
        r#"
fn main() {
    int x = undef_a
    int y = undef_b
}
"#,
    )
    .await;
    // Should have at least two errors.
    assert!(
        errors.len() >= 2,
        "expected at least 2 errors, got {}",
        errors.len()
    );
}

/// A valid program must produce zero diagnostics through the full pipeline.
#[tokio::test]
async fn diagnostic_zero_for_valid_program() {
    let (idx, errors) = build_single_file(
        "zero_diag",
        r#"
fn main() {
    int x = 42
    int y = x + 1
}
"#,
    )
    .await;
    assert!(errors.is_empty(), "valid program should produce zero errors");

    // Also verify via build_diagnostics.
    let mut m = nls::project::Module::default();
    m.source = "fn main() {\n    int x = 42\n    int y = x + 1\n}\n".into();
    m.rope = ropey::Rope::from_str(&m.source);
    m.analyzer_errors = errors;
    let diagnostics = nls::server::Backend::build_diagnostics(&m);
    assert!(diagnostics.is_empty(), "diagnostics should be empty for valid code");
}

// ─── Additional language feature regression ─────────────────────────────────────

/// Generic type definition and usage should compile clean.
#[tokio::test]
async fn valid_generic_type() {
    let (_, errors) = build_single_file(
        "generic",
        r#"
type box<t0> = struct {
    t0 value
}

fn main() {
    var b = box<int>{value: 42}
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "generic type usage should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// Array operations should work.
#[tokio::test]
async fn valid_array() {
    let (_, errors) = build_single_file(
        "array",
        r#"
fn main() {
    var list = [1, 2, 3]
    var first = list[0]
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "array usage should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// Map literal should work.
#[tokio::test]
async fn valid_map() {
    let (_, errors) = build_single_file(
        "map",
        r#"
fn main() {
    var m = {1: 'hello', 2: 'world'}
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "map usage should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// Tuple definition and access should work.
#[tokio::test]
async fn valid_tuple() {
    let (_, errors) = build_single_file(
        "tuple",
        r#"
fn main() {
    var t = (1, 'hello', true)
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "tuple usage should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// Error handling with try-catch should analyze cleanly.
#[tokio::test]
async fn valid_error_handling() {
    let (_, errors) = build_single_file(
        "error_handling",
        r#"
fn risky():int! {
    return 42
}

fn main() {
    var result = risky() catch err {
        return
    }
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "error handling should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// Closure / anonymous function should analyze cleanly.
#[tokio::test]
async fn valid_closure() {
    let (_, errors) = build_single_file(
        "closure",
        r#"
fn main() {
    var add = fn(int a, int b):int {
        return a + b
    }
    var result = add(1, 2)
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "closure should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// Type alias should work.
#[tokio::test]
async fn valid_type_alias() {
    let (_, errors) = build_single_file(
        "type_alias",
        r#"
type num = int

fn main() {
    num x = 42
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "type alias should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// `as` type cast should work.
#[tokio::test]
async fn valid_type_cast() {
    let (_, errors) = build_single_file(
        "type_cast",
        r#"
fn main() {
    f64 x = 3.14
    int y = x as int
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "type cast should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}

/// `is` type check should work.
#[tokio::test]
async fn valid_is_expr() {
    let (_, errors) = build_single_file(
        "is_expr",
        r#"
type animal = int|string

fn main() {
    animal a = 42
    if a is int {
        var x = a as int
    }
}
"#,
    )
    .await;
    assert!(
        errors.is_empty(),
        "is expression should produce no errors, got: {:?}",
        errors.iter().map(|e| &e.message).collect::<Vec<_>>()
    );
}
