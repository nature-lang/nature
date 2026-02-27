//! Go-to-definition, find references, prepare rename, rename, and shared scope/symbol helpers.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::utils::{extract_word_at_offset_rope, is_ident_char, offset_to_position};

use super::Backend;

impl Backend {
    pub(crate) async fn handle_goto_definition(&self, params: GotoDefinitionParams) -> Option<GotoDefinitionResponse> {
        let uri = params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        if project.is_building.load(std::sync::atomic::Ordering::SeqCst) {
            return None;
        }

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;

        let (word, word_start, _) = extract_word_at_offset_rope(rope, char_offset)?;

        debug!("goto_definition at offset {}, word: '{}'", char_offset, word);

        let symbol_table = project.symbol_table.lock().ok()?;
        let current_scope_id = find_innermost_scope(&symbol_table, module.scope_id, char_offset);

        // Check if this is a method/field access: receiver.method
        // If there's a dot before the word, prioritize method resolution so that
        // e.g. `myApp.start()` resolves to `fn app.start()` instead of an unrelated
        // symbol named `start`.
        if word_start > 0 {
            let ch_before = rope.char(word_start - 1);
            if ch_before == '.' && word_start >= 2 {
                // Extract the receiver name before the dot
                if let Some((receiver, _, _)) = extract_word_at_offset_rope(rope, word_start - 2) {
                    debug!("goto_definition: detected method call {}.{}", receiver, word);
                    if let Some(result) = resolve_method_definition(
                        &symbol_table, module, &receiver, &word, current_scope_id, &module_db,
                    ) {
                        return Some(result);
                    }
                    // Method resolution failed — fall through to normal resolution
                }
            }
        }

        // Normal resolution (local, global, import, selective import)
        if let Some(result) = resolve_definition_location(&symbol_table, module, &word, current_scope_id, &module_db) {
            return Some(result);
        }

        None
    }

    pub(crate) async fn handle_references(&self, params: ReferenceParams) -> Option<Vec<Location>> {
        let uri = params.text_document_position.text_document.uri;
        let position = params.text_document_position.position;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        if project.is_building.load(std::sync::atomic::Ordering::SeqCst) {
            return None;
        }

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;

        let (word, _, _) = extract_word_at_offset_rope(rope, char_offset)?;

        debug!("find_references at offset {}, word: '{}'", char_offset, word);

        let symbol_table = project.symbol_table.lock().ok()?;
        let current_scope_id = find_innermost_scope(&symbol_table, module.scope_id, char_offset);

        let target_symbol_id = resolve_target_symbol_id(&symbol_table, module, &word, current_scope_id)?;

        let mut results = Vec::new();
        for m in module_db.iter() {
            let m_text = m.rope.to_string();
            collect_references_in_text(
                &m_text, &word, &m.rope, &m.path,
                &symbol_table, m.scope_id, target_symbol_id,
                &mut results,
            );
        }

        if results.is_empty() { None } else { Some(results) }
    }

    pub(crate) async fn handle_prepare_rename(&self, params: TextDocumentPositionParams) -> Option<PrepareRenameResponse> {
        let uri = params.text_document.uri;
        let position = params.position;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;

        let (word, word_start, word_end) = extract_word_at_offset_rope(rope, char_offset)?;

        let symbol_table = project.symbol_table.lock().ok()?;
        let current_scope_id = find_innermost_scope(&symbol_table, module.scope_id, char_offset);

        let _target_id = resolve_target_symbol_id(&symbol_table, module, &word, current_scope_id)?;

        let start_pos = offset_to_position(word_start, rope)?;
        let end_pos = offset_to_position(word_end, rope)?;

        Some(PrepareRenameResponse::Range(Range::new(start_pos, end_pos)))
    }

