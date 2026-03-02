//! Project state: module database, symbol table, and multi-phase build pipeline.
//!
//! A [`Project`] represents a single workspace root.  It owns the full
//! compilation state: all parsed modules, the cross-module symbol table, and
//! the workspace-wide symbol index.

use crate::analyzer::common::{AnalyzerError, AstFnDef, AstNode, ImportStmt, PackageConfig, Stmt};
use crate::analyzer::flow::Flow;
use crate::analyzer::generics::Generics;
use crate::analyzer::global_eval::GlobalEval;
use crate::analyzer::lexer::{Lexer, Token};
use crate::analyzer::semantic::Semantic;
use crate::analyzer::symbol::{NodeId, SymbolTable};
use crate::analyzer::syntax::Syntax;
use crate::analyzer::typesys::Typesys;
use crate::analyzer::workspace_index::WorkspaceIndex;
use crate::analyzer::{analyze_imports, register_global_symbol};
use crate::package::parse_package;
use log::{debug, error};
use ropey::Rope;
use std::collections::{HashMap, HashSet};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// Default path to the Nature standard library root.
pub const DEFAULT_NATURE_ROOT: &str = "/usr/local/nature";

// ─── Module ─────────────────────────────────────────────────────────────────────

/// A single compiled `.n` source file.
#[derive(Debug, Clone)]
pub struct Module {
    pub index: usize,
    pub ident: String,
    /// Full source text.
    pub source: String,
    pub rope: Rope,
    /// Absolute file path.
    pub path: String,
    /// Parent directory.
    pub dir: String,
    /// Lexer tokens.
    pub token_db: Vec<Token>,
    pub token_indexes: Vec<usize>,
    /// Semantic (resolved) tokens for semantic-token requests.
    pub sem_token_db: Vec<Token>,
    /// Top-level statements (AST).
    pub stmts: Vec<Box<Stmt>>,
    pub global_vardefs: Vec<AstNode>,
    pub global_fndefs: Vec<Arc<Mutex<AstFnDef>>>,
    /// All function definitions (global + local).
    pub all_fndefs: Vec<Arc<Mutex<AstFnDef>>>,
    /// Errors collected during analysis.
    pub analyzer_errors: Vec<AnalyzerError>,
    /// Scope id for this module in the symbol table.
    pub scope_id: NodeId,
    /// Modules that depend on this one (reverse deps).
    pub references: Vec<usize>,
    /// Modules this one imports (forward deps).
    pub dependencies: Vec<ImportStmt>,
}

