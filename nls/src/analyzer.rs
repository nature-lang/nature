pub mod common;
pub mod completion;
pub mod flow;
pub mod generics;
pub mod global_eval;
pub mod lexer; // 声明子模块
pub mod semantic;
pub mod symbol;
pub mod syntax;
pub mod typesys;
pub mod workspace_index;

use std::path::Path;

use crate::module_index::{
    import_module_path, module_ident_join, module_parent_path, module_source_rel_path, package_unit_load,
    PackageUnit,
};
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
#[cfg(target_os = "windows")]
const TARGET_OS: &str = "windows";

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
                is_warning: false,
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
            is_warning: false,
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
                is_warning: false,
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
            is_warning: false,
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
                is_warning: false,
                            });
        }
    }
}

pub fn target_os() -> &'static str {
    TARGET_OS
}

pub fn target_arch() -> &'static str {
    TARGET_ARCH
}

/// Compute the module ident from the path in single-file compatibility mode (no package.toml)
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
                is_warning: false,
                            });
        }

        import.full_path = Path::new(&m.dir).join(file).to_string_lossy().into_owned();
        if !import.full_path.ends_with(".n") {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import file suffix must .n"),
                is_warning: false,
                            });
        }

        // check file exist
        if !Path::new(&import.full_path).exists() {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("import file {} not found", file.clone()),
                is_warning: false,
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

        let package_config_option = package_config_mutex.lock().unwrap();
        let current_package = package_config_option.as_ref().cloned();
        drop(package_config_option);

        // single-file compatibility mode without a package.toml keeps resolving by path
        let Some(p) = current_package else {
            import.package_dir = project_root.clone();
            import.module_ident = module_unique_ident(&project_root, &import.full_path);
            return Ok(());
        };

        let package_dir = Path::new(&p.path).parent().unwrap_or(Path::new("")).to_str().unwrap_or("").to_string();
        let pu = package_unit_load(&package_dir, &p.package_data.name);

        // a quoted import may only point at a standalone file of the current package that has no mod declaration
        let Some(unit) = pu.find_source(&import.full_path) else {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("cannot import '{}': not found in package '{}'", file, p.package_data.name),
                is_warning: false,
            });
        };

        if unit.is_dir_module {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!(
                    "cannot import '{}': it is part of module {}, use 'import {}'",
                    file, unit.module_ident, unit.module_ident
                ),
                is_warning: false,
            });
        }

        if unit.module_ident == m.ident {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("cannot import '{}': module {} cannot import itself", file, m.ident),
                is_warning: false,
            });
        }

        import.package_conf = Some(p);
        import.package_dir = package_dir;
        import.module_ident = unit.module_ident.clone();
        import.full_path = unit.sources[0].clone();
        import.module_sources = unit.sources.clone();

        return Ok(());
    }

    // import module
    let package_ident = import.ast_package.as_ref().unwrap()[0].clone();

    // 如果存在 package_config, 说明项目存在 package.toml, import 就存在 package.toml 中的 main > dep package > std package
    // 如果不存在 package package_config 则只能是 std package
    let package_config_option = package_config_mutex.lock().unwrap();
    let current_package = package_config_option.as_ref().cloned();
    drop(package_config_option);

    if let Some(p) = current_package {
        if is_current_package(&p, &package_ident) {
            // set import belong package_conf
            import.package_dir = Path::new(&p.path).parent().unwrap_or(Path::new("")).to_str().unwrap_or("").to_string();
            import.package_conf = Some(p);
        } else if is_dep_package(&p, &package_ident) {
            analyze_import_dep(&p, m, import)?;
        } else if is_std_package(&package_ident) {
            analyze_import_std(m, import)?;
        } else {
            return Err(AnalyzerError {
                start: import.start,
                end: import.end,
                message: format!("package '{}' not found", package_ident),
                is_warning: false,
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
                is_warning: false,
                            });
        }
    }

    // imports resolve purely by logical module name
    // a dependencies key is only a local alias, package identity always comes from package.toml.name
    let package_name = import.package_conf.as_ref().unwrap().package_data.name.clone();
    let pu = package_unit_load(&import.package_dir, &package_name);

    let module_path = import_module_path(import.ast_package.as_ref().unwrap());
    let Some(unit) = pu.find_module(&module_path) else {
        let full_ident = module_ident_join(&package_name, &module_path);
        let parent = module_parent_path(&module_path);

        let last_segment = module_path.rsplit('.').next().unwrap_or(&module_path);

        // when it points at a source part, offer an actionable suggestion
        if let Some(parent_unit) = pu.find_module(&parent) {
            if parent_unit.is_dir_module && PackageUnit::unit_has_source_named(parent_unit, last_segment) {
                return Err(AnalyzerError {
                    start: import.start,
                    end: import.end,
                    message: format!(
                        "'{}' is not a module, it is part of {}, use 'import {}'",
                        full_ident, parent_unit.module_ident, parent_unit.module_ident
                    ),
                    is_warning: false,
                });
            }
        }

        return Err(AnalyzerError {
            start: import.start,
            end: import.end,
            message: format!("cannot import '{}': module not found", full_ident),
            is_warning: false,
        });
    };

    // a source part cannot import the module it belongs to
    if unit.module_ident == m.ident {
        return Err(AnalyzerError {
            start: import.start,
            end: import.end,
            message: format!("module {} cannot import itself", m.ident),
            is_warning: false,
        });
    }

    // calc import as, 如果不存在 import as, 则使用 ast_package 的最后一个元素作为 import as
    // For selective imports, leave as_name empty — the symbols are imported directly,
    // not via a module alias, so the module should not shadow local variables in completion.
    if import.as_name.is_empty() && !import.is_selective {
        import.as_name = import.ast_package.as_ref().unwrap().last().unwrap().clone();
    }

    import.module_ident = unit.module_ident.clone();
    import.full_path = unit.sources[0].clone();
    import.module_sources = unit.sources.clone();
    return Ok(());
}

