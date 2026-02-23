use nls::analyzer::module_unique_ident;
use nls::project::Project;
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

fn case_root(name: &str) -> PathBuf {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[tokio::test]
async fn test_global_eval_non_empty_string_should_fail() {
    let root = case_root("global_eval");
    let file_path = root.join("main.n");
    let code = r#"
string s = 'hello world'

fn main() {
}
"#;
    fs::write(&file_path, code).unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let module_ident = module_unique_ident(&project.root, &file_path.to_string_lossy());
    let module_index = project.build(&file_path.to_string_lossy(), &module_ident, Some(code.to_string())).await;

    let module_db = project.module_db.lock().unwrap();
    let m = &module_db[module_index];

    let has_error = m.analyzer_errors.iter().any(|e| e.message.contains("global string initializer must be empty"));
    assert!(
        has_error,
        "expected non-empty global string error, actual errors: {:?}",
        m.analyzer_errors.iter().map(|e| e.message.clone()).collect::<Vec<_>>()
    );
}
