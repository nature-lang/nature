//! Hover handler and symbol formatting for hover display.

use log::debug;
use tower_lsp::lsp_types::*;

use crate::utils::{extract_word_at_offset_rope, offset_to_position};

use super::navigation::{find_innermost_scope, find_module_for_scope};
use super::Backend;

impl Backend {
    pub(crate) async fn handle_hover(&self, params: HoverParams) -> Option<Hover> {
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

        // Use the live document rope for position→offset conversion so that
        // hover works correctly even while a build is debouncing.  Fall back
        // to the module rope (from the last build) if the live doc is missing.
        let live_doc = self.documents.get(file_path);
        let rope = match &live_doc {
            Some(r) => r.value(),
            None => &module.rope,
        };
        let line_char = rope.try_line_to_char(position.line as usize).ok()?;
        let char_offset = line_char + position.character as usize;

        let (word, word_start, word_end) = extract_word_at_offset_rope(rope, char_offset)?;

        debug!("Hover at offset {}, word: '{}'", char_offset, word);

        let symbol_table = project.symbol_table.lock().ok()?;
        let current_scope_id = find_innermost_scope(&symbol_table, module.scope_id, char_offset);

        // Check if this is a method/field access: receiver.method
        // Prioritize method hover so that e.g. `myApp.start()` shows the method
        // signature rather than an unrelated symbol named `start`.
        let hover_content = if word_start > 0 && rope.char(word_start - 1) == '.' && word_start >= 2 {
            if let Some((receiver, _, _)) = extract_word_at_offset_rope(rope, word_start - 2) {
                debug!("Hover: detected method call {}.{}", receiver, word);
                resolve_method_hover_content(
                    &symbol_table, module, &receiver, &word, current_scope_id, &module_db,
                ).or_else(|| resolve_hover_content(&symbol_table, module, &word, current_scope_id, &module_db))
            } else {
                resolve_hover_content(&symbol_table, module, &word, current_scope_id, &module_db)
            }
        } else {
            resolve_hover_content(&symbol_table, module, &word, current_scope_id, &module_db)
        };

        hover_content.map(|content| {
            let start_pos = offset_to_position(word_start, rope).unwrap_or(position);
            let end_pos = offset_to_position(word_end, rope).unwrap_or(position);

            Hover {
                contents: HoverContents::Markup(MarkupContent {
                    kind: MarkupKind::Markdown,
                    value: content,
                }),
                range: Some(Range::new(start_pos, end_pos)),
            }
        })
    }
}

// ─── Hover helpers ──────────────────────────────────────────────────────────────

/// Resolve hover content for a given word at a position.
fn resolve_hover_content(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module: &crate::project::Module,
    word: &str,
    current_scope_id: crate::analyzer::symbol::NodeId,
    module_db: &[crate::project::Module],
) -> Option<String> {
    use crate::utils::format_global_ident;

    let get_source_for_symbol = |symbol: &crate::analyzer::symbol::Symbol| -> String {
        if let Some(def_module) = find_module_for_scope(symbol_table, symbol.defined_in, module_db, module) {
            def_module.source.clone()
        } else {
            module.source.clone()
        }
    };

    // 1. Local symbol lookup
    if let Some(symbol_id) = symbol_table.lookup_symbol(word, current_scope_id) {
        if let Some(symbol) = symbol_table.get_symbol_ref(symbol_id) {
            let source = get_source_for_symbol(symbol);
            return Some(format_symbol_hover(symbol, &source));
        }
    }

    // 2. Module-qualified global symbol
    let global_ident = format_global_ident(module.ident.clone(), word.to_string());
    if let Some(symbol) = symbol_table.find_global_symbol(&global_ident) {
        let source = get_source_for_symbol(symbol);
        return Some(format_symbol_hover(symbol, &source));
    }

    // 3. Imported module symbols
    for import in &module.dependencies {
        if import.as_name == word {
            let mut content = String::new();
            content.push_str("```nature\n");
            if let Some(ref file) = import.file {
                content.push_str(&format!("import '{}'\n", file));
            } else if let Some(ref package) = import.ast_package {
                content.push_str(&format!("import {}\n", package.join(".")));
            }
            content.push_str("```\n");
            content.push_str(&format!("Module `{}`", word));
            return Some(content);
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
                            let source = get_source_for_symbol(symbol);
                            return Some(format_symbol_hover(symbol, &source));
                        }
                    }
                }
            }
        }
    }

    // 5. Builtin type keyword
    get_builtin_type_hover(word)
}

