use crate::analyzer::common::AstFnDef;
use crate::analyzer::symbol::{NodeId, Symbol, SymbolKind, SymbolTable};
use crate::project::Module;
use log::debug;
use std::collections::HashSet;

#[derive(Debug, Clone)]
pub struct CompletionItem {
    pub label: String, // Variable name
    pub kind: CompletionItemKind,
    pub detail: Option<String>, // Type information
    pub documentation: Option<String>,
    pub insert_text: String,                  // Insert text
    pub sort_text: Option<String>,            // Sort priority
    pub additional_text_edits: Vec<TextEdit>, // Additional text edits (for auto-import)
}

#[derive(Debug, Clone)]
pub struct TextEdit {
    pub line: usize,
    pub character: usize,
    pub new_text: String,
}

#[derive(Debug, Clone)]
pub enum CompletionItemKind {
    Variable,
    Parameter,
    Function,
    Constant,
    Module, // Module type for imports
    Struct, // Type definitions (structs, typedefs)
}

pub struct CompletionProvider<'a> {
    symbol_table: &'a mut SymbolTable,
    module: &'a mut Module,
    nature_root: String,
    project_root: String,
    package_config: Option<crate::analyzer::common::PackageConfig>,
}

impl<'a> CompletionProvider<'a> {
    pub fn new(
        symbol_table: &'a mut SymbolTable,
        module: &'a mut Module,
        nature_root: String,
        project_root: String,
        package_config: Option<crate::analyzer::common::PackageConfig>,
    ) -> Self {
        Self {
            symbol_table,
            module,
            nature_root,
            project_root,
            package_config,
        }
    }

    /// Main auto-completion entry function
    pub fn get_completions(&self, position: usize, text: &str) -> Vec<CompletionItem> {
        dbg!("get_completions", position, &self.module.ident, self.module.scope_id);

        // Check if in struct initialization context (type_name{ field: ... })
        if let Some((type_name, field_prefix)) = extract_struct_init_context(text, position) {
            debug!("Detected struct initialization context: type='{}', field_prefix='{}'", type_name, field_prefix);
            let current_scope_id = self.find_innermost_scope(self.module.scope_id, position);
            return self.get_struct_field_completions(&type_name, &field_prefix, current_scope_id);
        }

        let prefix = extract_prefix_at_position(text, position);

        // Check if in type member access context (variable.field)
        if let Some((var_name, member_prefix)) = extract_module_member_context(&prefix, position) {
            // First try as variable type member access
            let current_scope_id = self.find_innermost_scope(self.module.scope_id, position);
            if let Some(completions) = self.get_type_member_completions(&var_name, &member_prefix, current_scope_id) {
                debug!("Found type member completions for variable '{}'", var_name);
                return completions;
            }

            // If not a variable, try as module member access
            debug!("Detected module member access: {} and {}", var_name, member_prefix);
            return self.get_module_member_completions(&var_name, &member_prefix);
        }

        // Normal variable completion
        debug!("Getting completions at position {} with prefix '{}'", position, prefix);

        // 1. Find current scope based on position
        let current_scope_id = self.find_innermost_scope(self.module.scope_id, position);
        debug!("Found scope_id {} by positon {}", current_scope_id, position);

        // cannot auto-import in global module scope
        if current_scope_id == self.module.scope_id {
            return Vec::new();
        }

        // 2. Collect all visible variable symbols
        let mut completions = Vec::new();
        self.collect_variable_completions(current_scope_id, &prefix, &mut completions, position);

        // 3. Collect already-imported modules (show at top)
        self.collect_imported_module_completions(&prefix, &mut completions);

        // 4. Collect available modules (for auto-import)
        self.collect_module_completions(&prefix, &mut completions);

        // 5. Sort and filter
        self.sort_and_filter_completions(&mut completions, &prefix);

        debug!("Found {} completions", completions.len());
        dbg!(&completions);

        completions
    }

