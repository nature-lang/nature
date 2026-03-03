use crate::analyzer::common::TypeKind;
use crate::analyzer::symbol::{NodeId, Symbol, SymbolKind};
use log::debug;
use std::collections::HashSet;

use super::context::extract_last_ident_part;
use super::{CompletionItem, CompletionItemKind, CompletionProvider};

impl<'a> CompletionProvider<'a> {
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
    pub(crate) fn collect_impl_methods(&self, typedef_symbol_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>) {
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

    /// Collect variable completion items
    pub(crate) fn collect_variable_completions(&self, current_scope_id: NodeId, prefix: &str, completions: &mut Vec<CompletionItem>, position: usize) {
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
    ///
    /// Also scans the **global scope** where builtin functions (println,
    /// print, panic, assert, …) are registered.  Builtins use
    /// `module_ident = ""` which maps to the global scope.
    pub(crate) fn collect_module_scope_fn_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
        let module_scope_id = self.module.scope_id;
        let global_scope_id = self.symbol_table.global_scope_id;

        let existing_labels: HashSet<String> = completions.iter().map(|c| c.label.clone()).collect();

        // Closure that inspects one symbol and maybe pushes a completion.
        let mut check_symbol = |symbol_id: NodeId, is_builtin: bool| {
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
                    let detail = if is_builtin {
                        format!("fn: {} (builtin)", signature)
                    } else {
                        format!("fn: {}", signature)
                    };
                    completions.push(CompletionItem {
                        label: fndef.fn_name.clone(),
                        kind: CompletionItemKind::Function,
                        detail: Some(detail),
                        documentation: None,
                        insert_text,
                        sort_text: Some(format!("{:08}", fndef.symbol_start)),
                        additional_text_edits: Vec::new(),
                    });
                }
                SymbolKind::Type(typedef_mutex) => {
                    // Skip builtin types when prefix is empty — too noisy
                    if is_builtin && prefix.is_empty() {
                        return;
                    }
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

        // 1. Current module scope
        let module_scope = self.symbol_table.find_scope(module_scope_id);
        for &symbol_id in &module_scope.symbols {
            check_symbol(symbol_id, false);
        }
        let module_symbol_map: Vec<NodeId> = module_scope.symbol_map.values().copied().collect();
        for symbol_id in module_symbol_map {
            check_symbol(symbol_id, false);
        }

        // 2. Global scope (builtins — println, print, panic, assert, …)
        //    Only include builtins when the user has typed a prefix; with an
        //    empty prefix we limit noise to local-scope items only.
        debug!(
            "global_scope_id={}, module_scope_id={}, prefix='{}', will_scan_global={}",
            global_scope_id,
            module_scope_id,
            prefix,
            global_scope_id != module_scope_id && !prefix.is_empty()
        );
        if global_scope_id != module_scope_id && !prefix.is_empty() {
            let global_scope = self.symbol_table.find_scope(global_scope_id);
            debug!(
                "global scope: {} symbols, {} symbol_map entries",
                global_scope.symbols.len(),
                global_scope.symbol_map.len()
            );
            for &symbol_id in &global_scope.symbols {
                if let Some(sym) = self.symbol_table.get_symbol_ref(symbol_id) {
                    debug!("  global symbol: id={}, ident='{}', kind={:?}", symbol_id, sym.ident, std::mem::discriminant(&sym.kind));
                }
                check_symbol(symbol_id, true);
            }
            let global_symbol_map: Vec<NodeId> = global_scope.symbol_map.values().copied().collect();
            for symbol_id in global_symbol_map {
                check_symbol(symbol_id, true);
            }
        }
    }
}