/// Resolve hover content for a method call like `receiver.method()`.
/// Looks up the receiver variable, determines its type (unwrapping ref/ptr),
/// and finds the method in the type's method_table or as a global impl function.
fn resolve_method_hover_content(
    symbol_table: &crate::analyzer::symbol::SymbolTable,
    module: &crate::project::Module,
    receiver_name: &str,
    method_name: &str,
    current_scope_id: crate::analyzer::symbol::NodeId,
    module_db: &[crate::project::Module],
) -> Option<String> {
    use crate::analyzer::common::TypeKind;
    use crate::analyzer::symbol::SymbolKind;

    debug!("resolve_method_hover: receiver='{}', method='{}'", receiver_name, method_name);

    let get_source_for_symbol = |symbol: &crate::analyzer::symbol::Symbol| -> String {
        if let Some(def_module) = find_module_for_scope(symbol_table, symbol.defined_in, module_db, module) {
            def_module.source.clone()
        } else {
            module.source.clone()
        }
    };

    // 1. Find the receiver variable
    let receiver_symbol_id = symbol_table.lookup_symbol(receiver_name, current_scope_id)?;
    let receiver_symbol = symbol_table.get_symbol_ref(receiver_symbol_id)?;

    // 2. Unwrap the receiver's type to find the underlying struct/type
    let (mut type_symbol_id, mut type_ident) = match &receiver_symbol.kind {
        SymbolKind::Var(var_decl) => {
            let var = var_decl.lock().unwrap();
            let type_ = &var.type_;

            let extract = |inner: &crate::analyzer::common::Type, outer: &crate::analyzer::common::Type| -> (usize, String) {
                if inner.symbol_id != 0 {
                    (inner.symbol_id, inner.ident.clone())
                } else if !inner.ident.is_empty() {
                    (0, inner.ident.clone())
                } else {
                    match &inner.kind {
                        TypeKind::Struct(struct_ident, _, _) if !struct_ident.is_empty() => {
                            (inner.symbol_id, struct_ident.clone())
                        }
                        _ => (outer.symbol_id, outer.ident.clone()),
                    }
                }
            };

            match &type_.kind {
                TypeKind::Ref(inner) | TypeKind::Ptr(inner) => extract(inner, type_),
                TypeKind::Ident if (type_.ident == "ref" || type_.ident == "ptr") && !type_.args.is_empty() => {
                    extract(&type_.args[0], type_)
                }
                _ => (type_.symbol_id, type_.ident.clone()),
            }
        }
        _ => return None,
    };

    // 2b. Fallback: resolve type_symbol_id by name
    if type_symbol_id == 0 && !type_ident.is_empty() && type_ident != "ref" && type_ident != "ptr" {
        let short_name = if let Some(dot_pos) = type_ident.rfind('.') {
            type_ident[dot_pos + 1..].to_string()
        } else {
            type_ident.clone()
        };

        if let Some(sym_id) = symbol_table.find_module_symbol_id(&module.ident, &short_name) {
            type_symbol_id = sym_id;
        }

        if type_symbol_id == 0 {
            for import in &module.dependencies {
                if let Some(sym_id) = symbol_table.find_module_symbol_id(&import.module_ident, &short_name) {
                    type_symbol_id = sym_id;
                    type_ident = short_name.clone();
                    break;
                }
            }
        }

        if type_symbol_id == 0 {
            if let Some(sym_id) = symbol_table.find_symbol_id(&type_ident, symbol_table.global_scope_id) {
                type_symbol_id = sym_id;
            }
        }
    }

    debug!("resolve_method_hover: type_symbol_id={}, type_ident='{}'", type_symbol_id, type_ident);

    // 3. Look up the method on the type
    if type_symbol_id != 0 {
        if let Some(type_symbol) = symbol_table.get_symbol_ref(type_symbol_id) {
            if let SymbolKind::Type(typedef_mutex) = &type_symbol.kind {
                let typedef = typedef_mutex.lock().unwrap();
                // Direct method_table lookup — gives us hover for the method
                if let Some(method) = typedef.method_table.get(method_name) {
                    let fndef = method.lock().unwrap();
                    let display_name = method_name;
                    let hover = format_fn_hover(&fndef, display_name);
                    drop(fndef);
                    drop(typedef);

                    // Try to find the impl function symbol for doc comments
                    let type_short_name = crate::analyzer::completion::extract_last_ident_part_pub(&type_symbol.ident);
                    let method_suffix = format!("{}.{}", type_short_name, method_name);
                    let mut doc_source = None;
                    let mut def_offset = 0usize;

                    // Search global scope for the method function
                    let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
                    for (key, &sym_id) in &global_scope.symbol_map {
                        if key.ends_with(&method_suffix) {
                            if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                                doc_source = Some(get_source_for_symbol(sym));
                                def_offset = sym.pos;
                                break;
                            }
                        }
                    }

                    // Also check module scope
                    if doc_source.is_none() {
                        let module_scope = symbol_table.find_scope(module.scope_id);
                        for (key, &sym_id) in &module_scope.symbol_map {
                            if key.ends_with(&method_suffix) {
                                if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                                    doc_source = Some(get_source_for_symbol(sym));
                                    def_offset = sym.pos;
                                    break;
                                }
                            }
                        }
                    }

                    let mut result = hover;
                    if let Some(source) = doc_source {
                        if def_offset > 0 {
                            if let Some(doc) = extract_doc_comment(&source, def_offset) {
                                result.push_str("\n\n---\n\n");
                                result.push_str(&doc);
                            }
                        }
                    }
                    return Some(result);
                }
            }
        }

        // Fallback: search for the method as a standalone impl function
        let search_scope = |scope: &crate::analyzer::symbol::Scope| -> Option<String> {
            for (_, &sym_id) in &scope.symbol_map {
                if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                    if let SymbolKind::Fn(fn_mutex) = &sym.kind {
                        let fndef = fn_mutex.lock().unwrap();
                        if fndef.impl_type.symbol_id == type_symbol_id && fndef.fn_name == method_name {
                            drop(fndef);
                            let source = get_source_for_symbol(sym);
                            return Some(format_symbol_hover(sym, &source));
                        }
                    }
                }
            }
            None
        };

        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
        if let Some(result) = search_scope(global_scope) {
            return Some(result);
        }

        let module_scope = symbol_table.find_scope(module.scope_id);
        if let Some(result) = search_scope(module_scope) {
            return Some(result);
        }
    }

    // 4. Name-based fallback (when type_symbol_id is 0 but we have an ident)
    if !type_ident.is_empty() && type_ident != "ref" && type_ident != "ptr" {
        let short_name = if let Some(dot_pos) = type_ident.rfind('.') {
            &type_ident[dot_pos + 1..]
        } else {
            &type_ident
        };

        let method_suffix = format!("{}.{}", short_name, method_name);
        let global_scope = symbol_table.find_scope(symbol_table.global_scope_id);
        for (key, &sym_id) in &global_scope.symbol_map {
            if key.ends_with(&method_suffix) {
                if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                    let source = get_source_for_symbol(sym);
                    return Some(format_symbol_hover(sym, &source));
                }
            }
        }

        for import in &module.dependencies {
            if let Some(&scope_id) = symbol_table.module_scopes.get(&import.module_ident) {
                let scope = symbol_table.find_scope(scope_id);
                for (key, &sym_id) in &scope.symbol_map {
                    if key.ends_with(&method_suffix) {
                        if let Some(sym) = symbol_table.get_symbol_ref(sym_id) {
                            let source = get_source_for_symbol(sym);
                            return Some(format_symbol_hover(sym, &source));
                        }
                    }
                }
            }
        }
    }

    None
}

