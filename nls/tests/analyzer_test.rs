use log::debug;
use nls::analyzer::module_unique_ident;
use nls::project::Project;
use ropey::Rope;
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

/// Create a unique temp directory for a test case.
fn temp_project(name: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let dir = std::env::temp_dir().join(format!("nls_analyzer_test_{}_{}", name, nanos));
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn test_rope() {
    let text = "你好\n世界"; // 8个字节(你=3字节,好=3字节,\n=1字节,世=3字节,界=3字节)
    let rope = Rope::from_str(text);

    assert_eq!(rope.try_byte_to_line(0).unwrap(), 0); // 第一行
    assert_eq!(rope.try_byte_to_line(7).unwrap(), 1); // 第二行
    assert!(rope.try_byte_to_line(14).is_err()); // 超出范围，应该返回错误
}

#[tokio::test]
async fn test_project() {
    let _ = env_logger::builder().filter_level(log::LevelFilter::Debug).try_init();
    debug!("start test");

    let root = temp_project("test_project");
    let file = root.join("main.n");
    let code = "fn main() {}\n";
    fs::write(&file, code).unwrap();

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    project.start_queue_worker();

    let module_ident = module_unique_ident(&project.root, &file.to_string_lossy());
    let module_index = project
        .build(&file.to_string_lossy(), &module_ident, Some(code.to_string()))
        .await;
    dbg!(module_index);

    let module_db = project.module_db.lock().unwrap();
    let m = &module_db[module_index];
    println!("errors: {:?}", &m.analyzer_errors);
    assert!(m.analyzer_errors.is_empty(), "empty main should have no errors");
}

#[tokio::test]
async fn test_stage() {
    let _ = env_logger::builder().filter_level(log::LevelFilter::Debug).try_init();
    debug!("start test");

    let root = temp_project("test_stage");
    let file = root.join("main.n");

    let mut project = Project::new(root.to_string_lossy().to_string()).await;
    project.start_queue_worker();

    let module_ident = module_unique_ident(&project.root, &file.to_string_lossy());

    // Stage test data — each phase is built sequentially.
    let test_codes = vec![
        r#"
type addr1_t = struct{
    string ip
    int port
}

type addr2_t = struct{
    string ip
    int port
}

fn main() {
    var a1 = addr1_t{
        ip = '1.1.1.1',
        port= 22,
    }

    addr2_t a2 = a1 as addr2_t
}
        "#,
    ];

    // Write the first phase to disk so the build can read it.
    fs::write(&file, test_codes[0].as_bytes()).expect("Failed to write first phase to file");

    for (index, code) in test_codes.iter().enumerate() {
        let phase_name = format!("Phase {}", index + 1);
        println!("{phase_name} build:");

        let module_index = project
            .build(&file.to_string_lossy(), &module_ident, Some(code.to_string()))
            .await;
        dbg!(module_index);

        let module_db = project.module_db.lock().unwrap();
        let m = &module_db[module_index];
        println!("{phase_name} errors: {:?}", &m.analyzer_errors);
    }
}
