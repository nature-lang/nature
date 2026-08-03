//! Rule tests for the package module index, kept in sync with the compiler's src/module_index.c.

use nls::module_index::{import_module_path, module_ident_join, module_parent_path, package_unit_load, package_unit_reset, read_mod_decl, source_slot_key};
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

fn temp_package(name: &str) -> PathBuf {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_module_index_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write(root: &PathBuf, rel: &str, content: &str) {
    let path = root.join(rel);
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, content).unwrap();
}

#[test]
fn read_mod_decl_variants() {
    assert_eq!(read_mod_decl("mod codec\n"), Some("codec".to_string()));
    assert_eq!(read_mod_decl("\n\n// head\nmod codec\n"), Some("codec".to_string()));
    assert_eq!(read_mod_decl("/* block */ mod codec\n"), Some("codec".to_string()));
    assert_eq!(read_mod_decl("\u{feff}mod codec\n"), Some("codec".to_string()));
    assert_eq!(read_mod_decl("mod\tcodec\n"), Some("codec".to_string()));
    // the lexer drops inline block comments, so the header reader must behave the same
    assert_eq!(read_mod_decl("mod /* c */ codec\n"), Some("codec".to_string()));
    // a block comment spanning lines puts mod and ident on different lines
    assert_eq!(read_mod_decl("mod /* c\n */ codec\n"), None);

    // no mod declaration
    assert_eq!(read_mod_decl("fn main() {}\n"), None);
    // mod must be followed by a space or tab, modx is just a normal identifier
    assert_eq!(read_mod_decl("modx codec\n"), None);
    // a mod after an import is not a header
    assert_eq!(read_mod_decl("import fmt\nmod codec\n"), None);
}

#[test]
fn slot_key_strips_target_suffix() {
    assert_eq!(source_slot_key("/p/os/main.n"), Some("/p/os/main.n".to_string()));
    assert_eq!(source_slot_key("/p/os/main.darwin.n"), Some("/p/os/main.n".to_string()));
    assert_eq!(source_slot_key("/p/os/main.windows_amd64.n"), Some("/p/os/main.n".to_string()));
    // sub is not a valid os, so it stays part of the file name
    assert_eq!(source_slot_key("/p/os/seed.sub.n"), Some("/p/os/seed.sub.n".to_string()));
    assert_eq!(source_slot_key("/p/os/readme.md"), None);
}

#[test]
fn module_path_helpers() {
    assert_eq!(module_ident_join("app", ""), "app");
    assert_eq!(module_ident_join("app", "net.http"), "app.net.http");

    assert_eq!(import_module_path(&["app".to_string()]), "");
    assert_eq!(import_module_path(&["app".to_string(), "net".to_string(), "http".to_string()]), "net.http");

    assert_eq!(module_parent_path("net.http"), "net");
    assert_eq!(module_parent_path("net"), "");
}

#[test]
fn package_instances_have_distinct_internal_keys() {
    package_unit_reset();
    let left = temp_package("same_name_left");
    let right = temp_package("same_name_right");
    for root in [&left, &right] {
        write(root, "package.toml", "name = \"shared\"\n");
        write(root, "shared.n", "mod shared\n");
    }

    let left_unit = package_unit_load(left.to_str().unwrap(), "shared");
    let right_unit = package_unit_load(right.to_str().unwrap(), "shared");
    let left_root = left_unit.find_module("").unwrap();
    let right_root = right_unit.find_module("").unwrap();

    assert_eq!(left_root.module_ident, right_root.module_ident);
    assert_ne!(left_root.module_key, right_root.module_key);
}

#[cfg(unix)]
#[test]
fn directory_symlink_cycle_is_scanned_once() {
    use std::os::unix::fs::symlink;

    package_unit_reset();
    let root = temp_package("symlink_cycle");
    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "app.n", "mod app\n");
    write(&root, "sub/sub.n", "fn value():int { return 1 }\n");
    symlink(".", root.join("sub/loop")).unwrap();

    let unit = package_unit_load(root.to_str().unwrap(), "app");
    assert!(unit.errors.is_empty(), "errors: {:?}", unit.errors);
    assert_eq!(unit.modules.len(), 2);
}

#[test]
fn directory_module_merges_source_parts() {
    package_unit_reset();
    let root = temp_package("dir_module");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "main.n", "fn main() {}\n");
    write(&root, "app.n", "mod app\n\nfn root() {}\n");
    write(&root, "codec/encode.n", "mod codec\n\nfn encode() {}\n");
    write(&root, "codec/codec.n", "mod codec\n\nfn decode() {}\n");
    write(&root, "codec/helper.n", "fn helper() {}\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(pu.errors.is_empty(), "unexpected errors: {:?}", pu.errors);

    // app.n anchors the package-root module
    let root_module = pu.find_module("").expect("root module");
    assert_eq!(root_module.module_ident, "app");
    assert!(root_module.is_dir_module);
    assert_eq!(root_module.sources.len(), 1);

    // main.n has no mod, so it stays the standalone module app.main
    assert_eq!(pu.find_module("main").expect("app.main").module_ident, "app.main");

    // codec/codec.n anchors a two-part directory module
    let codec = pu.find_module("codec").expect("app.codec");
    assert_eq!(codec.module_ident, "app.codec");
    assert!(codec.is_dir_module);
    assert_eq!(codec.sources.len(), 2);

    // a file without mod in the same directory stays a standalone module
    assert_eq!(pu.find_module("codec.helper").expect("app.codec.helper").module_ident, "app.codec.helper");

    // siblings of a source part
    let encode = root.join("codec/encode.n").to_string_lossy().to_string();
    assert_eq!(pu.sibling_sources(&encode).len(), 1);
}