/// Validate a leading mod declaration: the root must equal package.toml.name, a normal subdirectory the directory basename
pub fn analyze_mod_decl(package_config: &Arc<Mutex<Option<PackageConfig>>>, m: &mut Module, stmts: &Vec<Box<Stmt>>) {
    let Some(stmt) = stmts.first() else {
        return;
    };
    let AstNode::Mod(mod_stmt) = &stmt.node else {
        return;
    };

    let package_config_option = package_config.lock().unwrap();
    let current_package = package_config_option.as_ref().cloned();
    drop(package_config_option);

    let Some(p) = current_package else {
        m.analyzer_errors.push(AnalyzerError::new(
            mod_stmt.start,
            mod_stmt.end,
            "cannot use 'mod' without package.toml".to_string(),
        ));
        return;
    };

    let package_dir = Path::new(&p.path).parent().unwrap_or(Path::new("")).to_str().unwrap_or("").to_string();
    let rel_dir = module_source_rel_path(&package_dir, &m.dir);

    let expect = if rel_dir.is_empty() {
        p.package_data.name.clone()
    } else {
        rel_dir.rsplit('/').next().unwrap_or(&rel_dir).to_string()
    };

    if mod_stmt.ident == expect {
        return;
    }

    let message = if rel_dir.is_empty() {
        format!(
            "'mod {}' does not match package name '{}', use 'mod {}'",
            mod_stmt.ident, expect, expect
        )
    } else {
        format!("'mod {}' does not match directory '{}', use 'mod {}'", mod_stmt.ident, expect, expect)
    };

    m.analyzer_errors.push(AnalyzerError::new(mod_stmt.start, mod_stmt.end, message));
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
                        errors_push(
                            m,
                            AnalyzerError {
                                start: var_decl.symbol_start,
                                end: var_decl.symbol_end,
                                message: e,
                                is_warning: false,
                                                            },
                        );
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
                        errors_push(
                            m,
                            AnalyzerError {
                                start: constdef.symbol_start,
                                end: constdef.symbol_end,
                                message: e,
                                is_warning: false,
                                                            },
                        );
                    }
                }

                // Register in global symbol table (needed for selective imports)
                let _ = symbol_table.define_global_symbol(
                    constdef.ident.clone(),
                    SymbolKind::Const(constdef_mutex.clone()),
                    constdef.symbol_start,
                    m.scope_id,
                );
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
                        errors_push(
                            m,
                            AnalyzerError {
                                start: typedef.symbol_start,
                                end: typedef.symbol_end,
                                message: e,
                                is_warning: false,
                                                            },
                        );
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
                            errors_push(
                                m,
                                AnalyzerError {
                                    start: fndef.symbol_start,
                                    end: fndef.symbol_end,
                                    message: format!("ident '{}' redeclared", fndef.symbol_name),
                                    is_warning: false,
                                                                    },
                            );
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