/// Format hover content for a resolved symbol.
fn format_symbol_hover(symbol: &crate::analyzer::symbol::Symbol, source: &str) -> String {
    use crate::analyzer::symbol::SymbolKind;

    let display_name = if let Some(dot_pos) = symbol.ident.rfind('.') {
        &symbol.ident[dot_pos + 1..]
    } else {
        &symbol.ident
    };

    let mut content = match &symbol.kind {
        SymbolKind::Var(var_decl) => {
            let var = var_decl.lock().unwrap();
            let type_str = format_type_display(&var.type_);
            format!("```nature\nvar {}: {}\n```", display_name, type_str)
        }
        SymbolKind::Const(const_def) => {
            let c = const_def.lock().unwrap();
            let type_str = format_type_display(&c.type_);
            format!("```nature\nconst {}: {}\n```", display_name, type_str)
        }
        SymbolKind::Fn(fndef_mutex) => {
            let fndef = fndef_mutex.lock().unwrap();
            format_fn_hover(&fndef, display_name)
        }
        SymbolKind::Type(typedef_mutex) => {
            let typedef = typedef_mutex.lock().unwrap();
            format_typedef_hover(&typedef, display_name)
        }
    };

    // Append doc comment if available
    let def_offset = symbol.pos;
    if def_offset > 0 {
        if let Some(doc) = extract_doc_comment(source, def_offset) {
            content.push_str("\n\n---\n\n");
            content.push_str(&doc);
        }
    }

    content
}

