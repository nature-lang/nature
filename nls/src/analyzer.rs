pub mod common;
pub mod lexer; // 声明子模块
pub mod semantic;
pub mod symbol;
pub mod syntax;
pub mod typesys;

use std::path::Path;

use crate::package::parse_package;
use crate::project::{Module, DEFAULT_NATURE_ROOT};
use crate::utils::{errors_push, format_global_ident};
use common::{AnalyzerError, AstNode, ImportStmt, PackageConfig, Stmt};
use lazy_static::lazy_static;
use log::debug;
use std::collections::HashSet;
use std::env;
use std::fs;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use symbol::SymbolKind;
use symbol::SymbolTable;

// 在文件顶部添加
#[cfg(target_os = "linux")]
const TARGET_OS: &str = "linux";
#[cfg(target_os = "macos")]
const TARGET_OS: &str = "darwin";

#[cfg(target_arch = "x86_64")]
const TARGET_ARCH: &str = "amd64";
#[cfg(target_arch = "aarch64")]
const TARGET_ARCH: &str = "arm64";

#[cfg(target_arch = "riscv64")]
const TARGET_ARCH: &str = "riscv64";

lazy_static! {
    static ref STD_PACKAGES: Mutex<Option<HashSet<String>>> = Mutex::new(None);
}
const PACKAGE_SOURCE_INFIX: &str = ".nature/package/sources";
const PACKAGE_TOML: &str = "package.toml";

fn dep_package_dir() -> PathBuf {
    let home = env::var("HOME").expect("cannot find home dir");
    PathBuf::from(home).join(PACKAGE_SOURCE_INFIX)
}

fn package_dep_git_dir(package_config: &PackageConfig, package: &str) -> String {
    let package_dir = dep_package_dir();
    let dep_data = &package_config.package_data.dependencies[package];

    let mut url = dep_data.url.as_ref().unwrap().replace('/', ".");
    let version = dep_data.version.replace('/', ".");
    url = format!("{}@{}", url, version);

    package_dir.join(url).to_str().unwrap().to_string()
}

fn package_dep_local_dir(package_config: &PackageConfig, package: &str) -> String {
    let package_dir = dep_package_dir();
    let dep_data = &package_config.package_data.dependencies[package];

    let mut path = package.replace("/", ".");
    let version = dep_data.version.replace("/", ".");
    path = format!("{}@{}", path, version);

    package_dir.join(path).to_str().unwrap().to_string()
}

fn is_std_package(package: &str) -> bool {
    let mut std_packages = STD_PACKAGES.lock().unwrap();

    // 如果已经初始化过，直接检查包是否存在
    if let Some(packages) = std_packages.as_ref() {
        return packages.contains(package);
    }

    // 首次调用时初始化
    let mut packages = HashSet::new();

    // 扫描 std 目录
    let std_dir = Path::new(&std::env::var("NATURE_ROOT").unwrap_or(DEFAULT_NATURE_ROOT.to_string())).join("std");

    if let Ok(entries) = fs::read_dir(&std_dir) {
        for entry in entries {
            if let Ok(entry) = entry {
                if let Ok(file_type) = entry.file_type() {
                    if file_type.is_dir() {
                        if let Some(dirname) = entry.file_name().to_str() {
                            // 排除特殊目录
                            if ![".", "..", "builtin"].contains(&dirname) {
                                packages.insert(dirname.to_string());
                            }
                        }
                    }
                }
            }
        }
    }

    // 保存结果并返回
    *std_packages = Some(packages);
    std_packages.as_ref().unwrap().contains(package)
}

fn is_dep_package(package_config: &PackageConfig, package: &str) -> bool {
    // 检查 dependencies 中是否包含指定的 package
    package_config.package_data.dependencies.contains_key(package)
}

fn is_current_package(package_config: &PackageConfig, package: &str) -> bool {
    // 检查包名是否与当前包名一致
    package_config.package_data.name == package
}