    /// Get auto-completions for module members
    pub fn get_module_member_completions(&self, imported_as_name: &str, prefix: &str) -> Vec<CompletionItem> {
        debug!("Getting module member completions for module '{}' with prefix '{}'", imported_as_name, prefix);

        let mut completions = Vec::new();

        let deps = &self.module.dependencies;

        let import_stmt = deps.iter().find(|&dep| dep.as_name == imported_as_name);
        if import_stmt.is_none() {
            return completions;
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

    /// Get auto-completions for type members (for struct fields and methods)
    pub fn get_type_member_completions(&self, var_name: &str, prefix: &str, current_scope_id: NodeId) -> Option<Vec<CompletionItem>> {
        debug!("Getting type member completions for variable '{}' with prefix '{}'", var_name, prefix);

        // 1. Find the variable in the current scope
        let var_symbol = self.find_variable_in_scope(var_name, current_scope_id)?;

        // 2. Get the variable's type
        use crate::analyzer::common::TypeKind;
        let (var_type_kind, typedef_symbol_id) = match &var_symbol.kind {
            SymbolKind::Var(var_decl) => {
                let var = var_decl.lock().unwrap();
                let type_ = &var.type_;

                debug!("Variable '{}' has type: {:?}, symbol_id: {}", var_name, type_.kind, type_.symbol_id);

                (type_.kind.clone(), type_.symbol_id)
            }
            _ => {
                debug!("Symbol is not a variable");
                return None;
            }
        };

        let mut completions = Vec::new();

        // 3. Handle direct struct type (inlined)
        if let TypeKind::Struct(_, _, properties) = &var_type_kind {
            debug!("Variable has direct struct type with {} fields", properties.len());
            for prop in properties {
                if prefix.is_empty() || prop.name.starts_with(prefix) {
                    completions.push(CompletionItem {
                        label: prop.name.clone(),
                        kind: CompletionItemKind::Variable,
                        detail: Some(format!("field: {}", prop.type_)),
                        documentation: None,
                        insert_text: prop.name.clone(),
                        sort_text: Some(format!("{:08}", prop.start)),
                        additional_text_edits: Vec::new(),
                    });
                }
            }
        }

        // 4. Handle typedef reference
        if matches!(var_type_kind, TypeKind::Ident) && typedef_symbol_id != 0 {
            debug!("Looking up typedef with symbol_id: {}", typedef_symbol_id);

            if let Some(typedef_symbol) = self.symbol_table.get_symbol_ref(typedef_symbol_id) {
                if let SymbolKind::Type(typedef) = &typedef_symbol.kind {
                    let typedef = typedef.lock().unwrap();
                    debug!(
                        "Found typedef: {}, type_expr.kind: {:?}, methods: {}",
                        typedef.ident,
                        typedef.type_expr.kind,
                        typedef.method_table.len()
                    );

                    // Add struct fields from typedef
                    if let TypeKind::Struct(_, _, properties) = &typedef.type_expr.kind {
                        debug!("Typedef struct has {} fields", properties.len());
                        for prop in properties {
                            if prefix.is_empty() || prop.name.starts_with(prefix) {
                                completions.push(CompletionItem {
                                    label: prop.name.clone(),
                                    kind: CompletionItemKind::Variable,
                                    detail: Some(format!("field: {}", prop.type_)),
                                    documentation: None,
                                    insert_text: prop.name.clone(),
                                    sort_text: Some(format!("{:08}", prop.start)),
                                    additional_text_edits: Vec::new(),
                                });
                            }
                        }
                    }

                    // Add methods for typedef
                    for method in typedef.method_table.values() {
                        let fndef = method.lock().unwrap();
                        if prefix.is_empty() || fndef.fn_name.starts_with(prefix) {
                            let signature = self.format_function_signature(&fndef);
                            let insert_text = if self.has_parameters(&fndef) {
                                format!("{}($0)", fndef.fn_name)
                            } else {
                                format!("{}()", fndef.fn_name)
                            };
                            completions.push(CompletionItem {
                                label: fndef.fn_name.clone(),
                                kind: CompletionItemKind::Function,
                                detail: Some(format!("fn: {}", signature)),
                                documentation: None,
                                insert_text,
                                sort_text: Some(format!("{:08}", fndef.symbol_start)),
                                additional_text_edits: Vec::new(),
                            });
                        }
                    }
                } else {
                    debug!("Symbol {} is not a Type", typedef_symbol_id);
                }
            }
        }

        // 5. Look for methods by searching for functions with impl_type matching this type
        // This handles cases like: fn config.helloWorld()
        if typedef_symbol_id != 0 {
            self.collect_impl_methods(typedef_symbol_id, prefix, &mut completions);
        }

        self.sort_and_filter_completions(&mut completions, prefix);
        debug!("Found {} type member completions", completions.len());

        if completions.is_empty() {
            None
        } else {
            Some(completions)
        }
    }

    /// Get auto-completions for struct fields during initialization (type_name{ field: ... })
    pub fn get_struct_field_completions(&self, type_name: &str, prefix: &str, current_scope_id: NodeId) -> Vec<CompletionItem> {
        debug!("Getting struct field completions for type '{}' with prefix '{}'", type_name, prefix);

        let mut completions = Vec::new();

        // Find the type symbol
        let type_symbol = self.find_type_in_scope(type_name, current_scope_id);
        if type_symbol.is_none() {
            debug!("Type '{}' not found in scope", type_name);
            return completions;
        }

        let type_symbol = type_symbol.unwrap();

        // Extract struct fields from the type
        if let SymbolKind::Type(typedef) = &type_symbol.kind {
            let typedef = typedef.lock().unwrap();

            use crate::analyzer::common::TypeKind;
            if let TypeKind::Struct(_, _, properties) = &typedef.type_expr.kind {
                debug!("Found struct type with {} fields", properties.len());

                for prop in properties {
                    if prefix.is_empty() || prop.name.starts_with(prefix) {
                        completions.push(CompletionItem {
                            label: prop.name.clone(),
                            kind: CompletionItemKind::Variable,
                            detail: Some(format!("{}: {}", prop.name, prop.type_)),
                            documentation: None,
                            insert_text: format!("{}: ", prop.name),
                            sort_text: Some(format!("{:08}", prop.start)),
                            additional_text_edits: Vec::new(),
                        });
                    }
                }
            } else {
                debug!("Type '{}' is not a struct type", type_name);
            }
        }

        self.sort_and_filter_completions(&mut completions, prefix);
        debug!("Found {} struct field completions", completions.len());

        completions
    }

    /// Collect methods implemented for a type (fn TypeName.method())
    fn collect_impl_methods(&self, typedef_symbol_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>) {
        // Search module scope for functions with matching impl_type
        let module_scope = self.symbol_table.find_scope(self.module.scope_id);
        for &symbol_id in &module_scope.symbols {
            if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                if let SymbolKind::Fn(fndef) = &symbol.kind {
                    let fndef = fndef.lock().unwrap();
                    // Check if this function's impl_type matches our typedef
                    if fndef.impl_type.symbol_id == typedef_symbol_id {
                        if prefix.is_empty() || fndef.fn_name.starts_with(prefix) {
                            debug!("Found impl method: {}", fndef.fn_name);
                            let signature = self.format_function_signature(&fndef);
                            let insert_text = if self.has_parameters(&fndef) {
                                format!("{}($0)", fndef.fn_name)
                            } else {
                                format!("{}()", fndef.fn_name)
                            };
                            completions.push(CompletionItem {
                                label: fndef.fn_name.clone(),
                                kind: CompletionItemKind::Function,
                                detail: Some(format!("fn: {}", signature)),
                                documentation: None,
                                insert_text,
                                sort_text: Some(format!("{:08}", fndef.symbol_start)),
                                additional_text_edits: Vec::new(),
                            });
                        }
                    }
                }
            }
        }
    }

    /// Find a variable in the current scope and parent scopes
    fn find_variable_in_scope(&self, var_name: &str, current_scope_id: NodeId) -> Option<&Symbol> {
        debug!("Searching for variable '{}' starting from scope {}", var_name, current_scope_id);
        let mut visited_scopes = HashSet::new();
        let mut current = current_scope_id;

        while current > 0 && !visited_scopes.contains(&current) {
            visited_scopes.insert(current);
            let scope = self.symbol_table.find_scope(current);
            debug!("Checking scope {} with {} symbols", current, scope.symbols.len());

            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    match &symbol.kind {
                        SymbolKind::Var(var_decl) => {
                            let var = var_decl.lock().unwrap();
                            debug!("Found var symbol: '{}' (looking for '{}')", var.ident, var_name);
                            if var.ident == var_name {
                                drop(var);
                                debug!("Variable '{}' found in scope {}", var_name, current);
                                return Some(symbol);
                            }
                        }
                        _ => {}
                    }
                }
            }

            current = scope.parent;
            if current == 0 {
                break;
            }
        }