/// Format a function definition for hover display.
fn format_fn_hover(fndef: &crate::analyzer::common::AstFnDef, display_name: &str) -> String {
    use crate::analyzer::common::SelfKind;

    let mut content = String::from("```nature\nfn ");

    if fndef.is_impl {
        let impl_name = if !fndef.impl_type.ident.is_empty() {
            if let Some(dot_pos) = fndef.impl_type.ident.rfind('.') {
                fndef.impl_type.ident[dot_pos + 1..].to_string()
            } else {
                fndef.impl_type.ident.clone()
            }
        } else {
            "?".to_string()
        };
        content.push_str(&format!("{}.", impl_name));
    }

    content.push_str(display_name);

    if let Some(ref generics_params) = fndef.generics_params {
        if !generics_params.is_empty() {
            let params_str: Vec<String> = generics_params.iter().map(|p| {
                if p.constraints.is_empty() {
                    p.ident.clone()
                } else {
                    let constraints: Vec<String> = p.constraints.iter().map(|c| format_type_display(c)).collect();
                    format!("{}: {}", p.ident, constraints.join(" + "))
                }
            }).collect();
            content.push_str(&format!("<{}>", params_str.join(", ")));
        }
    }

    content.push('(');

    let mut first = true;
    for param in &fndef.params {
        let param = param.lock().unwrap();
        if !first {
            content.push_str(", ");
        }
        first = false;

        if param.ident == "self" {
            match fndef.self_kind {
                SelfKind::SelfRefT => content.push_str("&self"),
                SelfKind::SelfPtrT => content.push_str("*self"),
                _ => content.push_str("self"),
            }
        } else {
            let type_str = format_type_display(&param.type_);
            content.push_str(&format!("{}: {}", param.ident, type_str));
        }
    }

    if fndef.rest_param {
        if !first {
            content.push_str(", ");
        }
        content.push_str("...");
    }

    content.push(')');

    let ret_type_str = format_type_display(&fndef.return_type);
    if ret_type_str != "void" {
        content.push_str(&format!(": {}", ret_type_str));
    }
    if fndef.is_errable {
        content.push('!');
    }

    content.push_str("\n```");
    content
}