fn analyze_import_dep(package_config: &PackageConfig, _m: &mut Module, import: &mut ImportStmt) -> Result<(), AnalyzerError> {
    let package_ident = import.ast_package.as_ref().unwrap()[0].clone();
    let dep_data = &package_config.package_data.dependencies[&package_ident];

    // 根据依赖类型获取包目录
    let package_dir = match dep_data.dep_type.as_str() {
        "git" => package_dep_git_dir(package_config, &package_ident),
        "local" => package_dep_local_dir(package_config, &package_ident),
        _ => {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("{} not found", package_ident),
            });
        }
    };

    // join package.toml and must exists
    let package_conf_path = Path::new(&package_dir).join("package.toml");
    if !package_conf_path.exists() {
        return Err(AnalyzerError {
            start: import.start,
            end: import.end,
            message: format!("{} not found", package_conf_path.display()),
        });
    }

    match parse_package(package_conf_path.to_str().unwrap()) {
        Ok(package_conf) => {
            // 设置导入信息
            import.use_links = true;
            import.package_dir = package_dir;
            import.package_conf = Some(package_conf);
            return Ok(());
        }
        Err(e) => {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import failed: {} {}", package_conf_path.display(), e.message),
            })
        }
    }
}

fn analyze_import_std(_m: &mut Module, import: &mut ImportStmt) -> Result<(), AnalyzerError> {
    let package_ident = import.ast_package.as_ref().unwrap()[0].clone();

    // 获取标准库目录
    let std_dir = Path::new(&std::env::var("NATURE_ROOT").unwrap_or(DEFAULT_NATURE_ROOT.to_string()))
        .join("std")
        .join(&package_ident);

    // 检查 package.toml 是否存在
    let package_conf_path = std_dir.join(PACKAGE_TOML);
    if !package_conf_path.exists() {
        return Err(AnalyzerError {
            start: import.start,
            end: import.end,
            message: format!("{} not found", package_conf_path.display()),
        });
    }

    match parse_package(package_conf_path.to_str().unwrap()) {
        Ok(package_conf) => {
            // 设置导入信息
            import.use_links = true;
            import.package_dir = std_dir.to_str().unwrap().to_string();
            import.package_conf = Some(package_conf);
            return Ok(());
        }
        Err(e) => {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import package failed: {} parse err {}", package_conf_path.display(), e.message),
            });
        }
    }
}

fn package_import_fullpath(package_conf: &PackageConfig, package_dir: &str, ast_import_package: &Vec<String>) -> Result<String, String> {
    assert!(!ast_import_package.is_empty());

    // 获取入口文件名，默认为 main
    let entry = package_conf.package_data.entry.as_deref().unwrap_or("main");

    assert!(!entry.ends_with(".n"), "entry cannot end with .n, entry '{}'", entry);

    // 构建基础路径
    let mut prefix = PathBuf::from(package_dir);
    for package_part in ast_import_package.iter().skip(1) {
        prefix.push(package_part);
    }

    let mut entry_count = 0;
    loop {
        if entry_count == 1 {
            if prefix.is_dir() {
                prefix.push(entry);
            }
        }
        entry_count += 1;

        // 检查 os_arch 特定文件
        let os = env::var("BUILD_OS").unwrap_or(TARGET_OS.to_string());
        let arch = env::var("BUILD_ARCH").unwrap_or(TARGET_ARCH.to_string());
        let os_arch = format!("{}_{}", os, arch);

        let os_arch_path = prefix.with_extension(format!("{}.n", os_arch));
        if os_arch_path.exists() {
            return Ok(os_arch_path.to_str().unwrap().to_string());
        }

        // 检查 os 特定文件
        let os_path = prefix.with_extension(format!("{}.n", os));
        if os_path.exists() {
            return Ok(os_path.to_str().unwrap().to_string());
        }

        // 检查标准文件
        let normal_path = prefix.with_extension("n");
        if normal_path.exists() {
            return Ok(normal_path.to_str().unwrap().to_string());
        }

        if entry_count >= 2 {
            break;
        }
    }

    return Err(format!("cannot find module from '{}'", prefix.to_str().unwrap()));
}

// 在文件中添加这个新函数
pub fn module_unique_ident(root: &str, full_path: &str) -> String {
    // 获取 package_dir 的父目录
    let temp_dir = Path::new(root).parent().and_then(|p| p.to_str()).unwrap_or("");

    // 移除前缀路径
    let mut ident = full_path.replace(temp_dir, "");

    // 移除开头的斜杠
    ident = ident.trim_start_matches('/').to_string();

    // 移除 .n 后缀
    ident = ident.trim_end_matches(".n").to_string();

    // 将路径分隔符替换为点
    ident = ident.replace('/', ".");

    ident
}