impl Module {
    pub fn new(
        ident: String,
        source: String,
        path: String,
        index: usize,
        scope_id: NodeId,
    ) -> Self {
        let dir = Path::new(&path)
            .parent()
            .and_then(|p| p.to_str())
            .unwrap_or("")
            .to_string();
        let rope = Rope::from_str(&source);

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

    /// Check if the on-disk source differs from the cached source.
    pub fn need_rebuild(&self) -> bool {
        std::fs::read_to_string(&self.path)
            .map(|content| content != self.source)
            .unwrap_or(false)
    }
}

impl Default for Module {
    fn default() -> Self {
        Self {
            scope_id: 0,
            index: 0,
            ident: String::new(),
            source: String::new(),
            path: String::new(),
            dir: String::new(),
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

// ─── Queue ──────────────────────────────────────────────────────────────────────

/// An item in the rebuild queue.
#[derive(Debug, Clone)]
pub struct QueueItem {
    pub path: String,
    /// If set, the module at this path should be notified after build completes.
    pub notify: Option<String>,
}

// ─── Project ────────────────────────────────────────────────────────────────────

/// Owns the full analysis state for a workspace root.
#[derive(Debug, Clone)]
pub struct Project {
    /// Path to the Nature standard library.
    pub nature_root: String,
    /// Workspace root path.
    pub root: String,
    /// All compiled modules.
    pub module_db: Arc<Mutex<Vec<Module>>>,
    /// path → index into `module_db`.
    pub module_handled: Arc<Mutex<HashMap<String, usize>>>,
    /// Async rebuild queue for dependent modules.
    pub queue: Arc<Mutex<Vec<QueueItem>>>,
    /// Parsed `package.toml`, if present.
    pub package_config: Arc<Mutex<Option<PackageConfig>>>,
    /// Cross-module symbol table.
    pub symbol_table: Arc<Mutex<SymbolTable>>,
    /// Lightweight workspace-wide symbol index (for workspace/symbol search).
    pub workspace_index: Arc<Mutex<WorkspaceIndex>>,
    /// Guard: true while a build is in progress.
    pub is_building: Arc<AtomicBool>,
}

impl Project {
    /// Create a new project for the given workspace root.
    ///
    /// Loads builtin modules from `$NATURE_ROOT/std/builtin/` and parses
    /// `package.toml` if present.
    pub async fn new(project_root: String) -> Self {
        let nature_root =
            std::env::var("NATURE_ROOT").unwrap_or_else(|_| DEFAULT_NATURE_ROOT.to_string());

        // Collect builtin .n files.
        let mut builtin_list: Vec<String> = Vec::new();
        let std_builtin_dir = Path::new(&nature_root).join("std").join("builtin");
        if let Ok(entries) = std::fs::read_dir(&std_builtin_dir) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.is_file() && p.extension().map_or(false, |ext| ext == "n") {
                    if let Some(s) = p.to_str() {
                        builtin_list.push(s.to_string());
                    }
                }
            }
        }

        // Parse package.toml.
        let package_path = Path::new(&project_root)
            .join("package.toml")
            .to_string_lossy()
            .to_string();
        let package_config = match parse_package(&package_path) {
            Ok(cfg) => Arc::new(Mutex::new(Some(cfg))),
            Err(_) => Arc::new(Mutex::new(None)),
        };

        let mut project = Self {
            nature_root: nature_root.clone(),
            root: project_root.clone(),
            module_db: Arc::new(Mutex::new(Vec::new())),
            module_handled: Arc::new(Mutex::new(HashMap::new())),
            queue: Arc::new(Mutex::new(Vec::new())),
            package_config,
            symbol_table: Arc::new(Mutex::new(SymbolTable::new())),
            workspace_index: Arc::new(Mutex::new(WorkspaceIndex::new())),
            is_building: Arc::new(AtomicBool::new(false)),
        };

        // Initial workspace scan.
        {
            let mut index = project.workspace_index.lock().unwrap();
            index.scan_workspace(&project_root, &nature_root);
        }

        // Build builtins.
        for path in builtin_list {
            project.build(&path, "", None).await;
        }

        project
    }

    /// Spawn a background task that continuously drains the rebuild queue.
    pub fn start_queue_worker(&self) {
        let mut clone = self.clone();
        tokio::spawn(async move {
            clone.run_queue().await;
        });
    }

    async fn run_queue(&mut self) {
        loop {
            let item = self.queue.lock().unwrap().pop();
            match item {
                Some(item) => {
                    if self.needs_build(&item) {
                        self.build(&item.path, "", None).await;
                    }
                }
                None => {
                    tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
                }
            }
        }
    }

    fn needs_build(&self, item: &QueueItem) -> bool {
        let handled = self.module_handled.lock().unwrap();
        if let Some(&idx) = handled.get(&item.path) {
            let db = self.module_db.lock().unwrap();
            db[idx].need_rebuild()
        } else {
            true
        }
    }

    /// Collect all modules that transitively depend on `index`.
    pub fn all_references(&self, index: usize) -> Vec<String> {
        let mut handled = HashSet::new();
        let mut result = Vec::new();
        let mut worklist = vec![index];

        while let Some(current) = worklist.pop() {
            if !handled.insert(current) {
                continue;
            }
            let db = self.module_db.lock().unwrap();
            if let Some(m) = db.get(current) {
                for &ref_idx in &m.references {
                    if let Some(ref_m) = db.get(ref_idx) {
                        result.push(ref_m.path.clone());
                        worklist.push(ref_idx);
                    }
                }
            }
        }

        result
    }

    // ── Build pipeline ──────────────────────────────────────────────────

    /// Run the full build pipeline for a module and its transitive imports.
    pub async fn build(
        &mut self,
        main_path: &str,
        module_ident: &str,
        content: Option<String>,
    ) -> usize {
        self.is_building.store(true, Ordering::SeqCst);
        let result = self.build_inner(main_path, module_ident, content).await;
        self.is_building.store(false, Ordering::SeqCst);
        result
    }

    async fn build_inner(
        &mut self,
        main_path: &str,
        module_ident: &str,
        content: Option<String>,
    ) -> usize {
        let mut worklist: Vec<ImportStmt> = Vec::new();
        let mut handled: HashSet<String> = HashSet::new();
        let mut module_indexes: Vec<usize> = Vec::new();

        // Seed with the main module.
        let mut main_import = ImportStmt::default();
        main_import.full_path = main_path.to_string();
        main_import.module_ident = module_ident.to_string();
        worklist.push(main_import);
        handled.insert(main_path.to_string());

        // ── Phase 1: Lex + Parse + collect imports ──────────────────────
        debug!("{} processing worklist", main_path);
        while let Some(import_stmt) = worklist.pop() {
            let index = {
                let module_handled = self.module_handled.lock().unwrap();
                let existing = module_handled.get(&import_stmt.full_path).copied();
                drop(module_handled);

                if let Some(i) = existing {
                    if import_stmt.full_path == main_path {
                        // Update content of the main module.
                        if let Some(ref c) = content {
                            let mut db = self.module_db.lock().unwrap();
                            let m = &mut db[i];
                            m.source = c.clone();
                            m.rope = Rope::from_str(&m.source);
                        }
                        i
                    } else {
                        continue; // already compiled
                    }
                } else {
                    // New module — read from disk.
                    let Ok(file_content) = std::fs::read_to_string(&import_stmt.full_path) else {
                        continue;
                    };

                    let mut mh = self.module_handled.lock().unwrap();
                    let mut db = self.module_db.lock().unwrap();
                    let idx = db.len();
                    let scope_id = self
                        .symbol_table
                        .lock()
                        .unwrap()
                        .create_module_scope(import_stmt.module_ident.clone());
                    db.push(Module::new(
                        import_stmt.module_ident,
                        file_content,
                        import_stmt.full_path.clone(),
                        idx,
                        scope_id,
                    ));
                    mh.insert(import_stmt.full_path, idx);
                    idx
                }
            };

            let mut db = self.module_db.lock().unwrap();
            let m = &mut db[index];

            debug!(
                "build module #{} path={} dir={} root={}",
                m.index, m.path, m.dir, self.root
            );

            // Clean scope for re-analysis.
            self.symbol_table
                .lock()
                .unwrap()
                .clean_module_scope(m.ident.clone());

            // Lex.
            let (token_db, token_indexes, lexer_errors) = Lexer::new(m.source.clone()).scan();
            m.token_db = token_db.clone();
            m.token_indexes = token_indexes.clone();
            m.analyzer_errors = lexer_errors;

            // Parse.
            let (mut stmts, sem_token_db, syntax_errors) =
                Syntax::new(m.clone(), token_db, token_indexes).parser();
            m.sem_token_db = sem_token_db;
            m.analyzer_errors.extend(syntax_errors);

            module_indexes.push(index);

            // Register global symbols.
            let mut st = self.symbol_table.lock().unwrap();
            register_global_symbol(m, &mut st, &stmts);
            drop(st);

            // Resolve imports.
            let imports = analyze_imports(self.root.clone(), &self.package_config, m, &mut stmts);
            m.stmts = stmts;

            let mut filter_imports: Vec<ImportStmt> = Vec::new();
            for import in imports {
                if import.full_path == main_path {
                    continue;
                }
                filter_imports.push(import.clone());
                if !handled.contains(&import.full_path) {
                    worklist.push(import.clone());
                    handled.insert(import.full_path.clone());
                }
            }
            drop(db);

            self.update_module_deps(index, filter_imports);
        }

        // ── Phase 2: Semantic analysis passes ───────────────────────────
        debug!(
            "{} semantic passes, modules: {:?}",
            main_path, module_indexes
        );

        for &idx in &module_indexes {
            let mut db = self.module_db.lock().unwrap();
            let mut st = self.symbol_table.lock().unwrap();
            let m = &mut db[idx];
            m.all_fndefs = Vec::new();
            if let Err(e) = catch_unwind(AssertUnwindSafe(|| {
                Semantic::new(m, &mut st).analyze();
            })) {
                error!("panic in semantic pass for module #{}: {:?}", idx, e);
            }
        }

        for &idx in &module_indexes {
            let mut db = self.module_db.lock().unwrap();
            let st = self.symbol_table.lock().unwrap();
            let m = &mut db[idx];
            if let Err(e) = catch_unwind(AssertUnwindSafe(|| {
                Generics::new(m, &st).analyze();
            })) {
                error!("panic in generics pass for module #{}: {:?}", idx, e);
            }
        }

        for &idx in &module_indexes {
            let mut db = self.module_db.lock().unwrap();
            let mut st = self.symbol_table.lock().unwrap();
            let m = &mut db[idx];
            match catch_unwind(AssertUnwindSafe(|| {
                Typesys::new(&mut st, m).pre_infer()
            })) {
                Ok(errors) => m.analyzer_errors.extend(errors),
                Err(e) => error!("panic in typesys pre_infer for module #{}: {:?}", idx, e),
            }
        }

        for &idx in &module_indexes {
            let mut db = self.module_db.lock().unwrap();
            let mut st = self.symbol_table.lock().unwrap();
            let m = &mut db[idx];
            match catch_unwind(AssertUnwindSafe(|| {
                GlobalEval::new(m, &mut st).analyze()
            })) {
                Ok(errors) => m.analyzer_errors.extend(errors),
                Err(e) => error!("panic in global_eval for module #{}: {:?}", idx, e),
            }
        }

        for &idx in &module_indexes {
            let mut db = self.module_db.lock().unwrap();
            let mut st = self.symbol_table.lock().unwrap();
            let m = &mut db[idx];
            match catch_unwind(AssertUnwindSafe(|| {
                let errors = Typesys::new(&mut st, m).infer();
                let flow_errors = Flow::new(m).analyze();
                (errors, flow_errors)
            })) {
                Ok((errors, flow_errors)) => {
                    m.analyzer_errors.extend(errors);
                    m.analyzer_errors.extend(flow_errors);
                }
                Err(e) => error!("panic in typesys infer/flow for module #{}: {:?}", idx, e),
            }
        }

        // ── Phase 3: Queue dependent rebuilds ───────────────────────────
        let main_index = {
            let mh = self.module_handled.lock().unwrap();
            match mh.get(main_path).copied() {
                Some(i) => i,
                None => return 0,
            }
        };

        let refers = self.all_references(main_index);
        for refer in refers {
            if refer == main_path {
                continue;
            }
            debug!("{} queuing dependent: {}", main_path, refer);
            self.queue.lock().unwrap().push(QueueItem {
                path: refer,
                notify: Some(main_path.to_string()),
            });
        }

        // Re-index the built file.
        {
            let mut ws = self.workspace_index.lock().unwrap();
            ws.remove_file_symbols(main_path);
            ws.index_file(main_path, &self.root);
            debug!("workspace index: re-indexed {}", main_path);
        }

        main_index
    }

    /// Update forward/reverse dependency links for a module.
    fn update_module_deps(&mut self, module_index: usize, imports: Vec<ImportStmt>) {
        let dep_indices: Vec<usize> = {
            let mh = self.module_handled.lock().unwrap();
            imports
                .iter()
                .filter_map(|imp| mh.get(&imp.full_path).copied())
                .collect()
        };

        let mut db = self.module_db.lock().unwrap();

        // Remove stale reverse references.
        let old_deps: HashSet<String> = db[module_index]
            .dependencies
            .iter()
            .map(|d| d.full_path.clone())
            .collect();
        let new_deps: HashSet<String> = imports.iter().map(|d| d.full_path.clone()).collect();

        for removed in old_deps.difference(&new_deps) {
            if let Some(&dep_idx) = dep_indices
                .iter()
                .find(|&&i| db[i].path == *removed)
            {
                db[dep_idx].references.retain(|&x| x != module_index);
            }
        }

        // Update forward deps.
        db[module_index].dependencies = imports.clone();

        // Add reverse references.
        for import in &imports {
            if let Some(&dep_idx) = dep_indices
                .iter()
                .find(|&&i| db[i].path == import.full_path)
            {
                if !db[dep_idx].references.contains(&module_index) {
                    db[dep_idx].references.push(module_index);
                }
            }
        }
    }
}
