use nls::project::Project;
use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};

async fn errors(code: &str, extension: &str) -> Vec<String> {
    // The analyzer shares parser globals; serialize these integration fixtures.
    static LOCK: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());
    let _guard = LOCK.lock().await;
    std::env::set_var("NATURE_ROOT", std::path::Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap());
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let root = std::env::temp_dir().join(format!("nls_errable_{stamp}"));
    fs::create_dir_all(&root).unwrap();
    let path = root.join(format!("main.{extension}"));
    fs::write(&path, code).unwrap();
    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let index = project.build(&path.to_string_lossy(), "", Some(code.to_string())).await;
    let db = project.module_db.lock().unwrap();
    db[index].analyzer_errors.iter().map(|e| e.message.clone()).collect()
}

#[tokio::test]
async fn enum_errors_in_both_modes() {
    for ext in ["n", "x"] {
        let diagnostics = errors(
            r#"
type error_t:errort = enum { TIMEOUT, CLOSED }
fn leaf():int! { throw error_t.TIMEOUT }
fn main() {
    var v = leaf() catch e {
        var same = e == error_t.TIMEOUT
        var kind = e is error_t
        var value = e as error_t
        -1
    }
}
"#,
            ext,
        )
        .await;
        assert!(diagnostics.is_empty(), "{ext}: {diagnostics:?}");
    }
}

#[tokio::test]
async fn concrete_errors_and_zero_value() {
    let diagnostics = errors(
        r#"
fn zero():errable<int,int> { throw 0 }
fn text():errable<int,string> { throw 'typed' }
fn main() {
    var a = zero() catch e { e }
    var b = text() catch e { e.len() }
}
"#,
        "n",
    )
    .await;
    assert!(diagnostics.is_empty(), "{diagnostics:?}");
}

#[tokio::test]
async fn oversized_marker_payload_is_rejected() {
    let diagnostics = errors(
        r#"
type large_t:errort = struct { int a; int b; int c; int d }
fn fail():void! { throw large_t{a:1,b:2,c:3,d:4} }
fn main() {}
"#,
        "n",
    )
    .await;
    assert!(
        diagnostics.iter().any(|e| e.contains("inline error payload exceeds 24 bytes")),
        "{diagnostics:?}"
    );
}

#[tokio::test]
async fn incompatible_concrete_error_is_rejected() {
    let diagnostics = errors(
        r#"
fn leaf():errable<int,string> { throw 'typed' }
fn caller():errable<int,int> { return leaf() }
fn main() {}
"#,
        "x",
    )
    .await;
    assert!(diagnostics.iter().any(|e| e.contains("cannot propagate error")), "{diagnostics:?}");
}

#[tokio::test]
async fn union_errors_and_result_constructors() {
    for ext in ["n", "x"] {
        let diagnostics = errors(
            r#"
type issue_t = int|string
fn leaf(bool text):errable<int,issue_t> {
    if text { throw 'typed' }
    throw 12
}
fn explicit(bool failed):errable<int,int> {
    if failed { return errable<int,int>.error(0) }
    return errable<int,int>.value(42)
}
fn main() {
    try { leaf(true) } catch e { var is_text = e is string }
    var value = explicit(true) catch e { e }
}
"#,
            ext,
        )
        .await;
        assert!(diagnostics.is_empty(), "{ext}: {diagnostics:?}");
    }
}

#[tokio::test]
async fn mixed_concrete_error_and_bounds_is_rejected() {
    let diagnostics = errors(
        r#"
fn leaf():errable<int,int> { throw 0 }
fn main() {
    try { var a = leaf(); var data = [1,2]; var b = data[0] } catch e {}
}
"#,
        "n",
    )
    .await;
    assert!(diagnostics.iter().any(|e| e.contains("cannot casting to interface")), "{diagnostics:?}");
}

#[tokio::test]
async fn concrete_errors_propagate_to_union() {
    for ext in ["n", "x"] {
        let diagnostics = errors(
            r#"
type error_t:errort = enum { TIMEOUT }
type issue_t = int|string|errort
type large_t = struct { int a; int b; int c; int d; int e; int f; int g; int h }
type wide_t = int|string|errort|large_t
fn number():errable<int,int> { throw 7 }
fn text():errable<int,string> { throw 'typed' }
fn marked():int! { throw error_t.TIMEOUT }
fn outer(int kind):errable<int,issue_t> {
    if kind == 0 { return number() }
    if kind == 1 { return text() }
    return marked()
}
fn widen():errable<int,wide_t> { return outer(0) }
fn main() { try { widen() } catch e {} }
"#,
            ext,
        )
        .await;
        assert!(diagnostics.is_empty(), "{ext}: {diagnostics:?}");
    }
}

#[tokio::test]
async fn error_storage_conversions() {
    for ext in ["n", "x"] {
        let diagnostics = errors(
            r#"
type error_t:errort = enum { TIMEOUT }
type issue_t = errort|int
fn number():errable<int,int> { throw 7 }
fn main() {
    errort original = error_t.TIMEOUT
    any boxed = original
    var restored = boxed as errort
    issue_t union_value = original
    var unwrapped = union_value as errort
    var raw = original as anyptr
    var address = raw as ptr<errort>
    var same = *address == error_t.TIMEOUT
    try { var pointer = original as anyptr; number() } catch code {}
}
"#,
            ext,
        )
        .await;
        assert!(diagnostics.is_empty(), "{ext}: {diagnostics:?}");
    }
}

#[tokio::test]
async fn absent_union_error_member_is_rejected() {
    for ext in ["n", "x"] {
        let diagnostics = errors(
            r#"
type issue_t = int|string
fn leaf():errable<int,bool> { throw false }
fn outer():errable<int,issue_t> { return leaf() }
fn main() {}
"#,
            ext,
        )
        .await;
        assert!(diagnostics.iter().any(|e| e.contains("cannot propagate error")), "{ext}: {diagnostics:?}");
    }
}
