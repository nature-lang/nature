use crate::analyzer::common::{AnalyzerError, AstFnDef, AstNode, ImportStmt, PackageConfig, Stmt};
use crate::analyzer::flow::Flow;
use crate::analyzer::lexer::{Lexer, Token};
use crate::analyzer::semantic::Semantic;
use crate::analyzer::symbol::{NodeId, SymbolTable};
use crate::analyzer::syntax::Syntax;
use crate::analyzer::typesys::Typesys;
use crate::analyzer::{analyze_imports, register_global_symbol};
use crate::package::parse_package;
use log::debug;
use ropey::Rope;
use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::sync::{Arc, Mutex};

pub const DEFAULT_NATURE_ROOT: &str = "/usr/local/nature";
// pub const DEFAULT_NATURE_ROOT: &str = "/Users/weiwenhao/Code/nature";

// 单个文件称为 module, package 通常是包含 package.toml 的多级目录
#[derive(Debug, Clone)]
pub struct Module {
    pub index: usize,
    pub ident: String,
    pub source: String, //  源码内容
    pub rope: Rope,
    pub path: String, // 文件 路径
    pub dir: String,  //  文件 所在目录
    pub token_db: Vec<Token>,
    pub token_indexes: Vec<usize>,
    pub sem_token_db: Vec<Token>,
    pub stmts: Vec<Box<Stmt>>,
    pub global_vardefs: Vec<AstNode>,
    pub global_fndefs: Vec<Arc<Mutex<AstFnDef>>>,
    pub all_fndefs: Vec<Arc<Mutex<AstFnDef>>>, // 包含 global 和 local fn def
    pub analyzer_errors: Vec<AnalyzerError>,

    pub scope_id: NodeId, // 当前 module 对应的 scope

    pub references: Vec<usize>,        // 哪些模块依赖于当前模块
    pub dependencies: Vec<ImportStmt>, // 当前模块依赖 哪些模块
}

impl Module {
    pub fn new(ident: String, source: String, path: String, index: usize, scope_id: NodeId) -> Self {
        // 计算 module ident, 和 analyze_import 中的 import.module_ident 需要采取相同的策略

        let dir = Path::new(&path).parent().and_then(|p| p.to_str()).unwrap_or("").to_string();

        let rope = ropey::Rope::from_str(&source);

        // create module scope
        Self {
            index,
            ident,
            source,
            path,
            dir,
            rope,
            scope_id,
            token_db: Vec::new(),
            token_indexes: Vec::new(),
            sem_token_db: Vec::new(),
            stmts: Vec::new(),
            global_vardefs: Vec::new(),
            global_fndefs: Vec::new(),
            all_fndefs: Vec::new(),
            analyzer_errors: Vec::new(),
            references: Vec::new(),
            dependencies: Vec::new(),
        }
    }

    pub fn need_rebuild(&self) -> bool {
        // 读取文件最新内容
        if let Ok(content) = std::fs::read_to_string(&self.path) {
            // 如果内容发生变化，需要重新构建
            return content != self.source;
        }
        false
    }
}

impl Default for Module {
    fn default() -> Self {
        Self {
            scope_id: 0,
            index: 0,
            ident: "".to_string(),
            source: "".to_string(),
            path: "".to_string(),
            dir: "".to_string(),
            references: Vec::new(),
            dependencies: Vec::new(),
            token_db: Vec::new(),
            token_indexes: Vec::new(),
            sem_token_db: Vec::new(),
            stmts: Vec::new(),
            global_vardefs: Vec::new(),
            global_fndefs: Vec::new(),
            all_fndefs: Vec::new(),
            analyzer_errors: Vec::new(),
            rope: Rope::default(),
        }
    }
}

#[derive(Debug, Clone)]
pub struct QueueItem {
    pub path: String,
    pub notify: Option<String>, //  编译完成后需要通知的 module
}