#[test]
fn parent_and_child_module_coexist() {
    package_unit_reset();
    let root = temp_package("parent_child");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "fmt.n", "fn name() {}\n");
    write(&root, "fmt/utils.n", "fn name() {}\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(pu.errors.is_empty(), "unexpected errors: {:?}", pu.errors);
    assert_eq!(pu.find_module("fmt").expect("app.fmt").module_ident, "app.fmt");
    assert_eq!(pu.find_module("fmt.utils").expect("app.fmt.utils").module_ident, "app.fmt.utils");
}

#[test]
fn file_and_directory_module_collision_is_reported() {
    package_unit_reset();
    let root = temp_package("collision");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "fmt.n", "fn name() {}\n");
    write(&root, "fmt/fmt.n", "mod fmt\n");
    write(&root, "fmt/print.n", "mod fmt\n\nfn print() {}\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert_eq!(pu.errors.len(), 1, "errors: {:?}", pu.errors);
    assert!(
        pu.errors[0].message.contains("is defined by two layouts"),
        "unexpected message: {}",
        pu.errors[0].message
    );
}

#[test]
fn mod_name_must_match_directory_or_package_name() {
    package_unit_reset();
    let root = temp_package("mod_name");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "bad_root.n", "mod wrong\n");
    write(&root, "sub/bad.n", "mod other\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert_eq!(pu.errors.len(), 2, "errors: {:?}", pu.errors);

    let messages: Vec<String> = pu.errors.iter().map(|e| e.message.clone()).collect();
    assert!(
        messages.iter().any(|m| m.contains("does not match package name 'app'")),
        "messages: {:?}",
        messages
    );
    assert!(
        messages.iter().any(|m| m.contains("does not match directory 'sub'")),
        "messages: {:?}",
        messages
    );
}

#[test]
fn nested_package_stops_the_scan() {
    package_unit_reset();
    let root = temp_package("nested");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "main.n", "fn main() {}\n");
    write(&root, "vendor/package.toml", "name = \"deep\"\n");
    // if vendor/ were scanned as a normal subdirectory, `mod deep` would error for not matching the directory name
    write(&root, "vendor/deep.n", "mod deep\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(pu.errors.is_empty(), "unexpected errors: {:?}", pu.errors);
    assert!(pu.find_module("vendor.inner").is_none());
}

#[test]
fn target_variants_share_one_slot() {
    package_unit_reset();
    let root = temp_package("variants");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "plat/plat.n", "mod plat\n");
    write(&root, "plat/target.n", "mod plat\n");
    write(&root, "plat/target.linux.n", "mod plat\n");
    write(&root, "plat/target.darwin.n", "mod plat\n");
    write(&root, "plat/target.windows.n", "mod plat\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(pu.errors.is_empty(), "unexpected errors: {:?}", pu.errors);

    // the target suffix never enters the ModuleId, 4 variants contribute a single active source
    let plat = pu.find_module("plat").expect("app.plat");
    assert_eq!(plat.sources.len(), 2, "sources: {:?}", plat.sources);
}

#[test]
fn target_variants_must_agree_on_membership() {
    package_unit_reset();
    let root = temp_package("variant_mismatch");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "plat/plat.n", "mod plat\n");
    write(&root, "plat/target.n", "fn target() {}\n");
    write(&root, "plat/target.linux.n", "mod plat\n");
    write(&root, "plat/target.darwin.n", "mod plat\n");
    write(&root, "plat/target.windows.n", "mod plat\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(!pu.errors.is_empty());
    assert!(
        pu.errors.iter().any(|e| e.message.contains("must belong to the same module")),
        "errors: {:?}",
        pu.errors
    );
}

#[test]
fn only_single_file_directory_module_may_omit_mod() {
    package_unit_reset();
    let root = temp_package("single_file");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "single/single.n", "fn value() {}\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(pu.errors.is_empty(), "unexpected errors: {:?}", pu.errors);
    assert_eq!(pu.find_module("single").expect("app.single").module_ident, "app.single");

    package_unit_reset();
    write(&root, "single/part.n", "mod single\n");
    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(
        pu.errors.iter().any(|error| error.message.contains("requires 'mod single'")),
        "errors: {:?}",
        pu.errors
    );
}

#[test]
fn directory_module_requires_same_named_entry() {
    package_unit_reset();
    let root = temp_package("missing_entry");

    write(&root, "package.toml", "name = \"app\"\n");
    write(&root, "codec/encode.n", "mod codec\n");

    let pu = package_unit_load(root.to_str().unwrap(), "app");
    assert!(
        pu.errors.iter().any(|error| error.message.contains("must have entry 'codec.n'")),
        "errors: {:?}",
        pu.errors
    );
}