    pub(crate) async fn handle_rename(&self, params: RenameParams) -> Option<WorkspaceEdit> {
        let uri = params.text_document_position.text_document.uri;
        let position = params.text_document_position.position;
        let new_name = params.new_name;

        let file_path = uri.path();
        let project = self.get_file_project(file_path)?;

        let module_index = {
            let module_handled = project.module_handled.lock().ok()?;
            module_handled.get(file_path)?.clone()
        };

        let module_db = project.module_db.lock().ok()?;
        let module = module_db.get(module_index)?;

        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;

        let (word, _, _) = extract_word_at_offset_rope(rope, char_offset)?;

        debug!("rename '{}' -> '{}' at offset {}", word, new_name, char_offset);

        let symbol_table = project.symbol_table.lock().ok()?;
        let current_scope_id = find_innermost_scope(&symbol_table, module.scope_id, char_offset);

        let target_symbol_id = resolve_target_symbol_id(&symbol_table, module, &word, current_scope_id)?;

        let mut all_locations: Vec<Location> = Vec::new();
        for m in module_db.iter() {
            let m_text = m.rope.to_string();
            collect_references_in_text(
                &m_text, &word, &m.rope, &m.path,
                &symbol_table, m.scope_id, target_symbol_id,
                &mut all_locations,
            );
        }

        if all_locations.is_empty() {
            return None;
        }

        let mut changes: std::collections::HashMap<Url, Vec<TextEdit>> = std::collections::HashMap::new();
        for loc in all_locations {
            changes.entry(loc.uri).or_default().push(TextEdit {
                range: loc.range,
                new_text: new_name.clone(),
            });
        }

        Some(WorkspaceEdit {
            changes: Some(changes),
            document_changes: None,
            change_annotations: None,
        })
    }
}

// ─── Shared helpers ─────────────────────────────────────────────────────────────

/// Find the innermost scope containing the given position.
pub(crate) fn find_innermost_scope(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    scope_id: crate::analyzer::symbol::NodeId,
    position: usize,
) -> crate::analyzer::symbol::NodeId {
    let scope = symbol_table.find_scope(scope_id);

    if position >= scope.range.0 && (position < scope.range.1 || scope.range.1 == 0) {
        for &child_id in &scope.children {
            let child_scope = symbol_table.find_scope(child_id);
            if position >= child_scope.range.0 && position < child_scope.range.1 {
                return find_innermost_scope(symbol_table, child_id, position);
            }
        }
        return scope_id;
    }

    scope_id
}

/// Resolve the definition location for a symbol at the cursor position.
fn resolve_definition_location(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module: &crate::project::Module,
    word: &str,
    current_scope_id: crate::analyzer::symbol::NodeId,
    module_db: &[crate::project::Module],
) -> Option<GotoDefinitionResponse> {
    use crate::utils::format_global_ident;

    let symbol_to_location = |symbol: &crate::analyzer::symbol::Symbol| -> Option<GotoDefinitionResponse> {
        let (def_start, def_end) = get_symbol_span(symbol);
        if def_start == 0 && def_end == 0 {
            return None;
        }

        let def_module = find_module_for_scope(symbol_table, symbol.defined_in, module_db, module)?;
        let def_rope = &def_module.rope;

        let start_pos = offset_to_position(def_start, def_rope)?;
        let end_pos = offset_to_position(def_end, def_rope)?;
        let uri = Url::from_file_path(&def_module.path).ok()?;

        Some(GotoDefinitionResponse::Scalar(Location::new(
            uri,
            Range::new(start_pos, end_pos),
        )))
    };

    // 1. Local symbol lookup
    if let Some(symbol_id) = symbol_table.lookup_symbol(word, current_scope_id) {
        if let Some(symbol) = symbol_table.get_symbol_ref(symbol_id) {
            return symbol_to_location(symbol);
        }
    }

    // 2. Module-qualified global symbol
    let global_ident = format_global_ident(module.ident.clone(), word.to_string());
    if let Some(symbol) = symbol_table.find_global_symbol(&global_ident) {
        return symbol_to_location(symbol);
    }

    // 3. Imported module — jump to the imported file
    for import in &module.dependencies {
        if import.as_name == word {
            if !import.full_path.is_empty() {
                let uri = Url::from_file_path(&import.full_path).ok()?;
                return Some(GotoDefinitionResponse::Scalar(Location::new(
                    uri,
                    Range::new(Position::new(0, 0), Position::new(0, 0)),
                )));
            }
        }
    }

    // 4. Selective imports
    for import in &module.dependencies {
        if import.is_selective {
            if let Some(ref items) = import.select_items {
                for item in items {
                    let effective_name = item.alias.as_deref().unwrap_or(&item.ident);
                    if effective_name == word {
                        let imported_global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                        if let Some(symbol) = symbol_table.find_global_symbol(&imported_global_ident) {
                            return symbol_to_location(symbol);
                        }
                    }
                }
            }
        }
    }

    None
}