/**
 * import 'xxx/xxx.n' 只支持相对于当前 源文件路径导入
 * import project.test.mod
 */
pub fn analyze_import(
    project_root: String,
    package_config_mutex: &Arc<Mutex<Option<PackageConfig>>>,
    m: &mut Module,
    import: &mut ImportStmt,
) -> Result<(), AnalyzerError> {
    if let Some(file) = &import.file {
        // file 不能以 . 或者 / 开头
        if file.starts_with(".") || file.starts_with("/") {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import file cannot start with . or /"),
            });
        }

        import.full_path = Path::new(&m.dir).join(file).to_string_lossy().into_owned();
        if !import.full_path.ends_with(".n") {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import file suffix must .n"),
            });
        }

        // check file exist
        if !Path::new(&import.full_path).exists() {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import file {} not found", file.clone()),
            });
        }

        // 如果 import as empty, 则使用 import 的 file  的文件名称去除后缀作为 import as
        if import.as_name.is_empty() {
            import.as_name = Path::new(&file)
                .file_stem() // 获取文件名并去除后缀
                .and_then(|name| name.to_str())
                .unwrap_or("")
                .to_string();
        }

        import.package_dir = project_root.clone();
        import.module_ident = module_unique_ident(&project_root, &import.full_path);

        return Ok(());
    }

    // import module
    let package_ident = import.ast_package.as_ref().unwrap()[0].clone();

    // 如果存在 package_config, 说明项目存在 package.toml, import 就存在 package.toml 中的 main > dep package > std package
    // 如果不存在 package package_config 则只能是 std package
    let package_config_option = package_config_mutex.lock().unwrap();

    if let Some(p) = package_config_option.as_ref() {
        if is_current_package(&p, &package_ident) {
            // set import belong package_conf
            import.package_conf = Some(p.clone());
            import.package_dir = Path::new(&p.path).parent().unwrap_or(Path::new("")).to_str().unwrap_or("").to_string();
        } else if is_dep_package(&p, &package_ident) {
            analyze_import_dep(&p, m, import)?;
        } else if is_std_package(&package_ident) {
            analyze_import_std(m, import)?;
        } else {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("package '{}' not found", package_ident),
            });
        }
    } else {
        if is_std_package(&package_ident) {
            // only import std package
            analyze_import_std(m, import)?;
        } else {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("package '{}' not found", package_ident),
            });
        }
    }

    // calc full path
    match package_import_fullpath(import.package_conf.as_ref().unwrap(), &import.package_dir, import.ast_package.as_ref().unwrap()) {
        Ok(full_path) => {
            import.full_path = full_path;

            // check full_path exists
            if !Path::new(&import.full_path).exists() {
                return Err(AnalyzerError {
                    start: import.start,
                    end: import.end,
                    message: format!("cannot import '{}': file not found", import.full_path.clone()),
                });
            }

            // check file is n file
            if !import.full_path.ends_with(".n") {
                return Err(AnalyzerError {
                    start: import.start,
                    end: import.end,
                    message: format!("import file suffix must .n"),
                });
            }
        }
        Err(e) => {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: e,
            });
        }
    }

    // calc import as, 如果不存在 import as, 则使用 ast_package 的最后一个元素作为 import as
    if import.as_name.is_empty() {
        import.as_name = import.ast_package.as_ref().unwrap().last().unwrap().clone();
    }

    // calc moudle unique ident, 符号注册到符号表中需要采取同样的策略生成名称
    import.module_ident = module_unique_ident(&import.package_dir, &import.full_path);
    return Ok(());
}

pub fn analyze_imports(
    project_root: String,
    package_config: &Arc<Mutex<Option<PackageConfig>>>,
    m: &mut Module,
    stmts: &mut Vec<Box<Stmt>>,
) -> Vec<ImportStmt> {
    let mut imports: Vec<ImportStmt> = Vec::new();

    for stmt in stmts {
        if let AstNode::Import(import) = &mut stmt.node {
            // 解析出目标文件
            match analyze_import(project_root.clone(), package_config, m, import) {
                Ok(_) => {}
                Err(e) => {
                    m.analyzer_errors.push(e);
                }
            }

            imports.push(import.clone());
        }
    }
    imports
}