        debug!("Variable '{}' not found in any scope", var_name);
        None
    }

    /// Find a type in the current scope and parent scopes
    fn find_type_in_scope(&self, type_name: &str, current_scope_id: NodeId) -> Option<&Symbol> {
        debug!("Searching for type '{}' starting from scope {}", type_name, current_scope_id);
        let mut visited_scopes = HashSet::new();
        let mut current = current_scope_id;

        while current > 0 && !visited_scopes.contains(&current) {
            visited_scopes.insert(current);
            let scope = self.symbol_table.find_scope(current);
            debug!("Checking scope {} with {} symbols", current, scope.symbols.len());

            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    match &symbol.kind {
                        SymbolKind::Type(typedef) => {
                            let type_def = typedef.lock().unwrap();
                            let symbol_name = extract_last_ident_part(&type_def.ident);
                            debug!(
                                "Found type symbol: '{}' (extracted: '{}', looking for '{}')",
                                type_def.ident, symbol_name, type_name
                            );
                            if symbol_name == type_name {
                                drop(type_def);
                                debug!("Type '{}' found in scope {}", type_name, current);
                                return Some(symbol);
                            }
                        }
                        _ => {}
                    }
                }
            }

            current = scope.parent;
            if current == 0 {
                break;
            }
        }

        debug!("Type '{}' not found in any scope", type_name);
        None
    }

    /// Check if function has parameters (excluding self)
    fn has_parameters(&self, fndef: &AstFnDef) -> bool {
        fndef.params.iter().any(|param| {
            let param_locked = param.lock().unwrap();
            param_locked.ident != "self"
        })
    }

    /// Format a function signature for display
    fn format_function_signature(&self, fndef: &AstFnDef) -> String {
        let mut params_str = String::new();
        let mut first = true;

        for param in fndef.params.iter() {
            let param_locked = param.lock().unwrap();

            // Skip 'self' parameter
            if param_locked.ident == "self" {
                continue;
            }

            if !first {
                params_str.push_str(", ");
            }
            first = false;

            // Use the ident field from Type which contains the original type name
            let type_str = if !param_locked.type_.ident.is_empty() {
                param_locked.type_.ident.clone()
            } else {
                param_locked.type_.to_string()
            };

            params_str.push_str(&format!("{} {}", type_str, param_locked.ident));
        }

        if fndef.rest_param && !params_str.is_empty() {
            params_str.push_str(", ...");
        }

        let return_type = fndef.return_type.to_string();
        let fn_prefix = if fndef.is_fx { "fx" } else { "fn" };
        format!("{}({}): {}", fn_prefix, params_str, return_type)
    }

    /// Find innermost scope containing the position starting from module scope
    fn find_innermost_scope(&self, scope_id: NodeId, position: usize) -> NodeId {
        let scope = self.symbol_table.find_scope(scope_id);
        debug!("[find_innermost_scope] scope_id {}, start {}, end {}", scope_id, scope.range.0, scope.range.1);

        // Check if current scope contains this position (range.1 == 0 means file-level scope)
        if position >= scope.range.0 && (position < scope.range.1 || scope.range.1 == 0) {
            // Check child scopes, find innermost scope
            for &child_id in &scope.children {
                let child_scope = self.symbol_table.find_scope(child_id);

                debug!(
                    "[find_innermost_scope] child scope_id {}, start {}, end {}",
                    scope_id, child_scope.range.0, child_scope.range.1
                );
                if position >= child_scope.range.0 && position < child_scope.range.1 {
                    return self.find_innermost_scope(child_id, position);
                }
            }

            return scope_id;
        }

        scope_id // If not in range, return current scope
    }

    /// Collect variable completion items
    fn collect_variable_completions(&self, current_scope_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>, position: usize) {
        let mut visited_scopes = HashSet::new();
        let mut current = current_scope_id;

        // Traverse upward from current scope
        while current > 0 && !visited_scopes.contains(&current) {
            visited_scopes.insert(current);

            let scope = self.symbol_table.find_scope(current);
            debug!("Searching scope {} with {} symbols", current, scope.symbols.len());

            // Iterate through all symbols in current scope
            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    dbg!("Found symbol will check", symbol.ident.clone(), prefix, symbol.ident.starts_with(prefix));

                    // Only handle variables and constants
                    match &symbol.kind {
                        SymbolKind::Var(_) | SymbolKind::Const(_) => {
                            if (prefix.is_empty() || symbol.ident.starts_with(prefix)) && symbol.pos < position {
                                let completion_item = self.create_completion_item(symbol);
                                debug!("Adding completion: {}", completion_item.label);
                                completions.push(completion_item);
                            }
                        }
                        _ => {}
                    }
                }
            }
            current = scope.parent;

            // If reached root scope, stop traversal
            if current == 0 {
                break;
            }
        }
    }

    /// Collect already-imported modules as completions (show at top)
    fn collect_imported_module_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        debug!("Collecting imported module completions with prefix '{}'", prefix);

        for import_stmt in &self.module.dependencies {
            let module_name = &import_stmt.as_name;

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
    fn collect_module_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
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

    /// Create completion item
    fn create_completion_item(&self, symbol: &Symbol) -> CompletionItem {
        let (kind, detail) = match &symbol.kind {
            SymbolKind::Var(var_decl) => {
                let detail = {
                    let var = var_decl.lock().unwrap();
                    format!("var: {}", var.type_)
                };
                (CompletionItemKind::Variable, Some(detail))
            }
            SymbolKind::Const(const_def) => {
                let detail = {
                    let const_val = const_def.lock().unwrap();
                    format!("const: {}", const_val.type_)
                };
                (CompletionItemKind::Constant, Some(detail))
            }
            _ => (CompletionItemKind::Variable, None),
        };

        CompletionItem {
            label: symbol.ident.clone(),
            kind,
            detail,
            documentation: None,
            insert_text: symbol.ident.clone(),
            sort_text: Some(format!("{:08}", symbol.pos)), // Sort by definition position
            additional_text_edits: Vec::new(),
        }
    }

    /// Create module member completion item
    fn create_module_completion_member(&self, symbol: &Symbol) -> CompletionItem {
        let (ident, kind, detail, insert_text, priority) = match &symbol.kind {
            SymbolKind::Var(var) => {
                let var = var.lock().unwrap();
                let detail = format!("var: {}", var.type_);
                let display_ident = extract_last_ident_part(&var.ident.clone());
                (display_ident.clone(), CompletionItemKind::Variable, Some(detail), display_ident, 2)
            }
            SymbolKind::Const(constdef) => {
                let constdef = constdef.lock().unwrap();
                let detail = format!("const: {}", constdef.type_);
                let display_ident = extract_last_ident_part(&constdef.ident.clone());
                (display_ident.clone(), CompletionItemKind::Constant, Some(detail), display_ident, 3)
            }
            SymbolKind::Fn(fndef) => {
                let fndef = fndef.lock().unwrap();
                let signature = self.format_function_signature(&fndef);
                let insert_text = if self.has_parameters(&fndef) {
                    format!("{}($0)", fndef.fn_name)
                } else {
                    format!("{}()", fndef.fn_name)
                };
                (fndef.fn_name.clone(), CompletionItemKind::Function, Some(signature), insert_text, 0)
            }
            SymbolKind::Type(typedef) => {
                let typedef = typedef.lock().unwrap();
                let detail = format!("type definition");
                let display_ident = extract_last_ident_part(&typedef.ident);
                (display_ident.clone(), CompletionItemKind::Struct, Some(detail), display_ident, 1)
            }
        };

        CompletionItem {
            label: ident.clone(),
            kind,
            detail,
            documentation: None,
            insert_text,
            sort_text: Some(format!("{}_{}", priority, ident)),
            additional_text_edits: Vec::new(),
        }
    }

    /// Sort and filter completion items
    fn sort_and_filter_completions(&self, completions: &mut Vec<CompletionItem>, prefix: &str) {
        // Deduplicate - based on label
        completions.sort_by(|a, b| a.label.cmp(&b.label));
        completions.dedup_by(|a, b| a.label == b.label);

        // Sort by: 1) kind priority, 2) prefix match, 3) alphabetically
        completions.sort_by(|a, b| {
            // Priority order: Function > Struct > Variable > Constant > Module
            let a_priority = match a.kind {
                CompletionItemKind::Function => 0,
                CompletionItemKind::Struct => 1,
                CompletionItemKind::Variable | CompletionItemKind::Parameter => 2,
                CompletionItemKind::Constant => 3,
                CompletionItemKind::Module => 4,
            };
            let b_priority = match b.kind {
                CompletionItemKind::Function => 0,
                CompletionItemKind::Struct => 1,
                CompletionItemKind::Variable | CompletionItemKind::Parameter => 2,
                CompletionItemKind::Constant => 3,
                CompletionItemKind::Module => 4,
            };

            // First sort by kind priority
            match a_priority.cmp(&b_priority) {
                std::cmp::Ordering::Equal => {
                    // Then by prefix match
                    let a_exact = a.label.starts_with(prefix);
                    let b_exact = b.label.starts_with(prefix);

                    match (a_exact, b_exact) {
                        (true, false) => std::cmp::Ordering::Less,
                        (false, true) => std::cmp::Ordering::Greater,
                        _ => {
                            // Finally sort alphabetically
                            a.label.cmp(&b.label)
                        }
                    }
                }
                other => other,
            }
        });

        // Limit number of results
        completions.truncate(50);
    }
}

