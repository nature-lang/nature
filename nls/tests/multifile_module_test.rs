//! Behaviour of multi-file directory modules on the NLS side:
//! - every source part of a module shares the module scope and top-level declarations
//! - imports/aliases only take effect in the file declaring them
//! - a mod declaration is validated against package.toml.name / the directory basename

use nls::module_index::package_unit_reset;
use nls::project::Project;
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

fn temp_project(name: &str) -> PathBuf {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_multifile_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write(root: &PathBuf, rel: &str, content: &str) -> String {
    let path = root.join(rel);
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(&path, content).unwrap();
    path.to_string_lossy().to_string()
}

async fn build(root: &PathBuf, entry: &str) -> (Project, usize) {
    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let path = root.join(entry).to_string_lossy().to_string();
    let content = fs::read_to_string(&path).unwrap();
    let idx = project.build(&path, "", Some(content)).await;
    (project, idx)
}

fn errors_of(project: &Project, idx: usize) -> Vec<String> {
    let db = project.module_db.lock().unwrap();
    db[idx].analyzer_errors.iter().map(|e| e.message.clone()).collect()
}

/// Return the diagnostics of the module corresponding to a file
fn errors_of_path(project: &Project, path: &str) -> Vec<String> {
    let handled = project.module_handled.lock().unwrap();
    let Some(&idx) = handled.get(path) else {
        return Vec::new();
    };
    drop(handled);

    let db = project.module_db.lock().unwrap();
    db[idx].analyzer_errors.iter().map(|e| e.message.clone()).collect()
}

#[tokio::test]
async fn cross_part_declarations_are_shared() {
    package_unit_reset();
    let root = temp_project("cross_part");

    // avoid builtins (println and friends) so the test does not depend on NATURE_ROOT
    write(&root, "package.toml", "name = \"app\"\nversion = \"1.0.0\"\ntype = \"bin\"\n");
    write(&root, "codec/encode.n", "mod codec\n\nfn encode(int v):int {\n    return v * scale()\n}\n");
    write(&root, "codec/decode.n", "mod codec\n\nfn scale():int {\n    return 2\n}\n");
    write(&root, "main.n", "import app.codec\n\nfn main() {\n    int v = codec.encode(3)\n}\n");

    let (project, idx) = build(&root, "main.n").await;

    // a cross-part forward reference must not error
    assert!(errors_of(&project, idx).is_empty(), "main errors: {:?}", errors_of(&project, idx));

    let encode_path = root.join("codec/encode.n").to_string_lossy().to_string();
    assert!(
        errors_of_path(&project, &encode_path).is_empty(),
        "encode.n errors: {:?}",
        errors_of_path(&project, &encode_path)
    );

    // the two parts share one module ident and scope
    let db = project.module_db.lock().unwrap();
    let parts: Vec<&nls::project::Module> = db.iter().filter(|m| m.ident == "app.codec").collect();
    assert_eq!(parts.len(), 2, "expect 2 source parts, got {}", parts.len());
    assert_eq!(parts[0].scope_id, parts[1].scope_id);
}

#[tokio::test]
async fn import_alias_does_not_leak_between_parts() {
    package_unit_reset();
    let root = temp_project("alias_leak");

    write(&root, "package.toml", "name = \"app\"\nversion = \"1.0.0\"\ntype = \"bin\"\n");
    write(
        &root,
        "codec/encode.n",
        "mod codec\n\nimport fmt\n\nfn encode():string {\n    return fmt.sprintf('a')\n}\n",
    );
    let decode_path = write(&root, "codec/decode.n", "mod codec\n\nfn decode():string {\n    return fmt.sprintf('b')\n}\n");
    write(&root, "main.n", "import app.codec\n\nfn main() {\n    println(codec.decode())\n}\n");

    let (project, _) = build(&root, "main.n").await;

    let decode_errors = errors_of_path(&project, &decode_path);
    assert!(
        decode_errors.iter().any(|m| m.contains("fmt")),
        "expect decode.n to reject the alias from encode.n, got: {:?}",
        decode_errors
    );
}

#[tokio::test]
async fn mod_name_mismatch_is_reported() {
    package_unit_reset();
    let root = temp_project("mod_mismatch");

    write(&root, "package.toml", "name = \"app\"\nversion = \"1.0.0\"\ntype = \"bin\"\n");
    write(&root, "codec/encode.n", "mod wrong\n\nfn encode():int {\n    return 1\n}\n");
    write(&root, "main.n", "fn main() {\n}\n");

    let encode_path = root.join("codec/encode.n").to_string_lossy().to_string();
    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let content = fs::read_to_string(&encode_path).unwrap();
    let idx = project.build(&encode_path, "", Some(content)).await;

    let errors = errors_of(&project, idx);
    assert!(errors.iter().any(|m| m.contains("does not match directory 'codec'")), "errors: {:?}", errors);
}

#[tokio::test]
async fn mod_requires_package_toml() {
    package_unit_reset();
    let root = temp_project("mod_no_package");

    let main_path = write(&root, "main.n", "mod something\n\nfn main() {\n}\n");

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    let content = fs::read_to_string(&main_path).unwrap();
    let idx = project.build(&main_path, "", Some(content)).await;

    let errors = errors_of(&project, idx);
    assert!(
        errors.iter().any(|m| m.contains("cannot use 'mod' without package.toml")),
        "errors: {:?}",
        errors
    );
}

#[tokio::test]
async fn importing_a_source_part_is_rejected() {
    package_unit_reset();
    let root = temp_project("import_part");

    write(&root, "package.toml", "name = \"app\"\nversion = \"1.0.0\"\ntype = \"bin\"\n");
    write(&root, "codec/encode.n", "mod codec\n\nfn encode():int {\n    return 1\n}\n");
    write(&root, "codec/decode.n", "mod codec\n\nfn decode():int {\n    return 2\n}\n");
    write(&root, "main.n", "import app.codec.encode\n\nfn main() {\n}\n");

    let (project, idx) = build(&root, "main.n").await;

    let errors = errors_of(&project, idx);
    assert!(
        errors.iter().any(|m| m.contains("is not a module, it is part of app.codec")),
        "errors: {:?}",
        errors
    );
}

#[tokio::test]
async fn module_ident_uses_package_name_not_directory_name() {
    package_unit_reset();
    // the physical directory name differs from the package name
    let root = temp_project("phys_dir");

    write(&root, "package.toml", "name = \"renamed\"\nversion = \"1.0.0\"\ntype = \"bin\"\n");
    let main_path = write(&root, "main.n", "fn main() {\n}\n");
    write(&root, "root_part.n", "mod renamed\n\nfn root():int {\n    return 1\n}\n");

    let project = Project::new(root.to_string_lossy().to_string()).await;

    assert_eq!(project.module_ident_of(&main_path), "renamed.main");

    let root_part = root.join("root_part.n").to_string_lossy().to_string();
    assert_eq!(project.module_ident_of(&root_part), "renamed");
}
