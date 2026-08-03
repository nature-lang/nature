use nls::analyzer::module_unique_ident;
use nls::project::Project;
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

fn case_root(name: &str) -> PathBuf {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_sync_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

async fn build_errors(name: &str, code: &str) -> Vec<String> {
    let root = case_root(name);
    let file_path = root.join("main.n");
    fs::write(&file_path, code).unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let module_ident = module_unique_ident(&project.root, &file_path.to_string_lossy());
    let module_index = project.build(&file_path.to_string_lossy(), &module_ident, Some(code.to_string())).await;

    let module_db = project.module_db.lock().unwrap();
    module_db[module_index].analyzer_errors.iter().map(|e| e.message.clone()).collect()
}

#[tokio::test]
async fn test_for_range_and_defer_should_pass() {
    let code = r#"
fn main() {
    for i in 0..5 {
        defer println(i)
    }
}
"#;

    let errors = build_errors("for_range_defer_ok", code).await;
    assert!(errors.is_empty(), "expected no analyzer errors, actual: {:?}", errors);
}

#[tokio::test]
async fn test_defer_forbid_control_flow_jumps() {
    let code = r#"
fn main() {
    defer {
        return
    }
}
"#;

    let errors = build_errors("defer_invalid_jump", code).await;
    assert!(
        errors
            .iter()
            .any(|msg| msg.contains("return/break/continue/throw/ret are not allowed inside defer block")),
        "expected defer control-flow error, actual: {:?}",
        errors
    );
}

#[tokio::test]
async fn test_generic_method_interface_cannot_dynamic_dispatch() {
    let code = r#"
type generic_dispatch = interface{
    fn id<T>(T v):T
}

fn main() {
    generic_dispatch a = 1
}
"#;

    let errors = build_errors("generic_interface_dispatch", code).await;
    assert!(
        errors.iter().any(|msg| msg.contains("cannot be used as dynamic dispatch")),
        "expected object safety error, actual: {:?}",
        errors
    );
}

#[tokio::test]
async fn test_anyptr_can_assign_null_literal() {
    let code = r#"
fn main() {
    anyptr v = null
}
"#;

    let errors = build_errors("anyptr_null_literal", code).await;
    assert!(errors.is_empty(), "expected no analyzer errors, actual: {:?}", errors);
}

#[tokio::test]
async fn test_any_can_assign_null_literal() {
    let code = r#"
fn main() {
    any v = null
}
"#;

    let errors = build_errors("any_null_literal", code).await;
    assert!(errors.is_empty(), "expected no analyzer errors, actual: {:?}", errors);
}

#[tokio::test]
async fn test_impl_interface_checked_before_usage() {
    let code = r#"
type measurable = interface{
    fn area():int
    fn perimeter():int
}

type rectangle: measurable = struct{
    int width
    int height
}

fn rectangle.area(&self):int {
    return self.width * self.height
}

fn main() {}
"#;

    let errors = build_errors("impl_interface_early_check", code).await;
    assert!(
        errors.iter().any(|msg| msg.contains("not impl fn 'perimeter' for interface")),
        "expected impl-interface completeness error, actual: {:?}",
        errors
    );
}
