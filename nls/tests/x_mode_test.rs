//! x mode on the NLS side: the mode comes from the source extension, so a `.x` file has to be
//! analyzable at all and every fn declared in it has to carry `is_x`. Without both, the cross
//! mode call diagnostic ported from `src/semantic/infer.c` can never fire.
//!
//! These cases deliberately use no builtin, so they do not depend on NATURE_ROOT pointing at
//! this tree rather than an installed one.

use nls::module_index::package_unit_reset;
use nls::project::Project;
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

fn temp_project(name: &str) -> PathBuf {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_x_mode_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write(root: &PathBuf, rel: &str, content: &str) {
    fs::write(root.join(rel), content).unwrap();
}

/// Analyze `entry` inside a throwaway project and return its diagnostics.
async fn errors(name: &str, entry: &str, files: &[(&str, &str)]) -> Vec<String> {
    let root = temp_project(name);
    for (rel, content) in files {
        write(&root, rel, content);
    }

    package_unit_reset();
    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let path = root.join(entry).to_string_lossy().to_string();
    let content = fs::read_to_string(&path).unwrap();
    let idx = project.build(&path, "", Some(content)).await;

    let db = project.module_db.lock().unwrap();
    db[idx].analyzer_errors.iter().map(|e| e.message.clone()).collect()
}

#[tokio::test]
async fn test_x_file_is_analyzed() {
    // a plain .x entry must produce no diagnostics, which means it was parsed and analyzed
    // rather than skipped for having the wrong extension
    let errs = errors(
        "x_analyzed",
        "main.x",
        &[("main.x", "fn main() {\n    var v = 1 + 1\n}\n")],
    )
    .await;

    assert!(errs.is_empty(), "expected a clean .x module, actual: {:?}", errs);
}

#[tokio::test]
async fn test_x_calling_n_is_rejected() {
    let errs = errors(
        "x_calls_n",
        "main.x",
        &[
            ("main.x", "import \"util.n\"\n\nfn main() {\n    util.helper()\n}\n"),
            ("util.n", "pub fn helper() {}\n"),
        ],
    )
    .await;

    assert!(
        errs.iter().any(|e| e.contains("calling .n fn 'helper' from .x fn 'main' is not allowed")),
        "expected the cross mode call diagnostic, actual: {:?}",
        errs
    );
}

#[tokio::test]
async fn test_x_calling_x_is_allowed() {
    // the negative control for the case above: same shape, both sides .x
    let errs = errors(
        "x_calls_x",
        "main.x",
        &[
            ("main.x", "import \"util.x\"\n\nfn main() {\n    util.helper()\n}\n"),
            ("util.x", "pub fn helper() {}\n"),
        ],
    )
    .await;

    assert!(errs.is_empty(), "expected x to x calls to pass, actual: {:?}", errs);
}

#[tokio::test]
async fn test_n_calling_x_is_allowed() {
    let errs = errors(
        "n_calls_x",
        "main.n",
        &[
            ("main.n", "import \"util.x\"\n\nfn main() {\n    util.helper()\n}\n"),
            ("util.x", "pub fn helper() {}\n"),
        ],
    )
    .await;

    assert!(errs.is_empty(), "expected n to x calls to pass, actual: {:?}", errs);
}
