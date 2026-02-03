use log::debug;
use nls::project::Project;
use ropey::Rope;

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
    // Initialize logger with error handling
    let _ = env_logger::builder().filter_level(log::LevelFilter::Debug).try_init();

    debug!("start test");

    let project_root = "/Users/weiwenhao/Code/nature-test";

    let mut project = Project::new(project_root.to_string()).await;
    project.backend_handle_queue();

    let module_ident = "nature-test.main";
    let file_path = "/Users/weiwenhao/Code/nature-test/main.n";

    // 使用 None 自动从文件读取内容进行编译
    let module_index = project.build(&file_path, &module_ident, None).await;
    dbg!(module_index);

    let mut module_db = project.module_db.lock().unwrap();
    let m = &mut module_db[module_index];
    println!("errors: {:?}", &m.analyzer_errors);
    drop(module_db);
}

#[tokio::test]
async fn test_stage() {
    // Initialize logger with error handling
    let _ = env_logger::builder().filter_level(log::LevelFilter::Debug).try_init();

    debug!("start test");

    let project_root = "/Users/weiwenhao/Code/nature-test";

    let mut project = Project::new(project_root.to_string()).await;
    project.backend_handle_queue();

    let module_ident = "nature-test.main";
    let file_path = "/Users/weiwenhao/Code/nature-test/main.n";

    // 定义阶段测试数据
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

    // 将 first phase 写入到 file path 中。
    std::fs::write(&file_path, test_codes[0].as_bytes()).expect("Failed to write first phase to file");

    // 循环执行各个阶段的测试
    for (index, code) in test_codes.iter().enumerate() {
        let phase_name = format!("Phase {}", index + 1);
        println!("{} build:", phase_name);

        let module_index = project.build(&file_path, &module_ident, Some(code.to_string())).await;
        dbg!(module_index);

        let mut module_db = project.module_db.lock().unwrap();
        let m = &mut module_db[module_index];
        println!("{} errors: {:?}", phase_name, &m.analyzer_errors);
        drop(module_db);
    }
}