#[derive(Debug, Clone)]
pub struct Project {
    pub nature_root: String,
    pub root: String,
    pub module_db: Arc<Mutex<Vec<Module>>>,                 // key = uri, 记录所有已经编译的 module
    pub module_handled: Arc<Mutex<HashMap<String, usize>>>, // key = path, 记录所有已经编译的 module, usize 指向 module db
    // queue 中的每一个 module 都可以视为 main.n 来编译，主要是由于用户打开文件 A import B or C 产生的 B 和 C 注册到 queue 中进行处理
    pub queue: Arc<Mutex<Vec<QueueItem>>>,
    pub package_config: Arc<Mutex<Option<PackageConfig>>>, // 当前 project 如果包含 package.toml, 则可以解析出 package_config 等信息，import 需要借助该信息进行解析
    pub symbol_table: Arc<Mutex<SymbolTable>>,
}

impl Project {
    pub async fn new(project_root: String) -> Self {
        // 1. check nature root by env
        let nature_root = std::env::var("NATURE_ROOT").unwrap_or(DEFAULT_NATURE_ROOT.to_string());

        let mut builtin_list: Vec<String> = Vec::new();

        // 3. builtin package load by nature root std
        let std_dir = Path::new(&nature_root).join("std");
        let std_builtin_dir = std_dir.join("builtin");

        // 加载 builtin 中的所有文件(.n 结尾)
        let dirs = std::fs::read_dir(std_builtin_dir).unwrap();
        for dir in dirs {
            let dir_path = dir.unwrap().path();
            if !dir_path.is_file() {
                continue;
            }

            let file_name = dir_path.file_name().unwrap().to_str().unwrap();
            if !file_name.ends_with(".n") {
                continue;
            }

            // 将 完整文件路径加入到 await_queue 中,  进行异步解析
            let file_path = dir_path.to_str().unwrap();

            builtin_list.push(file_path.to_string());
        }

        let package_path = Path::new(&project_root).join("package.toml").to_str().unwrap().to_string();
        let package_config = match parse_package(&package_path) {
            Ok(package_config) => Arc::new(Mutex::new(Some(package_config))),
            Err(_e) => Arc::new(Mutex::new(None)),
        };

        let mut project = Self {
            nature_root,
            root: project_root,
            module_db: Arc::new(Mutex::new(Vec::new())),
            module_handled: Arc::new(Mutex::new(HashMap::new())),
            queue: Arc::new(Mutex::new(Vec::new())),
            package_config,
            symbol_table: Arc::new(Mutex::new(SymbolTable::new())),
        };

        // handle builtin list
        for file_path in builtin_list {
            project.build(&file_path, "", None).await;
        }

        return project;
    }

    pub fn backend_handle_queue(&self) {
        let mut self_clone = self.clone();
        tokio::spawn(async move {
            self_clone.handle_queue().await;
        });
    }

    pub async fn need_build(&self, item: &QueueItem) -> bool {
        let module_handled = self.module_handled.lock().unwrap();
        let index_option = module_handled.get(&item.path);
        if let Some(index) = index_option {
            let module_db = self.module_db.lock().unwrap();
            let m = &module_db[*index];
            return m.need_rebuild();
        } else {
            return true;
        }
    }

    pub async fn handle_queue(&mut self) {
        loop {
            // 尝试从 await_queue 中获取一个 file, 当前语句结束之后，await_queue 会自动解锁
            let item_option = self.queue.lock().unwrap().pop();
            if item_option.is_none() {
                tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
                continue;
            }

            let item = item_option.unwrap();

            if !self.need_build(&item).await {
                continue;
            }

            // build
            self.build(&item.path, "", None).await;
        }
    }

    /**
     * 当前 module 更新后，需要更新所有依赖了当前 module 的 module
     */
    pub fn all_references(&self, index: usize) -> Vec<String> {
        let mut handled: HashSet<usize> = HashSet::new();
        let mut result: Vec<String> = Vec::new();

        let mut worklist: Vec<usize> = Vec::new();
        worklist.push(index);

        while let Some(current_index) = worklist.pop() {
            // 如果已经处理过该模块，则跳过
            if handled.contains(&current_index) {
                continue;
            }
            // 标记该模块已处理
            handled.insert(current_index);

            let module_db = self.module_db.lock().unwrap();
            let current_module = &module_db[current_index];

            // 遍历所有引用当前模块的模块
            for &ref_index in &current_module.references {
                let ref_module = &module_db[ref_index];
                result.push(ref_module.path.clone());

                // 将引用模块加入工作列表，以便递归查找
                worklist.push(ref_index);
            }
        }

        result
    }

