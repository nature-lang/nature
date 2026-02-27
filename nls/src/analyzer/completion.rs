use crate::analyzer::common::{AstFnDef, ImportStmt};
use crate::analyzer::symbol::{NodeId, Symbol, SymbolKind, SymbolTable};
use crate::analyzer::workspace_index::{IndexedSymbolKind, WorkspaceIndex};
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
    Module,  // Module type for imports
    Struct,  // Type definitions (structs, typedefs)
    Keyword, // Language keywords
}

pub struct CompletionProvider<'a> {
    symbol_table: &'a mut SymbolTable,
    module: &'a mut Module,
    nature_root: String,
    project_root: String,
    package_config: Option<crate::analyzer::common::PackageConfig>,
    workspace_index: Option<&'a WorkspaceIndex>,
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
            workspace_index: None,
        }
    }

    pub fn with_workspace_index(mut self, index: &'a WorkspaceIndex) -> Self {
        self.workspace_index = Some(index);
        self
    }

    /// Main auto-completion entry function
    pub fn get_completions(&self, position: usize, text: &str) -> Vec<CompletionItem> {
        debug!("get_completions at position={}, module='{}', scope={}", position, &self.module.ident, self.module.scope_id);

        // Check if in selective import context (import module.{cursor})
        if let Some((module_path, item_prefix)) = extract_selective_import_context(text, position) {
            debug!("Detected selective import context: module='{}', prefix='{}'", module_path, item_prefix);
            return self.get_selective_import_completions(&module_path, &item_prefix);
        }

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

        // 2b. Collect functions/types defined in the current module scope
        self.collect_module_scope_fn_completions(&prefix, &mut completions);

        // 2c. Collect symbols from selective imports (import module.{symbol1, symbol2})
        self.collect_selective_import_symbol_completions(&prefix, &mut completions);

        // 3. Collect already-imported modules (show at top)
        self.collect_imported_module_completions(&prefix, &mut completions);

        // 4. Collect available modules (for auto-import)
        self.collect_module_completions(&prefix, &mut completions);

        // 5. Collect cross-file symbol completions (workspace index)
        self.collect_workspace_symbol_completions(&prefix, &mut completions);

        // 6. Collect keyword completions
        self.collect_keyword_completions(&prefix, &mut completions);

        // 7. Sort and filter
        self.sort_and_filter_completions(&mut completions, &prefix);

        debug!("Found {} completions", completions.len());

        completions
    }

    /// Get auto-completions for module members
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

        // 1. Find the variable in the current scope (try position-based first, then broad search)
        let var_symbol = self.find_variable_in_scope(var_name, current_scope_id)
            .or_else(|| {
                debug!("Variable '{}' not found via scope chain, trying broad search in module", var_name);
                self.find_variable_in_all_scopes(var_name, self.module.scope_id)
            })?;

        // 2. Get the variable's type
        use crate::analyzer::common::TypeKind;
        let (var_type_kind, mut typedef_symbol_id, type_ident) = match &var_symbol.kind {
            SymbolKind::Var(var_decl) => {
                let var = var_decl.lock().unwrap();
                let type_ = &var.type_;

                debug!("Variable '{}' has type: {:?}, symbol_id: {}, ident: '{}'", var_name, type_.kind, type_.symbol_id, type_.ident);

                // Extract the inner type ident for Ref/Ptr-wrapped types
                let inner_ident = match &type_.kind {
                    TypeKind::Ref(inner) | TypeKind::Ptr(inner) => {
                        if inner.symbol_id != 0 {
                            // Use inner symbol_id if available
                            debug!("Ref/Ptr inner type: {:?}, symbol_id: {}, ident: '{}'", inner.kind, inner.symbol_id, inner.ident);
                            (type_.kind.clone(), inner.symbol_id, inner.ident.clone())
                        } else {
                            (type_.kind.clone(), type_.symbol_id, inner.ident.clone())
                        }
                    }
                    _ => (type_.kind.clone(), type_.symbol_id, type_.ident.clone()),
                };

                inner_ident
            }
            _ => {
                debug!("Symbol is not a variable");
                return None;
            }
        };

        // 2b. Fallback: if typedef_symbol_id is still 0 and we have a type name, try to resolve it
        if typedef_symbol_id == 0 && !type_ident.is_empty() {
            debug!("typedef_symbol_id is 0, trying to resolve type by name: '{}'", type_ident);
            typedef_symbol_id = self.resolve_type_name_to_symbol_id(&type_ident);
            if typedef_symbol_id != 0 {
                debug!("Resolved type '{}' to symbol_id: {}", type_ident, typedef_symbol_id);
            }
        }

        let mut completions = Vec::new();
        let mut has_struct_fields = false;

        // 3. Handle direct struct type (inlined)
        if let TypeKind::Struct(_, _, properties) = &var_type_kind {
            debug!("Variable has direct struct type with {} fields", properties.len());
            has_struct_fields = true;
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

        // 4. Handle typedef reference (after reduction, kind may be Struct/Fn/etc, not Ident)
        if typedef_symbol_id != 0 {
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

                    // Add struct fields from typedef (only if not already added from inline type)
                    if !has_struct_fields {
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
        // Search global scope for functions with matching impl_type
        // Global symbols are stored in symbol_map (not symbols Vec), so we must check both.
        let global_scope = self.symbol_table.find_scope(self.symbol_table.global_scope_id);

        // Collect from symbols Vec
        for &symbol_id in &global_scope.symbols {
            self.check_impl_method(symbol_id, typedef_symbol_id, prefix, completions);
        }

        // Collect from symbol_map (where define_global_symbol stores them)
        for (_, &symbol_id) in &global_scope.symbol_map {
            self.check_impl_method(symbol_id, typedef_symbol_id, prefix, completions);
        }

        // Also check module scope (impl methods are registered there too)
        let module_scope = self.symbol_table.find_scope(self.module.scope_id);
        for &symbol_id in &module_scope.symbols {
            self.check_impl_method(symbol_id, typedef_symbol_id, prefix, completions);
        }
        for (_, &symbol_id) in &module_scope.symbol_map {
            self.check_impl_method(symbol_id, typedef_symbol_id, prefix, completions);
        }
    }

    /// Check if a symbol is an impl method for the given typedef and add it to completions
    fn check_impl_method(&self, symbol_id: NodeId, typedef_symbol_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>) {
        let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) else { return };
        let SymbolKind::Fn(fndef_mutex) = &symbol.kind else { return };
        let fndef = fndef_mutex.lock().unwrap();

        // Check if this function's impl_type matches our typedef
        if fndef.impl_type.symbol_id != typedef_symbol_id {
            return;
        }

        // Skip if already in completions (avoid duplicates from symbols + symbol_map overlap)
        if completions.iter().any(|c| c.label == fndef.fn_name) {
            return;
        }

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

    /// Find a variable in the current scope and parent scopes
    fn find_variable_in_scope(&self, var_name: &str, current_scope_id: NodeId) -> Option<&Symbol> {
        debug!("Searching for variable '{}' starting from scope {}", var_name, current_scope_id);
        let mut visited_scopes = HashSet::new();
        let mut current = current_scope_id;

        while current > 0 && !visited_scopes.contains(&current) {
            visited_scopes.insert(current);
            let scope = self.symbol_table.find_scope(current);
            debug!("Checking scope {} with {} symbols", current, scope.symbols.len());

            // Check symbols Vec
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

            // Also check symbol_map (global symbols from imported modules are only stored here)
            for (&ref _key, &symbol_id) in &scope.symbol_map {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    match &symbol.kind {
                        SymbolKind::Var(var_decl) => {
                            let var = var_decl.lock().unwrap();
                            let symbol_name = extract_last_ident_part(&var.ident);
                            if symbol_name == var_name {
                                drop(var);
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

    /// Broad search: find a variable by name across ALL child scopes of a given scope.
    /// Used as fallback when position-based scope resolution fails (e.g., during typing when
    /// the parse tree is incomplete and scope ranges are wrong).
    fn find_variable_in_all_scopes(&self, var_name: &str, start_scope_id: NodeId) -> Option<&Symbol> {
        let mut worklist = vec![start_scope_id];
        let mut visited = HashSet::new();

        while let Some(scope_id) = worklist.pop() {
            if !visited.insert(scope_id) {
                continue;
            }
            let scope = self.symbol_table.find_scope(scope_id);

            // Check symbols in this scope
            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    if let SymbolKind::Var(var_decl) = &symbol.kind {
                        let var = var_decl.lock().unwrap();
                        if var.ident == var_name {
                            drop(var);
                            debug!("Variable '{}' found via broad search in scope {}", var_name, scope_id);
                            return Some(symbol);
                        }
                    }
                }
            }

            // Check symbol_map
            for (_, &symbol_id) in &scope.symbol_map {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    if let SymbolKind::Var(var_decl) = &symbol.kind {
                        let var = var_decl.lock().unwrap();
                        let symbol_name = extract_last_ident_part(&var.ident);
                        if symbol_name == var_name {
                            drop(var);
                            debug!("Variable '{}' found via broad search in symbol_map of scope {}", var_name, scope_id);
                            return Some(symbol);
                        }
                    }
                }
            }

            // Add children to worklist
            worklist.extend(&scope.children);
        }

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

            // Check symbols Vec
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

            // Also check symbol_map (global symbols from imported modules are only stored here)
            for (&ref _key, &symbol_id) in &scope.symbol_map {
                if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                    match &symbol.kind {
                        SymbolKind::Type(typedef) => {
                            let type_def = typedef.lock().unwrap();
                            let symbol_name = extract_last_ident_part(&type_def.ident);
                            if symbol_name == type_name {
                                drop(type_def);
                                debug!("Type '{}' found in symbol_map of scope {}", type_name, current);
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

    /// Resolve a type name (possibly unqualified or partially qualified) to a symbol_id.
    /// Tries multiple lookup strategies: direct, module-qualified, and global search.
    fn resolve_type_name_to_symbol_id(&self, type_name: &str) -> NodeId {
        // 1. Try exact match in the module scope's symbol_map
        let module_scope = self.symbol_table.find_scope(self.module.scope_id);
        if let Some(&symbol_id) = module_scope.symbol_map.get(type_name) {
            return symbol_id;
        }

        // 2. Try fully-qualified name: module_ident.type_name
        if let Some(symbol_id) = self.symbol_table.find_module_symbol_id(&self.module.ident, type_name) {
            return symbol_id;
        }

        // 3. Try as-is in global scope (might already be fully qualified)
        if let Some(symbol_id) = self.symbol_table.find_symbol_id(type_name, self.symbol_table.global_scope_id) {
            return symbol_id;
        }

        // 4. Extract last part and try module-qualified (handles "module.type" stored as "type")
        let short_name = extract_last_ident_part(type_name);
        if short_name != type_name {
            if let Some(symbol_id) = self.symbol_table.find_module_symbol_id(&self.module.ident, &short_name) {
                return symbol_id;
            }
        }

        // 5. Search through imports
        for import in &self.module.dependencies {
            if let Some(symbol_id) = self.symbol_table.find_module_symbol_id(&import.module_ident, &short_name) {
                return symbol_id;
            }
        }

        0
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
        format!("fn({}): {}", params_str, return_type)
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
                    debug!("Found symbol: ident='{}', prefix='{}', matches={}", symbol.ident, prefix, symbol.ident.starts_with(prefix));

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

    /// Collect functions and types defined at the module scope level.
    /// These are not in scope.symbols (only in global_scope.symbol_map),
    /// so collect_variable_completions misses them.
    fn collect_module_scope_fn_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        let module_scope = self.symbol_table.find_scope(self.module.scope_id);
        let existing_labels: HashSet<String> = completions.iter().map(|c| c.label.clone()).collect();

        // Check both symbols vec and symbol_map in the module scope
        let mut check_symbol = |symbol_id: NodeId| {
            let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) else { return };
            let symbol_name = extract_last_ident_part(&symbol.ident);

            if !prefix.is_empty() && !symbol_name.starts_with(prefix) {
                return;
            }
            if existing_labels.contains(&symbol_name) {
                return;
            }

            match &symbol.kind {
                SymbolKind::Fn(fndef_mutex) => {
                    let fndef = fndef_mutex.lock().unwrap();
                    // Skip test functions and impl methods
                    if fndef.is_test || fndef.impl_type.kind.is_exist() {
                        return;
                    }
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
                SymbolKind::Type(typedef_mutex) => {
                    let typedef = typedef_mutex.lock().unwrap();
                    completions.push(CompletionItem {
                        label: symbol_name.clone(),
                        kind: CompletionItemKind::Struct,
                        detail: Some(format!("type: {}", typedef.ident)),
                        documentation: None,
                        insert_text: symbol_name,
                        sort_text: Some(format!("{:08}", typedef.symbol_start)),
                        additional_text_edits: Vec::new(),
                    });
                }
                _ => {}
            }
        };

        for &symbol_id in &module_scope.symbols {
            check_symbol(symbol_id);
        }
        for (_, &symbol_id) in &module_scope.symbol_map {
            check_symbol(symbol_id);
        }
    }

    /// Collect symbols from selective imports as direct completions.
    /// When the user has `import forest.app.configuration.{configuration}`, the symbol
    /// `configuration` should appear as a completion in function bodies.
    fn collect_selective_import_symbol_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
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
    fn collect_imported_module_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
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

    /// Collect cross-file symbol completions from the workspace index.
    /// This enables typing e.g. "MyControll" and getting "MyController" from another file,
    /// with a selective auto-import so the symbol can be used directly.
    fn collect_workspace_symbol_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
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

        for indexed_symbol in &matching_symbols {
            // Skip if this symbol is from the current file
            if indexed_symbol.file_path == self.module.path {
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

    /// Collect keyword completions matching the current prefix.
    fn collect_keyword_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        if prefix.is_empty() {
            return;
        }

        const KEYWORDS: &[(&str, &str, &str)] = &[
            ("fn", "fn name($0) {\n\t\n}", "Function definition"),
            ("if", "if $0 {\n\t\n}", "If statement"),
            ("else", "else {\n\t$0\n}", "Else clause"),
            ("for", "for $0 {\n\t\n}", "For loop"),
            ("var", "var $0", "Variable declaration"),
            ("let", "let $0", "Let binding (error unwrap)"),
            ("return", "return $0", "Return statement"),
            ("import", "import $0", "Import module"),
            ("type", "type $0 = ", "Type definition"),
            ("match", "match $0 {\n\t\n}", "Match expression"),
            ("continue", "continue", "Continue loop"),
            ("break", "break", "Break loop"),
            ("as", "as $0", "Type cast"),
            ("is", "is $0", "Type test"),
            ("in", "in $0", "In operator"),
            ("true", "true", "Boolean true"),
            ("false", "false", "Boolean false"),
            ("null", "null", "Null value"),
            ("throw", "throw $0", "Throw error"),
            ("try", "try {\n\t$0\n} catch err {\n\t\n}", "Try-catch block"),
            ("catch", "catch $0 {\n\t\n}", "Catch clause"),
            ("go", "go $0", "Spawn coroutine"),
            ("select", "select {\n\t$0\n}", "Select statement"),
        ];

        let lower_prefix = prefix.to_lowercase();
        for &(kw, snippet, detail) in KEYWORDS {
            if kw.starts_with(&lower_prefix) && kw != lower_prefix {
                completions.push(CompletionItem {
                    label: kw.to_string(),
                    kind: CompletionItemKind::Keyword,
                    detail: Some(detail.to_string()),
                    documentation: None,
                    insert_text: snippet.to_string(),
                    sort_text: Some(format!("90_{}", kw)), // low priority so symbols come first
                    additional_text_edits: Vec::new(),
                });
            }
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
                CompletionItemKind::Keyword => 5,
            };
            let b_priority = match b.kind {
                CompletionItemKind::Function => 0,
                CompletionItemKind::Struct => 1,
                CompletionItemKind::Variable | CompletionItemKind::Parameter => 2,
                CompletionItemKind::Constant => 3,
                CompletionItemKind::Module => 4,
                CompletionItemKind::Keyword => 5,
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

/// Public version of extract_last_ident_part for use by other modules.
pub fn extract_last_ident_part_pub(ident: &str) -> String {
    extract_last_ident_part(ident)
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

/// Detect if cursor is inside a selective import `{...}` block.
/// Returns (module_path, item_prefix) where module_path is the import path before `.{`
/// Example: `import forest.app.create.{cre|` -> ("forest.app.create", "cre")
pub fn extract_selective_import_context(text: &str, position: usize) -> Option<(String, String)> {
    let chars: Vec<char> = text.chars().collect();
    if position > chars.len() {
        return None;
    }

    // Extract any prefix at cursor
    let mut prefix_start = position;
    while prefix_start > 0 {
        let ch = chars[prefix_start - 1];
        if ch.is_alphanumeric() || ch == '_' {
            prefix_start -= 1;
        } else {
            break;
        }
    }
    let item_prefix: String = chars[prefix_start..position].iter().collect();

    // Look backward from prefix_start (or position) to find `{`
    let mut i = prefix_start;
    while i > 0 {
        i -= 1;
        let ch = chars[i];

        if ch == '{' {
            // Found `{` — now check if it's preceded by `.` and then an import path
            // Before `{` there should be a `.`
            let mut dot_pos = i;
            // skip whitespace between `.` and `{`
            while dot_pos > 0 && chars[dot_pos - 1].is_whitespace() {
                dot_pos -= 1;
            }
            if dot_pos == 0 || chars[dot_pos - 1] != '.' {
                return None;
            }
            let dot_idx = dot_pos - 1;

            // Before the `.` should be the module path (ident chars and dots)
            let path_end = dot_idx;
            let mut path_start = path_end;
            while path_start > 0 {
                let ch = chars[path_start - 1];
                if ch.is_alphanumeric() || ch == '_' || ch == '.' {
                    path_start -= 1;
                } else {
                    break;
                }
            }

            if path_start >= path_end {
                return None;
            }

            let module_path: String = chars[path_start..path_end].iter().collect();

            // Check that `import` keyword precedes the module path
            let mut kw_end = path_start;
            while kw_end > 0 && chars[kw_end - 1].is_whitespace() {
                kw_end -= 1;
            }
            let mut kw_start = kw_end;
            while kw_start > 0 && chars[kw_start - 1].is_alphabetic() {
                kw_start -= 1;
            }
            let keyword: String = chars[kw_start..kw_end].iter().collect();
            if keyword != "import" {
                return None;
            }

            debug!("Detected selective import context: module='{}', prefix='{}'", module_path, item_prefix);
            return Some((module_path, item_prefix));
        } else if ch == '}' || ch == ';' {
            // `}` means we're past/outside the braces already
            return None;
        }
        // Skip over commas, spaces, other ident chars (already-typed items)
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