/// Extract prefix at cursor position from text
pub fn extract_prefix_at_position(text: &str, position: usize) -> String {
    if position == 0 {
        return String::new();
    }

    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return String::new();
    }

    let mut start = position;

    // Search backward for start of identifier
    while start > 0 {
        let ch = chars[start - 1];
        if ch.is_alphanumeric() || ch == '_' || ch == '.' {
            start -= 1;
        } else {
            break;
        }
    }

    // Extract prefix
    chars[start..position].iter().collect()
}

/// Extract last identifier part from something like "io.main.writer" to get "writer"
fn extract_last_ident_part(ident: &str) -> String {
    if let Some(dot_pos) = ident.rfind('.') {
        ident[dot_pos + 1..].to_string()
    } else {
        ident.to_string()
    }
}

/// Detect if in module member access context, returns (module_name, member_prefix)
pub fn extract_module_member_context(prefix: &str, _position: usize) -> Option<(String, String)> {
    if let Some(dot_pos) = prefix.rfind('.') {
        let module_name = prefix[..dot_pos].to_string();
        let member_prefix = prefix[dot_pos + 1..].to_string();

        if !module_name.is_empty() {
            return Some((module_name, member_prefix));
        }
    }

    None
}

/// Detect if cursor is inside a struct initialization, returns (type_name, field_prefix)
/// Handles cases like: `config{ [cursor] }` or `config{ value: 42, [cursor] }`
pub fn extract_struct_init_context(text: &str, position: usize) -> Option<(String, String)> {
    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return None;
    }

    // Look backward to find the opening brace and type name
    let mut i = position;

    // First, extract any prefix at the cursor position
    let mut field_prefix_end = position;
    while field_prefix_end > 0 {
        let ch = chars[field_prefix_end - 1];
        if ch.is_alphanumeric() || ch == '_' {
            field_prefix_end -= 1;
        } else {
            break;
        }
    }
    let field_prefix: String = chars[field_prefix_end..position].iter().collect();

    // Look for opening brace
    while i > 0 {
        i -= 1;
        let ch = chars[i];

        if ch == '{' {
            // Found opening brace, now look for the type name before it
            let mut type_end = i;

            // Skip whitespace between type name and brace
            while type_end > 0 && chars[type_end - 1].is_whitespace() {
                type_end -= 1;
            }

            // Extract type name
            let mut type_start = type_end;
            while type_start > 0 {
                let ch = chars[type_start - 1];
                if ch.is_alphanumeric() || ch == '_' || ch == '.' {
                    type_start -= 1;
                } else {
                    break;
                }
            }

            if type_start < type_end {
                let type_name: String = chars[type_start..type_end].iter().collect();

                // Avoid treating test blocks as struct initializations: `test name { ... }`
                let mut kw_end = type_start;
                while kw_end > 0 && chars[kw_end - 1].is_whitespace() {
                    kw_end -= 1;
                }
                let mut kw_start = kw_end;
                while kw_start > 0 {
                    let ch = chars[kw_start - 1];
                    if ch.is_alphanumeric() || ch == '_' {
                        kw_start -= 1;
                    } else {
                        break;
                    }
                }
                if kw_start < kw_end {
                    let maybe_kw: String = chars[kw_start..kw_end].iter().collect();
                    if maybe_kw == "test" {
                        return None;
                    }
                }

                debug!("Detected struct init context: type='{}', field_prefix='{}'", type_name, field_prefix);
                return Some((type_name, field_prefix));
            }

            return None;
        } else if ch == '}' || ch == ';' {
            // We've left the struct initialization context
            return None;
        }
    }

    None
}