    pub async fn build(&mut self, main_path: &str, module_ident: &str, content_option: Option<String>) -> usize {
        // 所有未编译的 import 模块, 都需要进行关联处理
        let mut worklist: Vec<ImportStmt> = Vec::new();
        let mut handled: HashSet<String> = HashSet::new();
        let mut module_indexes = Vec::new();

        // 创建一个简单的 import 语句作为起始点
        let mut main_import = ImportStmt::default();
        main_import.full_path = main_path.to_string();
        main_import.module_ident = module_ident.to_string();

        worklist.push(main_import);
        handled.insert(main_path.to_string());

        debug!("{} handle work list", main_path);
        while let Some(import_stmt) = worklist.pop() {
            let module_handled = self.module_handled.lock().unwrap();
            let index_option = module_handled.get(&import_stmt.full_path).copied();
            drop(module_handled);

            let index: usize = if let Some(i) = index_option {
                // 如果 import module 已经存在 module 则不需要进行重复编译, main path module 则进行强制更新
                if import_stmt.full_path == main_path {
                    // 需要更新现有模块的内容(也就是当前文件)
                    if let Some(ref content) = content_option {
                        let mut module_db = self.module_db.lock().unwrap();
                        let m = &mut module_db[i];
                        m.source = content.clone();
                        m.rope = ropey::Rope::from_str(&m.source);
                    }
                    i.clone()
                } else {
                    continue;
                }
            } else {
                let content_option = std::fs::read_to_string(import_stmt.full_path.clone());
                if content_option.is_err() {
                    continue;
                }
                let content = content_option.unwrap();

                // push to module_db get index lock
                let mut module_handled = self.module_handled.lock().unwrap();
                let mut module_db = self.module_db.lock().unwrap();
                let index = module_db.len();

                // create module scope
                let scope_id = self.symbol_table.lock().unwrap().create_module_scope(import_stmt.module_ident.clone());

                let temp = Module::new(import_stmt.module_ident, content, import_stmt.full_path.to_string(), index, scope_id);
                module_db.push(temp);

                // set module_handled
                module_handled.insert(import_stmt.full_path, index);
                // unlock

                index
            };

            let mut module_db = self.module_db.lock().unwrap();
            let m = &mut module_db[index];

            debug!("build, m.path {}, module_dir {}, project root {}", m.path, m.dir, self.root);

            // clean module symbol table
            self.symbol_table.lock().unwrap().clean_module_scope(m.ident.clone());

            // - lexer
            let (token_db, token_indexes, lexer_errors) = Lexer::new(m.source.clone()).scan();
            m.token_db = token_db.clone();
            m.token_indexes = token_indexes.clone();
            m.analyzer_errors = lexer_errors; // 清空 error 从 analyzer 起重新计算

            // - parser
            let (mut stmts, sem_token_db, syntax_errors) = Syntax::new(m.clone(), token_db, token_indexes).parser();
            m.sem_token_db = sem_token_db.clone();
            // m.stmts = stmts;
            m.analyzer_errors.extend(syntax_errors);

            // collection all relation module
            module_indexes.push(index);

            // analyzer global ast to symbol table
            let mut symbol_table = self.symbol_table.lock().unwrap();
            register_global_symbol(m, &mut symbol_table, &stmts);
            drop(symbol_table);

            // analyzer imports to worklist
            let imports = analyze_imports(self.root.clone(), &self.package_config, m, &mut stmts);
            m.stmts = stmts;

            let mut filter_imports: Vec<ImportStmt> = Vec::new();

            // import to worklist
            for import in imports {
                // handle 重复进入表示 build module 发生了循环引用, 发送错误并跳过该 import 处理。
                if handled.contains(&import.full_path) {
                    // TODO 暂时不做处理
                    // errors_push(
                    //     m,
                    //     AnalyzerError {
                    //         start: import.start,
                    //         end: import.end,
                    //         message: format!("circular import"),
                    //     },
                    // );

                    dbg!("circular import");
                    // continue;
                }

                filter_imports.push(import.clone());
                worklist.push(import.clone());
                handled.insert(import.full_path.clone());
            }
            drop(module_db);

            // to module dep, and call diff (add and remove)
            self.update_module_dep(index, filter_imports);
        }

        debug!("{} will semantic handle, module_indexes: {:?}", main_path, &module_indexes);

        for index in module_indexes.clone() {
            let mut module_db = self.module_db.lock().unwrap();
            let mut symbol_table = self.symbol_table.lock().unwrap();
            let m = &mut module_db[index];
            m.all_fndefs = Vec::new();
            Semantic::new(m, &mut symbol_table).analyze();
        }

        // all pre infer
        for index in module_indexes.clone() {
            let mut module_db = self.module_db.lock().unwrap();
            let mut symbol_table = self.symbol_table.lock().unwrap();
            let m = &mut module_db[index];
            let errors = Typesys::new(&mut symbol_table, m).pre_infer();
            m.analyzer_errors.extend(errors);
        }

        for index in module_indexes.clone() {
            let mut module_db = self.module_db.lock().unwrap();
            let mut symbol_table = self.symbol_table.lock().unwrap();
            let m = &mut module_db[index];
            let mut errors = Typesys::new(&mut symbol_table, m).infer();
            m.analyzer_errors.extend(errors);

            // check returns
            errors = Flow::new(m).analyze();
            m.analyzer_errors.extend(errors);
        }

        // handle all refers
        let module_handled = self.module_handled.lock().unwrap();
        let index_option = module_handled.get(main_path);
        let main_index = index_option.unwrap();

        let refers = self.all_references(main_index.clone());

        // refers push to queue
        for refer in refers {
            self.queue.lock().unwrap().push(QueueItem {
                path: refer,
                notify: Some(main_path.to_string()), // 当引用模块编译完成后，通知主模块重新编译(主模块必定已经注册完成，包含完整的 module 信息)
            });
        }

        return *main_index;
    }