/// Format a type definition for hover display.
fn format_typedef_hover(
    typedef: &crate::analyzer::common::TypedefStmt,
    display_name: &str,
) -> String {
    use crate::analyzer::common::{SelfKind, TypeKind};

    let mut content = String::from("```nature\ntype ");
    content.push_str(display_name);

    if !typedef.params.is_empty() {
        let params_str: Vec<String> = typedef.params.iter().map(|p| p.ident.clone()).collect();
        content.push_str(&format!("<{}>", params_str.join(", ")));
    }

    content.push_str(" = ");

    match &typedef.type_expr.kind {
        TypeKind::Struct(_, _, properties) => {
            content.push_str("struct {\n");
            for prop in properties {
                let type_str = format_type_display(&prop.type_);
                content.push_str(&format!("    {}: {}\n", prop.name, type_str));
            }
            content.push('}');
        }
        TypeKind::Interface(elements) => {
            content.push_str("interface {\n");
            for elem in elements {
                if let TypeKind::Fn(fn_type) = &elem.kind {
                    let ret_str = format_type_display(&fn_type.return_type);
                    let params_str: Vec<String> = fn_type.param_types.iter().map(|p| format_type_display(p)).collect();
                    content.push_str(&format!("    fn {}({}): {}\n", fn_type.name, params_str.join(", "), ret_str));
                }
            }
            content.push('}');
        }
        TypeKind::Enum(base_type, variants) => {
            content.push_str("enum {");
            content.push_str(&format!(" // base: {}\n", format_type_display(base_type)));
            for variant in variants {
                if let Some(ref val) = variant.value {
                    content.push_str(&format!("    {} = {}\n", variant.name, val));
                } else {
                    content.push_str(&format!("    {}\n", variant.name));
                }
            }
            content.push('}');
        }
        TypeKind::TaggedUnion(_, elements) => {
            content.push_str("union {\n");
            for elem in elements {
                let type_str = format_type_display(&elem.type_);
                content.push_str(&format!("    {} = {}\n", elem.tag, type_str));
            }
            content.push('}');
        }
        _ => {
            content.push_str(&format_type_display(&typedef.type_expr));
        }
    }

    content.push_str("\n```");

    // Show methods if any
    if !typedef.method_table.is_empty() {
        content.push_str("\n\n**Methods:**\n```nature\n");
        let mut methods: Vec<_> = typedef.method_table.iter().collect();
        methods.sort_by_key(|(name, _)| (*name).clone());
        for (name, method) in methods {
            let fndef = method.lock().unwrap();
            let mut sig = String::from("fn ");

            let mut first = true;
            let mut params_str = String::new();
            for param in &fndef.params {
                let param = param.lock().unwrap();
                if !first {
                    params_str.push_str(", ");
                }
                first = false;
                if param.ident == "self" {
                    match fndef.self_kind {
                        SelfKind::SelfRefT => params_str.push_str("&self"),
                        SelfKind::SelfPtrT => params_str.push_str("*self"),
                        _ => params_str.push_str("self"),
                    }
                } else {
                    params_str.push_str(&format!("{}: {}", param.ident, format_type_display(&param.type_)));
                }
            }

            sig.push_str(&format!("{}({})", name, params_str));
            let ret = format_type_display(&fndef.return_type);
            if ret != "void" {
                sig.push_str(&format!(": {}", ret));
            }
            if fndef.is_errable {
                sig.push('!');
            }
            content.push_str(&sig);
            content.push('\n');
        }
        content.push_str("```");
    }

    content
}

/// Format a Type for display in hover tooltips, inlay hints, and document symbols.
pub(crate) fn format_type_display(t: &crate::analyzer::common::Type) -> String {
    use crate::analyzer::common::{TypeIdentKind, TypeKind};

    if !t.ident.is_empty() && t.ident_kind != TypeIdentKind::Unknown {
        let base = if let Some(dot_pos) = t.ident.rfind('.') {
            t.ident[dot_pos + 1..].to_string()
        } else {
            t.ident.clone()
        };

        if !t.args.is_empty() {
            let args_str: Vec<String> = t.args.iter().map(|a| format_type_display(a)).collect();
            return format!("{}<{}>", base, args_str.join(", "));
        }
        return base;
    }

    match &t.kind {
        TypeKind::Void => "void".to_string(),
        TypeKind::Bool => "bool".to_string(),
        TypeKind::Int => "int".to_string(),
        TypeKind::Uint => "uint".to_string(),
        TypeKind::Float => "float".to_string(),
        TypeKind::Int8 => "i8".to_string(),
        TypeKind::Uint8 => "u8".to_string(),
        TypeKind::Int16 => "i16".to_string(),
        TypeKind::Uint16 => "u16".to_string(),
        TypeKind::Int32 => "i32".to_string(),
        TypeKind::Uint32 => "u32".to_string(),
        TypeKind::Int64 => "i64".to_string(),
        TypeKind::Uint64 => "u64".to_string(),
        TypeKind::Float32 => "f32".to_string(),
        TypeKind::Float64 => "f64".to_string(),
        TypeKind::String => "string".to_string(),
        TypeKind::Null => "null".to_string(),
        TypeKind::Anyptr => "anyptr".to_string(),
        TypeKind::CoroutineT => "coroutine_t".to_string(),
        TypeKind::Vec(elem) => format!("[{}]", format_type_display(elem)),
        TypeKind::Arr(_, len, elem) => format!("[{};{}]", format_type_display(elem), len),
        TypeKind::Map(key, val) => format!("map<{}, {}>", format_type_display(key), format_type_display(val)),
        TypeKind::Set(elem) => format!("set<{}>", format_type_display(elem)),
        TypeKind::Chan(elem) => format!("chan<{}>", format_type_display(elem)),
        TypeKind::Tuple(elems, _) => {
            let parts: Vec<String> = elems.iter().map(|e| format_type_display(e)).collect();
            format!("({})", parts.join(", "))
        }
        TypeKind::Fn(fn_type) => {
            let params: Vec<String> = fn_type.param_types.iter().map(|p| format_type_display(p)).collect();
            let ret = format_type_display(&fn_type.return_type);
            let err = if fn_type.errable { "!" } else { "" };
            format!("fn({}): {}{}", params.join(", "), ret, err)
        }
        TypeKind::Ref(inner) => format!("ref<{}>", format_type_display(inner)),
        TypeKind::Ptr(inner) => format!("ptr<{}>", format_type_display(inner)),
        TypeKind::Struct(ident, _, _) if !ident.is_empty() => {
            if let Some(dot_pos) = ident.rfind('.') {
                ident[dot_pos + 1..].to_string()
            } else {
                ident.clone()
            }
        }
        TypeKind::Struct(_, _, props) => {
            let parts: Vec<String> = props.iter().map(|p| format!("{}: {}", p.name, format_type_display(&p.type_))).collect();
            format!("struct {{{}}}", parts.join(", "))
        }
        TypeKind::Union(any, nullable, elems) => {
            if *any { return "any".to_string(); }
            let parts: Vec<String> = elems.iter().map(|e| format_type_display(e)).collect();
            let mut result = parts.join("|");
            if *nullable { result.push_str("|null"); }
            result
        }
        TypeKind::TaggedUnion(ident, _) if !ident.is_empty() => ident.clone(),
        TypeKind::Enum(_, _) => "enum".to_string(),
        TypeKind::Interface(_) => "interface".to_string(),
        TypeKind::Ident => {
            if !t.ident.is_empty() { t.ident.clone() } else { "unknown".to_string() }
        }
        _ => t.kind.to_string(),
    }
}

