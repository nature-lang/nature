use crate::analyzer::common::ImportStmt;
use crate::analyzer::symbol::SymbolKind;
use crate::analyzer::workspace_index::IndexedSymbolKind;
use log::debug;
use std::collections::HashSet;

use super::context::extract_last_ident_part;
use super::{CompletionItem, CompletionItemKind, CompletionProvider, TextEdit};

impl<'a> CompletionProvider<'a> {
    /// Get completions for selective import context: `import module.path.{|}`
    /// Lists all exportable symbols from the target module.
    pub fn get_selective_import_completions(&self, module_path: &str, prefix: &str) -> Vec<CompletionItem> {
        debug!("Getting selective import completions for module path '{}' with prefix '{}'", module_path, prefix);

        let mut completions = Vec::new();

        // Find the import statement whose module path matches
        // The module_path from text is like "forest.app.create", the module_ident
        // in dependencies is derived from full path. Match via ast_package.
        let import_stmt = self.module.dependencies.iter().find(|dep| {
            if let Some(ref pkg) = dep.ast_package {
                let pkg_path = pkg.join(".");
                // The module_path may or may not include a trailing dot
                pkg_path == module_path || pkg_path.starts_with(module_path)
            } else {
                false
            }
        });

        let module_ident = if let Some(stmt) = import_stmt {
            stmt.module_ident.clone()
        } else {
            // Try to find in dependencies where module_ident or as_name contains the path
            // Fall back: try matching the last segment
            let last_seg = module_path.rsplit('.').next().unwrap_or(module_path);
            if let Some(stmt) = self.module.dependencies.iter().find(|dep| {
                dep.module_ident.ends_with(last_seg) || dep.as_name == last_seg
            }) {
                stmt.module_ident.clone()
            } else {
                debug!("Module path '{}' not found in dependencies", module_path);
                return completions;
            }
        };

        debug!("Resolved module_ident: '{}'", module_ident);

        // Find the module's scope and list its symbols
        if let Some(&scope_id) = self.symbol_table.module_scopes.get(&module_ident) {
            let scope = self.symbol_table.find_scope(scope_id);
            debug!("Found module scope {} with {} symbols", scope_id, scope.symbols.len());

            // Collect already-imported item names to mark them
            let already_imported: HashSet<String> = if let Some(stmt) = import_stmt {
                if let Some(ref items) = stmt.select_items {
                    items.iter().map(|item| item.ident.clone()).collect()
                } else {
                    HashSet::new()
                }
            } else {
                HashSet::new()
            };

            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    let symbol_name = extract_last_ident_part(&symbol.ident);

                    // Skip test functions and impl methods (struct methods can't be exported)
                    if let SymbolKind::Fn(fndef) = &symbol.kind {
                        let fndef_locked = fndef.lock().unwrap();
                        if fndef_locked.is_test || fndef_locked.impl_type.kind.is_exist() {
                            continue;
                        }
                    }

                    // Skip if already imported
                    if already_imported.contains(&symbol_name) {
                        continue;
                    }

                    // Check prefix match
                    if !prefix.is_empty() && !symbol_name.starts_with(prefix) {
                        continue;
                    }

                    let (kind, detail) = match &symbol.kind {
                        SymbolKind::Fn(fndef) => {
                            let fndef = fndef.lock().unwrap();
                            let sig = self.format_function_signature(&fndef);
                            (CompletionItemKind::Function, format!("fn: {}", sig))
                        }
                        SymbolKind::Type(typedef) => {
                            let typedef = typedef.lock().unwrap();
                            (CompletionItemKind::Struct, format!("type: {}", typedef.ident))
                        }
                        SymbolKind::Var(var) => {
                            let var = var.lock().unwrap();
                            (CompletionItemKind::Variable, format!("var: {}", var.type_))
                        }
                        SymbolKind::Const(c) => {
                            let c = c.lock().unwrap();
                            (CompletionItemKind::Constant, format!("const: {}", c.type_))
                        }
                    };

                    completions.push(CompletionItem {
                        label: symbol_name.clone(),
                        kind,
                        detail: Some(detail),
                        documentation: None,
                        insert_text: symbol_name,
                        sort_text: Some(format!("{:08}", symbol.pos)),
                        additional_text_edits: Vec::new(),
                    });
                }
            }
        } else {
            debug!("Module '{}' scope not found in symbol_table.module_scopes", module_ident);
        }

        self.sort_and_filter_completions(&mut completions, prefix);
        debug!("Found {} selective import completions", completions.len());
        completions
    }

    pub fn get_module_member_completions(&self, imported_as_name: &str, prefix: &str) -> Vec<CompletionItem> {
        debug!("Getting module member completions for module '{}' with prefix '{}'", imported_as_name, prefix);

        let mut completions = Vec::new();

        let deps = &self.module.dependencies;

        let import_stmt = deps.iter().find(|&dep| dep.as_name == imported_as_name);
        if import_stmt.is_none() {
            // Module not imported — try to offer workspace-indexed symbols
            // from a matching module with an auto-import text edit, so the
            // user can type `fmt.` and see `sprintf`, `printf`, etc. even
            // before adding `import fmt`.
            return self.get_unimported_module_member_completions(imported_as_name, prefix);
        }

        let imported_module_ident = import_stmt.unwrap().module_ident.clone();
        debug!("Imported module is '{}' find by {}", imported_module_ident, imported_as_name);

        // Find imported module scope
        if let Some(&imported_scope_id) = self.symbol_table.module_scopes.get(&imported_module_ident) {
            let imported_scope = self.symbol_table.find_scope(imported_scope_id);
            debug!("Found imported scope {} with {} symbols", imported_scope_id, imported_scope.symbols.len());

            // Iterate through all symbols in imported module
            for &symbol_id in &imported_scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    if let SymbolKind::Fn(fndef) = &symbol.kind {
                        if fndef.lock().unwrap().is_test {
                            continue;
                        }
                    }

                    // Extract the actual symbol name (without module path)
                    let symbol_name = extract_last_ident_part(&symbol.ident);

                    debug!(
                        "Checking symbol: {} (extracted: {}) of kind: {:?}",
                        symbol.ident,
                        symbol_name,
                        match &symbol.kind {
                            SymbolKind::Var(_) => "Var",
                            SymbolKind::Const(_) => "Const",
                            SymbolKind::Fn(_) => "Fn",
                            SymbolKind::Type(_) => "Type",
                        }
                    );

                    // Check prefix match against the extracted name
                    if prefix.is_empty() || symbol_name.starts_with(prefix) {
                        let completion_item = self.create_module_completion_member(symbol);
                        debug!("Adding module member completion: {} (kind: {:?})", completion_item.label, completion_item.kind);
                        completions.push(completion_item);
                    }
                }
            }
        } else {
            debug!("Module '{}' not found in symbol table", imported_module_ident);
        }

        // Sort and filter
        self.sort_and_filter_completions(&mut completions, prefix);

        debug!("Found {} module member completions", completions.len());
        completions
    }

    /// Provide dot-completions for a module that hasn't been imported yet.
    ///
    /// When the user types `fmt.` but doesn't have `import fmt`, we look up
    /// the module in the workspace index and offer its symbols with an
    /// auto-import text edit that inserts `import fmt\n` at the top of the file.
    fn get_unimported_module_member_completions(&self, module_name: &str, prefix: &str) -> Vec<CompletionItem> {
        debug!("Trying unimported module member completions for '{}'", module_name);

        let workspace_index = match &self.workspace_index {
            Some(idx) => idx,
            None => return Vec::new(),
        };

        // Find the module directory.  We check:
        //  1. std library:  $NATURE_ROOT/std/<module_name>/
        //  2. local file:   <module_dir>/<module_name>.n
        //  3. package sub-dir: <project_root>/<module_name>/
        let std_module_dir = {
            let mut p = std::path::PathBuf::from(&self.nature_root);
            p.push("std");
            p.push(module_name);
            p
        };
        let local_file = {
            let mut p = std::path::PathBuf::from(&self.module.dir);
            p.push(format!("{}.n", module_name));
            p
        };

        // Determine the import statement and which file paths belong to
        // this module.
        let (import_statement, match_paths): (String, Vec<String>) = if std_module_dir.is_dir() {
            // Standard library package — collect all .n files under std/<name>/
            let mut paths = Vec::new();
            if let Ok(entries) = std::fs::read_dir(&std_module_dir) {
                for entry in entries.flatten() {
                    let p = entry.path();
                    if p.is_file() && p.extension().map_or(false, |e| e == "n") {
                        if let Some(s) = p.to_str() {
                            paths.push(s.to_string());
                        }
                    }
                }
            }
            (format!("import {}\n", module_name), paths)
        } else if local_file.is_file() {
            let path_str = local_file.to_str().unwrap_or("").to_string();
            let file_name = format!("{}.n", module_name);
            (format!("import '{}'\n", file_name), vec![path_str])
        } else {
            return Vec::new();
        };

        let match_set: HashSet<&str> = match_paths.iter().map(|s| s.as_str()).collect();

        // Gather all indexed symbols whose file_path belongs to this module.
        let mut completions = Vec::new();

        for (name, indexed_list) in &workspace_index.symbols {
            for indexed in indexed_list {
                if !match_set.contains(indexed.file_path.as_str()) {
                    continue;
                }
                if !prefix.is_empty() && !name.starts_with(prefix) {
                    continue;
                }

                let kind = match indexed.kind {
                    IndexedSymbolKind::Function => CompletionItemKind::Function,
                    IndexedSymbolKind::Type => CompletionItemKind::Struct,
                    IndexedSymbolKind::Variable => CompletionItemKind::Variable,
                    IndexedSymbolKind::Constant => CompletionItemKind::Constant,
                };

                completions.push(CompletionItem {
                    label: name.clone(),
                    kind,
                    detail: Some(format!("{} (auto-import)", module_name)),
                    documentation: None,
                    insert_text: name.clone(),
                    sort_text: Some(format!("aaa_{}", name)),
                    additional_text_edits: vec![TextEdit {
                        line: 0,
                        character: 0,
                        new_text: import_statement.clone(),
                    }],
                });
            }
        }

        self.sort_and_filter_completions(&mut completions, prefix);
        debug!("Found {} unimported module member completions for '{}'", completions.len(), module_name);
        completions
    }

    /// Collect symbols from selective imports as direct completions.
    /// When the user has `import forest.app.configuration.{configuration}`, the symbol
    /// `configuration` should appear as a completion in function bodies.
    pub(crate) fn collect_selective_import_symbol_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        let existing_labels: HashSet<String> = completions.iter().map(|c| c.label.clone()).collect();

        for import in &self.module.dependencies {
            if !import.is_selective {
                continue;
            }
            let Some(ref items) = import.select_items else { continue };

            // Find the imported module's scope to look up symbols
            let imported_scope_id = self.symbol_table.module_scopes.get(&import.module_ident);

            for item in items {
                let local_name = item.alias.as_ref().unwrap_or(&item.ident);

                // Check prefix match
                if !prefix.is_empty() && !local_name.starts_with(prefix) {
                    continue;
                }

                // Skip if already provided by another completion source
                if existing_labels.contains(local_name) {
                    continue;
                }

                // Try to find the symbol in the imported module's scope
                let global_ident = crate::utils::format_global_ident(import.module_ident.clone(), item.ident.clone());
                let symbol = self.symbol_table.find_global_symbol(&global_ident)
                    .or_else(|| {
                        // Fallback: search in module scope
                        if let Some(&scope_id) = imported_scope_id {
                            self.symbol_table.find_symbol(&global_ident, scope_id)
                                .or_else(|| self.symbol_table.find_symbol(&item.ident, scope_id))
                        } else {
                            None
                        }
                    });

                let (kind, detail, insert_text) = if let Some(symbol) = symbol {
                    match &symbol.kind {
                        SymbolKind::Fn(fndef_mutex) => {
                            let fndef = fndef_mutex.lock().unwrap();
                            let sig = self.format_function_signature(&fndef);
                            let ins = if self.has_parameters(&fndef) {
                                format!("{}($0)", local_name)
                            } else {
                                format!("{}()", local_name)
                            };
                            (CompletionItemKind::Function, format!("fn: {}", sig), ins)
                        }
                        SymbolKind::Type(typedef_mutex) => {
                            let typedef = typedef_mutex.lock().unwrap();
                            (CompletionItemKind::Struct, format!("type: {}", typedef.ident), local_name.clone())
                        }
                        SymbolKind::Var(var_mutex) => {
                            let var = var_mutex.lock().unwrap();
                            (CompletionItemKind::Variable, format!("var: {}", var.type_), local_name.clone())
                        }
                        SymbolKind::Const(const_mutex) => {
                            let const_def = const_mutex.lock().unwrap();
                            (CompletionItemKind::Constant, format!("const: {}", const_def.type_), local_name.clone())
                        }
                    }
                } else {
                    // Symbol not found in symbol table; still offer it as a basic completion
                    (CompletionItemKind::Variable, format!("from {}", import.module_ident), local_name.clone())
                };

                let module_display = if let Some(ref pkg) = import.ast_package {
                    pkg.join(".")
                } else {
                    import.module_ident.clone()
                };

                completions.push(CompletionItem {
                    label: local_name.clone(),
                    kind,
                    detail: Some(detail),
                    documentation: Some(format!("imported from {}", module_display)),
                    insert_text,
                    sort_text: Some(format!("aab_{}", local_name)), // High priority (after already-imported modules)
                    additional_text_edits: Vec::new(),
                });
            }
        }
    }

    /// Collect already-imported modules as completions (show at top)
    pub(crate) fn collect_imported_module_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        debug!("Collecting imported module completions with prefix '{}'", prefix);

        for import_stmt in &self.module.dependencies {
            let module_name = &import_stmt.as_name;

            // Skip selective imports (they don't have a module alias)
            if module_name.is_empty() || import_stmt.is_selective {
                continue;
            }

            // Check prefix match
            if !prefix.is_empty() && !module_name.starts_with(prefix) {
                continue;
            }

            debug!("Found imported module: {}", module_name);

            // Determine import statement display
            let import_display = if let Some(file) = &import_stmt.file {
                format!("import '{}'", file)
            } else if let Some(ref package) = import_stmt.ast_package {
                format!("import {}", package.join("."))
            } else {
                format!("import {}", module_name)
            };

            completions.push(CompletionItem {
                label: module_name.to_string(),
                kind: CompletionItemKind::Module,
                detail: Some(format!("imported: {}", import_display)),
                documentation: Some(format!("Already imported module: {}", module_name)),
                insert_text: module_name.to_string(),
                sort_text: Some(format!("aaa_{}", module_name)), // Sort to top with "aaa" prefix
                additional_text_edits: Vec::new(),
            });
        }
    }

    /// Collect available module completions (for auto-import)
    pub(crate) fn collect_module_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        debug!("Collecting module completions with prefix '{}'", prefix);

        // Check which modules are already imported
        let already_imported: HashSet<String> = self.module.dependencies.iter().map(|dep| dep.as_name.clone()).collect();

        // 1. Scan .n files in current directory (file-based imports)
        let module_dir = &self.module.dir;
        debug!("Scanning module directory: {}", module_dir);

        if let Ok(entries) = std::fs::read_dir(module_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    let path = entry.path();
                    if path.is_file() && path.extension().and_then(|s| s.to_str()) == Some("n") {
                        if let Some(file_stem) = path.file_stem().and_then(|s| s.to_str()) {
                            // Skip current file
                            if path == std::path::Path::new(&self.module.path) {
                                continue;
                            }

                            // Skip already imported modules
                            if already_imported.contains(file_stem) {
                                continue;
                            }

                            // Check prefix match
                            if !prefix.is_empty() && !file_stem.starts_with(prefix) {
                                continue;
                            }

                            debug!("Found module file: {}", file_stem);

                            // Calculate import statement to insert at beginning of file
                            let module_name = path.file_name().unwrap().to_str().unwrap();
                            let import_statement = format!("import '{}'\n", module_name);

                            completions.push(CompletionItem {
                                label: file_stem.to_string(),
                                kind: CompletionItemKind::Module,
                                detail: Some(format!("{}", import_statement)),
                                documentation: Some(format!("Import module from {}", path.display())),
                                insert_text: file_stem.to_string(),
                                sort_text: Some(format!("zzz_{}", file_stem)), // Sort to back
                                additional_text_edits: vec![TextEdit {
                                    line: 0,
                                    character: 0,
                                    new_text: import_statement,
                                }],
                            });
                        }
                    }
                }
            }
        }

        // 2. Scan subdirectories for package-based imports (if package.toml exists)
        if let Some(ref pkg_config) = self.package_config {
            self.scan_subdirectories_for_modules(&self.project_root, &pkg_config.package_data.name, "", prefix, &already_imported, completions);
        }

        // 3. Scan standard library packages
        let std_dir = std::path::Path::new(&self.nature_root).join("std");
        debug!("Scanning std directory: {}", std_dir.display());

        if let Ok(entries) = std::fs::read_dir(&std_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    let path = entry.path();
                    if path.is_dir() {
                        if let Some(module_name) = path.file_name().and_then(|s| s.to_str()) {
                            // Exclude special directories
                            if [".", "..", "builtin"].contains(&module_name) {
                                continue;
                            }

                            // Skip already imported packages
                            if already_imported.contains(module_name) {
                                continue;
                            }

                            // Check prefix match
                            if !prefix.is_empty() && !module_name.starts_with(prefix) {
                                continue;
                            }

                            debug!("Found std package: {}", module_name);

                            // Standard library uses import package_name syntax
                            let import_statement = format!("import {}\n", module_name);

                            completions.push(CompletionItem {
                                label: module_name.to_string(),
                                kind: CompletionItemKind::Module,
                                detail: Some(format!("std: {}", import_statement)),
                                documentation: Some(format!("Import standard library package: {}", module_name)),
                                insert_text: module_name.to_string(),
                                sort_text: Some(format!("zzz_{}", module_name)), // Sort to back
                                additional_text_edits: vec![TextEdit {
                                    line: 0,
                                    character: 0,
                                    new_text: import_statement,
                                }],
                            });
                        }
                    }
                }
            }
        }
    }

    /// Recursively scan subdirectories for package-based module imports
    fn scan_subdirectories_for_modules(
        &self,
        base_dir: &str,
        package_name: &str,
        current_path: &str,
        prefix: &str,
        already_imported: &HashSet<String>,
        completions: &mut Vec<CompletionItem>,
    ) {
        let scan_dir = if current_path.is_empty() {
            std::path::PathBuf::from(base_dir)
        } else {
            std::path::PathBuf::from(base_dir).join(current_path)
        };

        if let Ok(entries) = std::fs::read_dir(&scan_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    let path = entry.path();

                    // Check subdirectories
                    if path.is_dir() {
                        if let Some(dir_name) = path.file_name().and_then(|s| s.to_str()) {
                            let new_path = if current_path.is_empty() {
                                dir_name.to_string()
                            } else {
                                format!("{}/{}", current_path, dir_name)
                            };

                            // Recursively scan subdirectory
                            self.scan_subdirectories_for_modules(base_dir, package_name, &new_path, prefix, already_imported, completions);
                        }
                    }
                    // Check .n files in subdirectories
                    else if path.is_file() && path.extension().and_then(|s| s.to_str()) == Some("n") {
                        if let Some(file_stem) = path.file_stem().and_then(|s| s.to_str()) {
                            // Skip if this is in the root directory (already handled by file-based imports)
                            if current_path.is_empty() {
                                continue;
                            }

                            // Build import path: package_name.folder.module_name
                            let module_import_path = if current_path.is_empty() {
                                format!("{}.{}", package_name, file_stem)
                            } else {
                                format!("{}.{}.{}", package_name, current_path.replace("/", "."), file_stem)
                            };

                            // Skip already imported modules
                            if already_imported.contains(file_stem) {
                                continue;
                            }

                            // Check prefix match (match against the module name, not full path)
                            if !prefix.is_empty() && !file_stem.starts_with(prefix) {
                                continue;
                            }

                            debug!("Found subdirectory module: {} at {}", file_stem, module_import_path);

                            let import_statement = format!("import {}\n", module_import_path);

                            completions.push(CompletionItem {
                                label: file_stem.to_string(),
                                kind: CompletionItemKind::Module,
                                detail: Some(format!("pkg: {}", import_statement)),
                                documentation: Some(format!("Import module from package: {}", module_import_path)),
                                insert_text: file_stem.to_string(),
                                sort_text: Some(format!("zzz_{}", file_stem)),
                                additional_text_edits: vec![TextEdit {
                                    line: 0,
                                    character: 0,
                                    new_text: import_statement,
                                }],
                            });
                        }
                    }
                }
            }
        }
    }

    /// Collect cross-file symbol completions from the workspace index.
    /// This enables typing e.g. "MyControll" and getting "MyController" from another file,
    /// with a selective auto-import so the symbol can be used directly.
    pub(crate) fn collect_workspace_symbol_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        let workspace_index = match &self.workspace_index {
            Some(idx) => idx,
            None => return,
        };

        if prefix.is_empty() || prefix.len() < 2 {
            return; // Need at least 2 chars to avoid flooding with results
        }

        debug!("Collecting workspace symbol completions with prefix '{}'", prefix);

        // Build a set of non-selectively imported module as_names
        let non_selective_imported: HashSet<String> = self.module.dependencies
            .iter()
            .filter(|dep| !dep.is_selective)
            .map(|dep| dep.as_name.clone())
            .collect();

        // Build a map of selectively imported modules: full_path -> &ImportStmt
        let selective_imports_by_path: std::collections::HashMap<&str, &ImportStmt> = self.module.dependencies
            .iter()
            .filter(|dep| dep.is_selective)
            .map(|dep| (dep.full_path.as_str(), dep as &ImportStmt))
            .collect();

        // Check which symbols are already selectively imported (by their visible name)
        let selectively_imported: HashSet<String> = self.module.dependencies
            .iter()
            .filter(|dep| dep.is_selective)
            .flat_map(|dep| {
                dep.select_items.iter()
                    .flat_map(|items| items.iter())
                    .map(|item| item.alias.clone().unwrap_or_else(|| item.ident.clone()))
            })
            .collect();

        // Collect symbol names already provided by local/module completions to avoid duplication
        let existing_labels: HashSet<String> = completions.iter().map(|c| c.label.clone()).collect();

        let package_name = self.package_config.as_ref().map(|c| c.package_data.name.as_str());

        let matching_symbols = workspace_index.find_symbols_by_prefix(prefix);

        // First pass: count how many distinct files provide each symbol name
        let mut name_sources: std::collections::HashMap<String, Vec<usize>> = std::collections::HashMap::new();
        for (i, sym) in matching_symbols.iter().enumerate() {
            name_sources.entry(sym.name.clone()).or_default().push(i);
        }

        // Build the builtin directory path so we can skip symbols that are
        // always in scope (e.g. println, print, …).
        let builtin_dir = {
            let mut p = std::path::PathBuf::from(&self.nature_root);
            p.push("std");
            p.push("builtin");
            p
        };

        for indexed_symbol in &matching_symbols {
            // Skip if this symbol is from the current file
            if indexed_symbol.file_path == self.module.path {
                continue;
            }

            // Skip builtin symbols — they are already provided by
            // collect_module_scope_fn_completions via the global scope.
            if std::path::Path::new(&indexed_symbol.file_path).starts_with(&builtin_dir) {
                continue;
            }

            // Skip if already selectively imported (already usable directly)
            if selectively_imported.contains(&indexed_symbol.name) {
                continue;
            }

            // Compute how to import the module containing this symbol
            let import_info = match workspace_index.compute_import_info(
                &indexed_symbol.file_path,
                &self.module.dir,
                &self.module.path,
                &self.project_root,
                &self.nature_root,
                package_name,
            ) {
                Some(info) => info,
                None => continue,
            };

            // Determine import state:
            // 1. Non-selective import exists for this module -> qualified access
            // 2. Selective import exists for same file -> extend the {..} list
            // 3. Not imported at all -> new selective import line
            let has_non_selective = non_selective_imported.contains(&import_info.module_as_name);
            let existing_selective = selective_imports_by_path.get(indexed_symbol.file_path.as_str());

            let kind = match indexed_symbol.kind {
                IndexedSymbolKind::Type => CompletionItemKind::Struct,
                IndexedSymbolKind::Function => CompletionItemKind::Function,
                IndexedSymbolKind::Variable => CompletionItemKind::Variable,
                IndexedSymbolKind::Constant => CompletionItemKind::Constant,
            };

            let kind_label = match indexed_symbol.kind {
                IndexedSymbolKind::Type => "type",
                IndexedSymbolKind::Function => "fn",
                IndexedSymbolKind::Variable => "var",
                IndexedSymbolKind::Constant => "const",
            };

            let file_stem = std::path::Path::new(&indexed_symbol.file_path)
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("")
                .to_string();

            // Check if multiple files provide a symbol with this name
            let has_duplicates = name_sources.get(&indexed_symbol.name)
                .map(|sources| sources.len() > 1)
                .unwrap_or(false);

            // Also check if a local symbol already exists with this name
            let conflicts_with_existing = existing_labels.contains(&indexed_symbol.name);
            let needs_disambiguation = has_duplicates || conflicts_with_existing;

            // Add () for functions, {} for structs (snippet syntax for direct use)
            let direct_insert = match indexed_symbol.kind {
                IndexedSymbolKind::Function => format!("{}($0)", indexed_symbol.name),
                IndexedSymbolKind::Type => format!("{}{{$0}}", indexed_symbol.name),
                _ => indexed_symbol.name.clone(),
            };

            if has_non_selective {
                // Case 1: Module already imported non-selectively -> use qualified access
                let label = if needs_disambiguation {
                    format!("{} ({})", indexed_symbol.name, import_info.module_as_name)
                } else {
                    indexed_symbol.name.clone()
                };

                let qualified_insert = match indexed_symbol.kind {
                    IndexedSymbolKind::Function => format!("{}.{}($0)", import_info.module_as_name, indexed_symbol.name),
                    IndexedSymbolKind::Type => format!("{}.{}{{$0}}", import_info.module_as_name, indexed_symbol.name),
                    _ => format!("{}.{}", import_info.module_as_name, indexed_symbol.name),
                };

                completions.push(CompletionItem {
                    label,
                    kind,
                    detail: Some(format!("{} (from {})", kind_label, import_info.module_as_name)),
                    documentation: Some(format!("from {} ({})", file_stem, indexed_symbol.file_path)),
                    insert_text: qualified_insert,
                    sort_text: Some(format!("zzy_{}", indexed_symbol.name)),
                    additional_text_edits: Vec::new(),
                });
            } else if let Some(sel_import) = existing_selective {
                // Case 2: Module already has a selective import -> extend the {..} list
                // Always disambiguate with source path so user knows where this comes from
                let label = if needs_disambiguation {
                    format!("{} ({})", indexed_symbol.name, file_stem)
                } else {
                    indexed_symbol.name.clone()
                };

                let additional_text_edits = if let Some(edit) = self.find_selective_import_insert_point(sel_import, &indexed_symbol.name) {
                    vec![edit]
                } else {
                    // Fallback: add a new selective import line
                    let selective_import = format!("{}.{{{}}}\n", import_info.import_base, indexed_symbol.name);
                    vec![TextEdit {
                        line: 0,
                        character: 0,
                        new_text: selective_import,
                    }]
                };

                completions.push(CompletionItem {
                    label,
                    kind,
                    detail: Some(format!("{} (add to import {})", kind_label, import_info.import_base)),
                    documentation: Some(format!("from {} ({})", file_stem, indexed_symbol.file_path)),
                    insert_text: direct_insert,
                    sort_text: Some(format!("zzy_{}", indexed_symbol.name)),
                    additional_text_edits,
                });
            } else {
                // Case 3: Module not imported at all -> new selective import line
                let selective_import = format!("{}.{{{}}}\n", import_info.import_base, indexed_symbol.name);

                let label = if needs_disambiguation {
                    format!("{} ({})", indexed_symbol.name, file_stem)
                } else {
                    indexed_symbol.name.clone()
                };

                completions.push(CompletionItem {
                    label,
                    kind,
                    detail: Some(format!("{} (auto import: {})", kind_label, selective_import.trim())),
                    documentation: Some(format!("from {} ({})", file_stem, indexed_symbol.file_path)),
                    insert_text: direct_insert,
                    sort_text: Some(format!("zzy_{}", indexed_symbol.name)),
                    additional_text_edits: vec![TextEdit {
                        line: 0,
                        character: 0,
                        new_text: selective_import,
                    }],
                });
            }
        }

        debug!("Added workspace symbol completions, total now: {}", completions.len());
    }

    /// Find the insert point to add a new symbol to an existing selective import.
    /// Returns a TextEdit that inserts ", symbolName" before the closing '}' of the import.
    fn find_selective_import_insert_point(
        &self,
        import_stmt: &ImportStmt,
        symbol_name: &str,
    ) -> Option<TextEdit> {
        // start/end are char offsets (from the lexer which works on Vec<char>).
        // Use the module's rope to convert char offsets to line/col positions.
        let rope = &self.module.rope;
        let end = import_stmt.end;
        let start = import_stmt.start;

        if end == 0 || end > rope.len_chars() {
            return None;
        }

        // Search backward from end to find '}' character
        let mut brace_char_idx = None;
        let mut pos = end;
        while pos > start {
            pos -= 1;
            let ch = rope.char(pos);
            if ch == '}' {
                brace_char_idx = Some(pos);
                break;
            }
        }

        let brace_char_idx = brace_char_idx?;

        // Convert char offset to line/character using the rope
        let line = rope.char_to_line(brace_char_idx);
        let line_start_char = rope.line_to_char(line);
        let character = brace_char_idx - line_start_char;

        Some(TextEdit {
            line,
            character,
            new_text: format!(", {}", symbol_name),
        })
    }
}