/**
 * 在 main module 进行 analyze 之前，需要将 import 关联的所有的模块的 global symbol 都注册到符号表中, ast 暂时不用进行解析
 *  后续的统一 analyzer 时会全部进行解析, 是对原始 nature 编译器的 can_import_symbol_table 字段的优化
 */
pub fn register_global_symbol(m: &mut Module, symbol_table: &mut SymbolTable, stmts: &Vec<Box<Stmt>>) {

    for stmt in stmts {
        match &stmt.node {
            AstNode::VarDef(var_decl_mutex, _) => {
                let mut var_decl = var_decl_mutex.lock().unwrap();

                // 构造全局唯一标识符
                var_decl.ident = format_global_ident(m.ident.clone(), var_decl.ident.clone());

                match symbol_table.define_symbol_in_scope(
                    var_decl.ident.clone(),
                    SymbolKind::Var(var_decl_mutex.clone()),
                    var_decl.symbol_start,
                    m.scope_id,
                ) {
                    Ok(symbol_id) => {
                        var_decl.symbol_id = symbol_id;
                    }
                    Err(e) => {
                        errors_push(m, AnalyzerError {
                            start: var_decl.symbol_start,
                            end: var_decl.symbol_end,
                            message: e,
                        });
                    }
                }

                // 注册到全局符号表
                let _ = symbol_table.define_global_symbol(
                    var_decl.ident.clone(),
                    SymbolKind::Var(var_decl_mutex.clone()),
                    var_decl.symbol_start,
                    m.scope_id,
                );
            }
            AstNode::ConstDef(constdef_mutex) => {
                let mut constdef = constdef_mutex.lock().unwrap();
                constdef.ident = format_global_ident(m.ident.clone(), constdef.ident.clone());

                match symbol_table.define_symbol_in_scope(
                    constdef.ident.clone(),
                    SymbolKind::Const(constdef_mutex.clone()),
                    constdef.symbol_start,
                    m.scope_id,
                ) {
                    Ok(symbol_id) => {
                        constdef.symbol_id = symbol_id;
                    }
                    Err(e) => {
                        errors_push(m, AnalyzerError {
                            start: constdef.symbol_start,
                            end: constdef.symbol_end,
                            message: e,
                        });
                    }
                }
            }
            AstNode::Typedef(typedef_mutex) => {
                let mut typedef = typedef_mutex.lock().unwrap();
                typedef.ident = format_global_ident(m.ident.clone(), typedef.ident.clone());

                match symbol_table.define_symbol_in_scope(typedef.ident.clone(), SymbolKind::Type(typedef_mutex.clone()), typedef.symbol_start, m.scope_id) {
                    Ok(symbol_id) => {
                        typedef.symbol_id = symbol_id;
                    }
                    Err(e) => {
                        debug!("define module typedef {} failed: {}, in scope {}", typedef.ident, e, m.scope_id);
                        errors_push(m, AnalyzerError {
                            start: typedef.symbol_start,
                            end: typedef.symbol_end,
                            message: e,
                        });
                    }
                }

                let _ = symbol_table.define_global_symbol(typedef.ident.clone(), SymbolKind::Type(typedef_mutex.clone()), typedef.symbol_start, m.scope_id);
            }
            AstNode::FnDef(fndef_mutex) => {
                let mut fndef = fndef_mutex.lock().unwrap();
                let symbol_name = fndef.symbol_name.clone();

                if fndef.impl_type.kind.is_unknown() {
                    fndef.symbol_name = format_global_ident(m.ident.clone(), symbol_name.clone());

                    match symbol_table.define_symbol_in_scope(fndef.symbol_name.clone(), SymbolKind::Fn(fndef_mutex.clone()), fndef.symbol_start, m.scope_id) {
                        Ok(symbol_id) => {
                            fndef.symbol_id = symbol_id;
                        }
                        Err(_e) => {
                            errors_push(m, AnalyzerError {
                                start: fndef.symbol_start,
                                end: fndef.symbol_end,
                                message: format!("ident '{}' redeclared", fndef.symbol_name),
                            });
                        }
                    }

                    let _ = symbol_table.define_global_symbol(fndef.symbol_name.clone(), SymbolKind::Fn(fndef_mutex.clone()), fndef.symbol_start, m.scope_id);
                } else {
                    // dealy semantic analyze
                }
            }
            _ => {
                // panic!("module stmt must be var_decl/var_def/fn_decl/type_alias");
                continue;
            }
        }
    }
}