/// Return hover content for builtin type keywords.
fn get_builtin_type_hover(word: &str) -> Option<String> {
    let desc = match word {
        "bool" => "Boolean type (`true` or `false`)",
        "int" => "Platform-native signed integer (i64)",
        "uint" => "Platform-native unsigned integer (u64)",
        "float" => "Platform-native float (f64)",
        "i8" => "8-bit signed integer",
        "i16" => "16-bit signed integer",
        "i32" => "32-bit signed integer",
        "i64" => "64-bit signed integer",
        "u8" => "8-bit unsigned integer",
        "u16" => "16-bit unsigned integer",
        "u32" => "32-bit unsigned integer",
        "u64" => "64-bit unsigned integer",
        "f32" => "32-bit floating point",
        "f64" => "64-bit floating point",
        "string" => "UTF-8 string type",
        "void" => "Void type (no value)",
        "any" => "Any type (union of all types)",
        "anyptr" => "Untyped raw pointer",
        _ => return None,
    };
    Some(format!("```nature\ntype {}\n```\n{}", word, desc))
}

/// Extract doc comments (`//` lines) immediately above a definition.
pub(crate) fn extract_doc_comment(source: &str, def_offset: usize) -> Option<String> {
    if def_offset == 0 || def_offset > source.len() {
        return None;
    }

    let lines: Vec<&str> = source.lines().collect();
    if lines.is_empty() {
        return None;
    }

    let mut char_count = 0;
    let mut def_line = 0;
    for (i, line) in lines.iter().enumerate() {
        let line_end = char_count + line.len() + 1;
        if def_offset < line_end {
            def_line = i;
            break;
        }
        char_count = line_end;
        if i == lines.len() - 1 {
            def_line = i;
        }
    }

    if def_line == 0 {
        return None;
    }

    let mut comment_lines: Vec<String> = Vec::new();
    let mut i = def_line - 1;
    loop {
        let trimmed = lines[i].trim();
        if trimmed.starts_with("//") {
            let text = trimmed.strip_prefix("//").unwrap_or("");
            let text = text.strip_prefix(' ').unwrap_or(text);
            comment_lines.push(text.to_string());
        } else if trimmed.is_empty() {
            break;
        } else {
            break;
        }

        if i == 0 {
            break;
        }
        i -= 1;
    }

    if comment_lines.is_empty() {
        return None;
    }

    comment_lines.reverse();
    Some(comment_lines.join("\n"))
}