/// Extract the (symbol_id, ident) from an inner type of a Ref/Ptr wrapper.
/// Handles resolved inner types (with symbol_id or ident) as well as
/// `TypeKind::Struct` where the ident lives inside the kind variant.
fn extract_inner_type_info(
    inner: &crate::analyzer::common::Type,
    outer: &crate::analyzer::common::Type,
) -> (crate::analyzer::symbol::NodeId, String) {
    use crate::analyzer::common::TypeKind;

    if inner.symbol_id != 0 {
        (inner.symbol_id, inner.ident.clone())
    } else if !inner.ident.is_empty() {
        (0, inner.ident.clone())
    } else {
        // Inner ident is empty — try to extract from inner kind (e.g., Struct)
        match &inner.kind {
            TypeKind::Struct(struct_ident, _, _) => {
                debug!("extract_inner_type_info: extracted struct ident from inner kind: '{}'", struct_ident);
                (inner.symbol_id, struct_ident.clone())
            }
            _ => (outer.symbol_id, outer.ident.clone()),
        }
    }
}

/// Resolve the definition location for a method call like `receiver.method()`.
/// Finds the receiver variable, determines its type, and looks up the method definition.
fn resolve_method_definition(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module: &crate::project::Module,
    receiver_name: &str,
    method_name: &str,
    current_scope_id: crate::analyzer::symbol::NodeId,
    module_db: &[crate::project::Module],
) -> Option<GotoDefinitionResponse> {
    use crate::analyzer::common::TypeKind;
    use crate::analyzer::symbol::SymbolKind;

    debug!("resolve_method_definition: receiver='{}', method='{}'", receiver_name, method_name);

    let symbol_to_location = |symbol: &crate::analyzer::symbol::Symbol| -> Option<GotoDefinitionResponse> {
        let (def_start, def_end) = get_symbol_span(symbol);
        if def_start == 0 && def_end == 0 {
            return None;
        }
        let def_module = find_module_for_scope(symbol_table, symbol.defined_in, module_db, module)?;
        let start_pos = offset_to_position(def_start, &def_module.rope)?;
        let end_pos = offset_to_position(def_end, &def_module.rope)?;
        let uri = Url::from_file_path(&def_module.path).ok()?;
        Some(GotoDefinitionResponse::Scalar(Location::new(uri, Range::new(start_pos, end_pos))))
    };

    // 1. Find the receiver variable
    let receiver_symbol_id = symbol_table.lookup_symbol(receiver_name, current_scope_id)?;
    let receiver_symbol = symbol_table.get_symbol_ref(receiver_symbol_id)?;

    // 2. Get the receiver's type — unwrap Ref/Ptr and try to resolve symbol_id
    let (mut type_symbol_id, mut _type_ident) = match &receiver_symbol.kind {
        SymbolKind::Var(var_decl) => {
            let var = var_decl.lock().unwrap();
            let type_ = &var.type_;

            debug!("resolve_method_definition: var type kind={:?}, ident='{}', symbol_id={}", type_.kind, type_.ident, type_.symbol_id);

            // Unwrap Ref/Ptr to get the inner type.
            // Two representations exist:
            //   1. Fully reduced: TypeKind::Ref(inner) / TypeKind::Ptr(inner)
            //   2. Unreduced:     TypeKind::Ident with ident="ref"/"ptr" and args=[inner_type]
            let (sym_id, ident) = match &type_.kind {
                TypeKind::Ref(inner) | TypeKind::Ptr(inner) => {
                    debug!("resolve_method_definition: inner kind={:?}, ident='{}', symbol_id={}", inner.kind, inner.ident, inner.symbol_id);
                    extract_inner_type_info(inner, type_)
                }
                TypeKind::Ident if (type_.ident == "ref" || type_.ident == "ptr") && !type_.args.is_empty() => {
                    // Unreduced ref<T>/ptr<T>: the inner type is in args[0]
                    let inner = &type_.args[0];
                    debug!("resolve_method_definition: unreduced {}<> — inner kind={:?}, ident='{}', symbol_id={}", type_.ident, inner.kind, inner.ident, inner.symbol_id);
                    extract_inner_type_info(inner, type_)
                }
                _ => (type_.symbol_id, type_.ident.clone()),
            };
            (sym_id, ident)
        }
        _ => return None,
    };

    // 2b. Fallback: if type_symbol_id is still 0, try resolving by type name
    if type_symbol_id == 0 && !_type_ident.is_empty() && _type_ident != "ref" && _type_ident != "ptr" {
        debug!("resolve_method_definition: trying to resolve type by name: '{}'", _type_ident);
        // Try module-qualified lookup
        let short_name = if let Some(dot_pos) = _type_ident.rfind('.') {
            _type_ident[dot_pos + 1..].to_string()
        } else {
            _type_ident.clone()
        };

        // Try in module scope
        if let Some(sym_id) = symbol_table.find_module_symbol_id(&module.ident, &short_name) {
            type_symbol_id = sym_id;
            debug!("resolve_method_definition: resolved '{}' to symbol_id {} via module scope", short_name, sym_id);
        }

        // Try in imported module scopes
        if type_symbol_id == 0 {
            for import in &module.dependencies {
                if let Some(sym_id) = symbol_table.find_module_symbol_id(&import.module_ident, &short_name) {
                    type_symbol_id = sym_id;
                    _type_ident = short_name.clone();
                    debug!("resolve_method_definition: resolved '{}' to symbol_id {} via import '{}'", short_name, sym_id, import.module_ident);
                    break;
                }
            }
        }

        // Try in global scope
        if type_symbol_id == 0 {
            if let Some(sym_id) = symbol_table.find_symbol_id(&_type_ident, symbol_table.global_scope_id) {
                type_symbol_id = sym_id;
                debug!("resolve_method_definition: resolved '{}' to symbol_id {} via global scope", _type_ident, sym_id);
            }
        }
    }

    debug!("resolve_method_definition: type_symbol_id={}, type_ident='{}'", type_symbol_id, _type_ident);

    // 3. Look up the method on the type
    if type_symbol_id != 0 {
        // Check typedef's method_table first
        if let Some(type_symbol) = symbol_table.get_symbol_ref(type_symbol_id) {
            if let SymbolKind::Type(typedef_mutex) = &type_symbol.kind {
                let typedef = typedef_mutex.lock().unwrap();
                if let Some(method) = typedef.method_table.get(method_name) {
                    let fndef = method.lock().unwrap();
                    if fndef.symbol_start > 0 {
                        // Create a temporary symbol to find the location
                        drop(fndef);
                        drop(typedef);
                        // Search for the method as a global function: type_name.method_name
                        let type_short_name = crate::analyzer::completion::extract_last_ident_part_pub(&type_symbol.ident);
                        let method_ident = format!("{}.{}", type_short_name, method_name);

                        // Search through all modules for the method function
                        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
                        for (key, &sym_id) in &global_scope.symbol_map {
                            if key.ends_with(&method_ident) {
                                if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                                    if let SymbolKind::Fn(fn_mutex) = &sym.kind {
                                        let fndef = fn_mutex.lock().unwrap();
                                        if fndef.impl_type.symbol_id == type_symbol_id {
                                            drop(fndef);
                                            return symbol_to_location(sym);
                                        }
                                    }
                                }
                            }
                        }

                        // Also search module scope
                        let module_scope = symbol_table.find_scope(module.scope_id);
                        for (key, &sym_id) in &module_scope.symbol_map {
                            if key.ends_with(&method_ident) {
                                if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                                    if let SymbolKind::Fn(fn_mutex) = &sym.kind {
                                        let fndef = fn_mutex.lock().unwrap();
                                        if fndef.impl_type.symbol_id == type_symbol_id {
                                            drop(fndef);
                                            return symbol_to_location(sym);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fallback: Search for any global function with impl_type matching this type
        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
        for (_, &sym_id) in &global_scope.symbol_map {
            if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                if let SymbolKind::Fn(fn_mutex) = &sym.kind {
                    let fndef = fn_mutex.lock().unwrap();
                    if fndef.impl_type.symbol_id == type_symbol_id && fndef.fn_name == method_name {
                        drop(fndef);
                        return symbol_to_location(sym);
                    }
                }
            }
        }

        // Also check module scope
        let module_scope = symbol_table.find_scope(module.scope_id);
        for (_, &sym_id) in &module_scope.symbol_map {
            if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                if let SymbolKind::Fn(fn_mutex) = &sym.kind {
                    let fndef = fn_mutex.lock().unwrap();
                    if fndef.impl_type.symbol_id == type_symbol_id && fndef.fn_name == method_name {
                        drop(fndef);
                        return symbol_to_location(sym);
                    }
                }
            }
        }
    }

    // 4. If type_symbol_id is still 0 but we have a type_ident, try resolving by name
    //    (skip generic wrapper names like "ref" and "ptr")
    if !_type_ident.is_empty() && _type_ident != "ref" && _type_ident != "ptr" {
        let short_name = if let Some(dot_pos) = _type_ident.rfind('.') {
            &_type_ident[dot_pos + 1..]
        } else {
            &_type_ident
        };

        // Search for method functions named like: *.short_name.method_name
        let method_suffix = format!("{}.{}", short_name, method_name);
        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
        for (key, &sym_id) in &global_scope.symbol_map {
            if key.ends_with(&method_suffix) {
                if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                    return symbol_to_location(sym);
                }
            }
        }

        // Also try in imported module scopes
        for import in &module.dependencies {
            if let Some(&scope_id) = symbol_table.module_scopes.get(&import.module_ident) {
                let scope = symbol_table.find_scope(scope_id);
                for (key, &sym_id) in &scope.symbol_map {
                    if key.ends_with(&method_suffix) {
                        if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                            return symbol_to_location(sym);
                        }
                    }
                }
            }
        }
    }

    None
}

/// Get the (start, end) byte offsets of a symbol's definition.
fn get_symbol_span(symbol: &crate::analyzer::symbol::Symbol) -> (usize, usize) {
    use crate::analyzer::symbol::SymbolKind;

    match &symbol.kind {
        SymbolKind::Var(var_decl) => {
            let var = var_decl.lock().unwrap();
            (var.symbol_start, var.symbol_end)
        }
        SymbolKind::Fn(fndef_mutex) => {
            let fndef = fndef_mutex.lock().unwrap();
            (fndef.symbol_start, fndef.symbol_end)
        }
        SymbolKind::Type(typedef_mutex) => {
            let typedef = typedef_mutex.lock().unwrap();
            (typedef.symbol_start, typedef.symbol_end)
        }
        SymbolKind::Const(constdef_mutex) => {
            let constdef = constdef_mutex.lock().unwrap();
            (constdef.symbol_start, constdef.symbol_end)
        }
    }
}

/// Find the module that contains a given scope_id.
pub(crate) fn find_module_for_scope<'a>(
    symbol_table: &'a crate::analyzer::symbol::SymbolTable,
    scope_id: crate::analyzer::symbol::NodeId,
    module_db: &'a [crate::project::Module],
    current_module: &'a crate::project::Module,
) -> Option<&'a crate::project::Module> {
    let mut current = scope_id;
    while current > 0 {
        let scope = symbol_table.get_scope(current)?;

        if let crate::analyzer::symbol::ScopeKind::Module(ref module_ident) = scope.kind {
            for m in module_db {
                if m.ident == *module_ident {
                    return Some(m);
                }
            }
        }

        if let crate::analyzer::symbol::ScopeKind::Global = scope.kind {
            return Some(current_module);
        }

        current = scope.parent;
    }

    Some(current_module)
}

/// Resolve the target symbol id for a word at the given scope.
pub(crate) fn resolve_target_symbol_id(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module: &crate::project::Module,
    word: &str,
    current_scope_id: crate::analyzer::symbol::NodeId,
) -> Option<crate::analyzer::symbol::NodeId> {
    use crate::utils::format_global_ident;

    // 1. Local symbol lookup
    if let Some(symbol_id) = symbol_table.lookup_symbol(word, current_scope_id) {
        return Some(symbol_id);
    }

    // 2. Module-qualified global symbol
    let global_ident = format_global_ident(module.ident.clone(), word.to_string());
    if let Some(symbol) = symbol_table.find_global_symbol(&global_ident) {
        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
        if let Some(&id) = global_scope.symbol_map.get(&global_ident) {
            return Some(id);
        }
        match &symbol.kind {
            crate::analyzer::symbol::SymbolKind::Fn(f) => {
                let fndef = f.lock().ok()?;
                if fndef.symbol_id > 0 { return Some(fndef.symbol_id); }
            }
            crate::analyzer::symbol::SymbolKind::Type(t) => {
                let td = t.lock().ok()?;
                if td.symbol_id > 0 { return Some(td.symbol_id); }
            }
            crate::analyzer::symbol::SymbolKind::Var(v) => {
                let var = v.lock().ok()?;
                if var.symbol_id > 0 { return Some(var.symbol_id); }
            }
            crate::analyzer::symbol::SymbolKind::Const(c) => {
                let cd = c.lock().ok()?;
                if cd.symbol_id > 0 { return Some(cd.symbol_id); }
            }
        }
    }

    // 3. Selective imports
    for import in &module.dependencies {
        if import.is_selective {
            if let Some(ref items) = import.select_items {
                for item in items {
                    let effective_name = item.alias.as_deref().unwrap_or(&item.ident);
                    if effective_name == word {
                        let imported_global_ident = format_global_ident(import.module_ident.clone(), item.ident.clone());
                        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
                        if let Some(&id) = global_scope.symbol_map.get(&imported_global_ident) {
                            return Some(id);
                        }
                    }
                }
            }
        }
    }

    None
}

/// Collect all references to a target symbol in a given text/module.
pub(crate) fn collect_references_in_text(
    text: &str,
    word: &str,
    rope: &ropey::Rope,
    file_path: &str,
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module_scope_id: crate::analyzer::symbol::NodeId,
    target_symbol_id: crate::analyzer::symbol::NodeId,
    results: &mut Vec<Location>,
) {
    let uri = match Url::from_file_path(file_path) {
        Ok(u) => u,
        Err(_) => return,
    };

    let chars: Vec<char> = text.chars().collect();
    let word_len = word.len();

    let mut i = 0;
    while i + word_len <= chars.len() {
        if chars[i] == word.chars().next().unwrap_or('\0') {
            let candidate: String = chars[i..i + word_len].iter().collect();
            if candidate == word {
                let before_ok = i == 0 || !is_ident_char(chars[i - 1]);
                let after_ok = i + word_len >= chars.len() || !is_ident_char(chars[i + word_len]);

                if before_ok && after_ok {
                    let scope_id = find_innermost_scope(symbol_table, module_scope_id, i);
                    if let Some(found_id) = symbol_table.lookup_symbol(word, scope_id) {
                        if found_id == target_symbol_id {
                            if let (Some(start), Some(end)) = (
                                offset_to_position(i, rope),
                                offset_to_position(i + word_len, rope),
                            ) {
                                results.push(Location::new(uri.clone(), Range::new(start, end)));
                            }
                        }
                    }
                }
            }
        }
        i += 1;
    }
}