    /**
     * 更新 module 的依赖, 尤其是反向依赖的 references 更新
     */
    pub fn update_module_dep(&mut self, module_index: usize, imports: Vec<ImportStmt>) {
        // 当前模块依赖这些目标模块，这些目标模块的 refers 需要进行相应的更新
        let mut dependency_indices = Vec::new();
        {
            let module_handled = self.module_handled.lock().unwrap();
            for import in &imports {
                if let Some(&index) = module_handled.get(&import.full_path) {
                    dependency_indices.push(index);
                }
            }
        }
        // 然后处理 module_db
        let mut module_db = self.module_db.lock().unwrap();
        let m = &mut module_db[module_index];

        // 将当前的依赖转换为 HashSet
        let old_deps: HashSet<String> = m.dependencies.iter().map(|dep| dep.full_path.clone()).collect();
        let new_deps: HashSet<String> = imports.iter().map(|dep| dep.full_path.clone()).collect();

        // old deps 存在，但是 new deps 中删除的数据
        for removed_dep in old_deps.difference(&new_deps) {
            if let Some(dep_index) = dependency_indices.iter().find(|&&i| module_db[i].path == *removed_dep) {
                let dep_module = &mut module_db[*dep_index];
                dep_module.references.retain(|&x| x != module_index);
            }
        }

        // 更新当前模块的依赖列表
        module_db[module_index].dependencies = imports.clone();

        // 添加反向引用关系
        for import in imports {
            if let Some(dep_index) = dependency_indices.iter().find(|&&i| module_db[i].path == import.full_path) {
                let dep_module = &mut module_db[*dep_index];
                if !dep_module.references.contains(&module_index) {
                    dep_module.references.push(module_index);
                }
            }
        }
    }
}
