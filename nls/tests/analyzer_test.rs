use nls::project::Project;
use ropey::Rope;

#[test]
fn test_rope() {
    let text = "你好\n世界"; // 8个字节(你=3字节,好=3字节,\n=1字节,世=3字节,界=3字节)
    let rope = Rope::from_str(text);

    assert_eq!(rope.try_byte_to_line(0).unwrap(), 0);  // 第一行
    assert_eq!(rope.try_byte_to_line(7).unwrap(), 1);  // 第二行
    assert!(rope.try_byte_to_line(14).is_err());       // 超出范围，应该返回错误
}


#[tokio::test]
async fn test_project() {
    env_logger::init();

    let project_root = "/Users/weiwenhao/Code/nature-test";

    let mut project = Project::new(project_root.to_string()).await;
    project.backend_handle_queue();

    let module_ident = "nature-test.main";
    let file_path = "/Users/weiwenhao/Code/nature-test/main.n";

    let module_index = project.build(&file_path, &module_ident, None).await;
    dbg!(module_index);

    let mut module_db = project.module_db.lock().unwrap();
    let m = &mut module_db[module_index];
    dbg!(&m.analyzer_errors);
}