//! Navigation request handlers: go-to-definition, find-all-references,
//! go-to-type-definition, and go-to-implementation.

use tower_lsp::lsp_types::*;

use crate::analyzer::common::Type;
use crate::analyzer::symbol::{NodeId, Scope, ScopeKind, Symbol, SymbolKind, SymbolTable};
use crate::project::{Module, Project};
use crate::utils::{
    extract_word_at_offset_rope, format_global_ident, is_ident_char, offset_to_position,
    position_to_char_offset,
};

use super::Backend;

// ─── Handler wiring ─────────────────────────────────────────────────────────────

impl Backend {
    pub(crate) async fn handle_goto_definition(
        &self,
        params: GotoDefinitionParams,
    ) -> Option<GotoDefinitionResponse> {
        let uri = &params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;
        let location = find_definition(&project, file_path, position)?;

        Some(GotoDefinitionResponse::Scalar(location))
    }

    pub(crate) async fn handle_references(
        &self,
        params: ReferenceParams,
    ) -> Option<Vec<Location>> {
        let uri = &params.text_document_position.text_document.uri;
        let position = params.text_document_position.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;

        find_references(&project, file_path, position, params.context.include_declaration)
    }

    pub(crate) async fn handle_goto_type_definition(
        &self,
        params: GotoDefinitionParams,
    ) -> Option<GotoDefinitionResponse> {
        let uri = &params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;
        let location = find_type_definition(&project, file_path, position)?;

        Some(GotoDefinitionResponse::Scalar(location))
    }

    pub(crate) async fn handle_goto_implementation(
        &self,
        params: GotoDefinitionParams,
    ) -> Option<GotoDefinitionResponse> {
        let uri = &params.text_document_position_params.text_document.uri;
        let position = params.text_document_position_params.position;
        let file_path = uri.path();

        let project = self.get_file_project(file_path)?;
        let locations = find_implementations(&project, file_path, position)?;

        if locations.len() == 1 {
            Some(GotoDefinitionResponse::Scalar(locations.into_iter().next().unwrap()))
        } else {
            Some(GotoDefinitionResponse::Array(locations))
        }
    }
}

// ─── Go to definition ───────────────────────────────────────────────────────────

/// Resolve the definition location for the symbol under the cursor.
fn find_definition(project: &Project, file_path: &str, position: Position) -> Option<Location> {
    let cursor = resolve_cursor(project, file_path, position)?;
    let symbol = resolve_symbol(project, &cursor)?;

    symbol_to_location(project, &symbol)
}

// ─── Find all references ────────────────────────────────────────────────────────

/// Collect every location that references the same symbol as the one under the cursor.
fn find_references(
    project: &Project,
    file_path: &str,
    position: Position,
    include_declaration: bool,
) -> Option<Vec<Location>> {
    let cursor = resolve_cursor(project, file_path, position)?;
    let target = resolve_symbol(project, &cursor)?;
    let def_offset = symbol_def_range(&target.kind);

    let db = project.module_db.lock().ok()?;
    let st = project.symbol_table.lock().ok()?;

    let mut locations: Vec<Location> = Vec::new();

    for module in db.iter() {
        let uri = match Url::from_file_path(&module.path) {
            Ok(u) => u,
            Err(_) => continue,
        };

        collect_references_in_module(
            &st,
            module,
            &target,
            def_offset,
            include_declaration,
            &uri,
            &mut locations,
        );
    }

    if locations.is_empty() {
        return None;
    }

    Some(locations)
}

/// Scan a single module's `token_db` for tokens that resolve to the same symbol.
fn collect_references_in_module(
    st: &SymbolTable,
    module: &Module,
    target: &ResolvedSymbol,
    def_offset: (usize, usize),
    include_declaration: bool,
    uri: &Url,
    locations: &mut Vec<Location>,
) {
    let raw_name = target.raw_name();

    for token in &module.token_db {
        if token.literal != raw_name {
            continue;
        }

        // Skip the declaration site unless requested.
        if !include_declaration && module.ident == target.module_ident {
            if token.start == def_offset.0 {
                continue;
            }
        }

        let scope_id = find_scope_at_offset(st, module.scope_id, token.start);
        if !token_resolves_to_same_symbol(st, &token.literal, scope_id, module, target) {
            continue;
        }

        let Some(start) = offset_to_position(token.start, &module.rope) else {
            continue;
        };
        let Some(end) = offset_to_position(token.end, &module.rope) else {
            continue;
        };

        locations.push(Location {
            uri: uri.clone(),
            range: Range { start, end },
        });
    }
}

/// Check whether `ident` in `scope_id` resolves to the same symbol as `target`.
fn token_resolves_to_same_symbol(
    st: &SymbolTable,
    ident: &str,
    scope_id: NodeId,
    module: &Module,
    target: &ResolvedSymbol,
) -> bool {
    // Local/scoped lookup.
    if let Some(id) = st.lookup_symbol(ident, scope_id) {
        return id == target.symbol_id;
    }

    // Module-qualified lookup.
    if let Some(id) = st.find_module_symbol_id(&module.ident, ident) {
        return id == target.symbol_id;
    }

    // Selective imports.
    for import in &module.dependencies {
        if !import.is_selective {
            continue;
        }
        let Some(ref items) = import.select_items else {
            continue;
        };
        for item in items {
            let local_name = item.alias.as_deref().unwrap_or(&item.ident);
            if local_name != ident {
                continue;
            }
            let global = format_global_ident(import.module_ident.clone(), item.ident.clone());
            if let Some(id) = st.find_symbol_id(&global, st.global_scope_id) {
                return id == target.symbol_id;
            }
        }
    }

    // Wildcard imports.
    for import in &module.dependencies {
        if import.as_name != "*" {
            continue;
        }
        if let Some(id) = st.find_module_symbol_id(&import.module_ident, ident) {
            return id == target.symbol_id;
        }
    }

    false
}

// ─── Go to type definition ──────────────────────────────────────────────────────

/// Navigate from a variable/constant to the definition of its type.
///
/// For example, if the cursor is on `x` where `var x: MyStruct = ...`, this
/// navigates to the `type MyStruct = struct { ... }` definition.
fn find_type_definition(
    project: &Project,
    file_path: &str,
    position: Position,
) -> Option<Location> {
    let cursor = resolve_cursor(project, file_path, position)?;
    let symbol = resolve_symbol(project, &cursor)?;

    // Extract the type from the symbol.
    let type_ = extract_symbol_type(&symbol.kind)?;

    // Look up the type's definition via its symbol_id.
    type_to_location(project, &type_)
}

/// Extract the `Type` associated with a symbol (variable's type, function's
/// return type, constant's type).
fn extract_symbol_type(kind: &SymbolKind) -> Option<Type> {
    match kind {
        SymbolKind::Var(v) => {
            let v = v.lock().unwrap();
            Some(v.type_.clone())
        }
        SymbolKind::Const(c) => {
            let c = c.lock().unwrap();
            Some(c.type_.clone())
        }
        SymbolKind::Fn(f) => {
            let f = f.lock().unwrap();
            Some(f.return_type.clone())
        }
        // If the cursor is already on a type, go to it directly.
        SymbolKind::Type(t) => {
            let t = t.lock().unwrap();
            // For aliases, navigate to the aliased type.
            if t.is_alias {
                Some(t.type_expr.clone())
            } else {
                None // already at the type itself
            }
        }
    }
}

/// Convert a `Type` to a `Location` by looking up its `symbol_id` in the
/// symbol table to find the `TypedefStmt` definition site.
fn type_to_location(project: &Project, type_: &Type) -> Option<Location> {
    // Only user-defined types have navigable definitions.
    if type_.symbol_id == 0 {
        return None;
    }

    let st = project.symbol_table.lock().ok()?;
    let symbol = st.get_symbol_ref(type_.symbol_id)?;

    // Must be a Type symbol.
    let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
        return None;
    };

    let typedef = typedef_mutex.lock().unwrap();
    let def_start = typedef.symbol_start;
    let def_end = typedef.symbol_end;
    drop(typedef);

    let module_ident = module_ident_for_scope(&st, symbol.defined_in)?;
    drop(st);

    let db = project.module_db.lock().ok()?;
    let module = db.iter().find(|m| m.ident == module_ident)?;

    let start = offset_to_position(def_start, &module.rope)?;
    let end = offset_to_position(def_end, &module.rope)?;
    let uri = Url::from_file_path(&module.path).ok()?;

    Some(Location {
        uri,
        range: Range { start, end },
    })
}

// ─── Go to implementation ───────────────────────────────────────────────────────

/// Find all types that implement a given interface.
///
/// When the cursor is on an interface type, this collects all `type X = struct { ... }`
/// definitions that have `impl InterfaceName` in their `impl_interfaces` list.
fn find_implementations(
    project: &Project,
    file_path: &str,
    position: Position,
) -> Option<Vec<Location>> {
    let cursor = resolve_cursor(project, file_path, position)?;
    let symbol = resolve_symbol(project, &cursor)?;

    // Must be a type symbol, ideally an interface.
    let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
        return None;
    };

    let typedef = typedef_mutex.lock().unwrap();
    let target_ident = typedef.ident.clone();
    let is_interface = typedef.is_interface;
    drop(typedef);

    if !is_interface {
        return None;
    }

    let db = project.module_db.lock().ok()?;
    let st = project.symbol_table.lock().ok()?;
    let mut locations: Vec<Location> = Vec::new();

    // Scan all symbols for types that implement this interface.
    for module in db.iter() {
        let Some(scope) = st.get_scope(module.scope_id) else {
            continue;
        };

        for &sym_id in &scope.symbols {
            let Some(sym) = st.get_symbol_ref(sym_id) else {
                continue;
            };

            let SymbolKind::Type(td_mutex) = &sym.kind else {
                continue;
            };

            let td = td_mutex.lock().unwrap();

            // Check if this type implements the target interface.
            let implements = td.impl_interfaces.iter().any(|iface| {
                iface.ident == target_ident || iface.symbol_id == symbol.symbol_id
            });

            if !implements {
                continue;
            }

            let Some(start) = offset_to_position(td.symbol_start, &module.rope) else {
                continue;
            };
            let Some(end) = offset_to_position(td.symbol_end, &module.rope) else {
                continue;
            };
            let Ok(uri) = Url::from_file_path(&module.path) else {
                continue;
            };

            locations.push(Location {
                uri,
                range: Range { start, end },
            });
        }
    }

    if locations.is_empty() {
        return None;
    }

    Some(locations)
}

// ─── Cursor resolution ──────────────────────────────────────────────────────────

/// Intermediate: identifies the word and scope under the cursor.
pub(crate) struct CursorContext {
    pub(crate) word: String,
    /// If the cursor is on the right side of a dot (e.g. `bootstrap.app`),
    /// this holds the left-side identifier (`"bootstrap"`).
    pub(crate) prefix: Option<String>,
    pub(crate) char_offset: usize,
    pub(crate) module_ident: String,
    pub(crate) module_path: String,
    pub(crate) scope_id: NodeId,
    pub(crate) dependencies: Vec<crate::analyzer::common::ImportStmt>,
}

/// Convert an LSP position into a `CursorContext` (word + scope).
pub(crate) fn resolve_cursor(
    project: &Project,
    file_path: &str,
    position: Position,
) -> Option<CursorContext> {
    let mh = project.module_handled.lock().ok()?;
    let &idx = mh.get(file_path)?;
    drop(mh);

    let db = project.module_db.lock().ok()?;
    let module = db.get(idx)?;

    let char_offset = position_to_char_offset(position, &module.rope)?;
    let (word, word_start, _) = extract_word_at_offset_rope(&module.rope, char_offset)?;

    // Check for a dot-prefix: if there's a `.` immediately before the word,
    // extract the identifier to the left of the dot.
    let prefix = if word_start >= 2 && module.rope.char(word_start - 1) == '.' {
        // Walk left from the dot to find the prefix identifier.
        let dot_pos = word_start - 1;
        let mut prefix_start = dot_pos;
        while prefix_start > 0 && is_ident_char(module.rope.char(prefix_start - 1)) {
            prefix_start -= 1;
        }
        if prefix_start < dot_pos {
            let prefix_word: String = module.rope.slice(prefix_start..dot_pos).chars().collect();
            Some(prefix_word)
        } else {
            None
        }
    } else {
        None
    };

    let st = project.symbol_table.lock().ok()?;
    let scope_id = find_scope_at_offset(&st, module.scope_id, char_offset);

    Some(CursorContext {
        word,
        prefix,
        char_offset,
        module_ident: module.ident.clone(),
        module_path: module.path.clone(),
        scope_id,
        dependencies: module.dependencies.clone(),
    })
}

// ─── Symbol resolution ──────────────────────────────────────────────────────────

/// A fully resolved symbol: its id, kind, owning module ident, and position.
pub(crate) struct ResolvedSymbol {
    pub(crate) symbol_id: NodeId,
    pub(crate) kind: SymbolKind,
    /// The global ident (may include module prefix).
    pub(crate) ident: String,
    /// Module ident of the module that defines this symbol.
    /// Stored instead of module_path to avoid locking module_db while
    /// symbol_table is held (which would invert the canonical lock order).
    pub(crate) module_ident: String,
}

impl ResolvedSymbol {
    /// The raw (unprefixed) name for token matching.
    pub(crate) fn raw_name(&self) -> &str {
        self.ident.rsplit('.').next().unwrap_or(&self.ident)
    }

    /// Lazily resolve the module file path from `module_ident`.
    /// Locks `module_db` — call only when `symbol_table` is NOT held.
    pub(crate) fn module_path(&self, project: &Project) -> Option<String> {
        let db = project.module_db.lock().ok()?;
        db.iter()
            .find(|m| m.ident == self.module_ident)
            .map(|m| m.path.clone())
    }
}

/// Resolve a cursor context into a `ResolvedSymbol`.
///
/// Follows the same resolution order as the semantic pass:
///   local scopes → same-module global → selective imports → wildcard imports → builtins.
pub(crate) fn resolve_symbol(project: &Project, ctx: &CursorContext) -> Option<ResolvedSymbol> {
    let st = project.symbol_table.lock().ok()?;

    // 1. Local / parent-scope walk.
    if let Some(id) = st.lookup_symbol(&ctx.word, ctx.scope_id) {
        return resolved_from_id(&st, id, &ctx.word);
    }

    // 2. Same-module global (symbols are stored as "module_ident.name").
    if let Some(id) = st.find_module_symbol_id(&ctx.module_ident, &ctx.word) {
        let global = format_global_ident(ctx.module_ident.clone(), ctx.word.clone());
        return resolved_from_id(&st, id, &global);
    }

    // 3. Selective imports.
    for import in &ctx.dependencies {
        if !import.is_selective {
            continue;
        }
        let Some(ref items) = import.select_items else {
            continue;
        };
        for item in items {
            let local_name = item.alias.as_deref().unwrap_or(&item.ident);
            if local_name != ctx.word {
                continue;
            }
            let global = format_global_ident(import.module_ident.clone(), item.ident.clone());
            if let Some(id) = st.find_symbol_id(&global, st.global_scope_id) {
                return resolved_from_id(&st, id, &global);
            }
        }
    }

    // 4. Wildcard imports.
    for import in &ctx.dependencies {
        if import.as_name != "*" {
            continue;
        }
        if let Some(id) = st.find_module_symbol_id(&import.module_ident, &ctx.word) {
            let global = format_global_ident(import.module_ident.clone(), ctx.word.clone());
            return resolved_from_id(&st, id, &global);
        }
    }

    // 5. Builtins (global scope, no prefix).
    if let Some(id) = st.find_symbol_id(&ctx.word, st.global_scope_id) {
        return resolved_from_id(&st, id, &ctx.word);
    }

    // 6. Dotted access: prefix.word (e.g. bootstrap.app or myVar.field).
    if let Some(ref prefix) = ctx.prefix {
        log::debug!("resolve_symbol step 6: prefix={:?}, word={}", prefix, ctx.word);

        // 6a. Module member access: prefix is an import as_name.
        for import in &ctx.dependencies {
            if import.as_name == *prefix {
                let global = format_global_ident(import.module_ident.clone(), ctx.word.clone());
                log::debug!("step 6a: trying import global={}", global);
                if let Some(id) = st.find_symbol_id(&global, st.global_scope_id) {
                    return resolved_from_id(&st, id, &global);
                }
            }
        }

        // 6b. Method/field access: prefix is a variable/const — resolve its type.
        if let Some(prefix_symbol) = resolve_prefix_symbol(&st, ctx, prefix) {
            log::debug!("step 6b: prefix_symbol kind={:?}", std::mem::discriminant(&prefix_symbol.kind));
            let prefix_type = extract_type_from_kind(&prefix_symbol.kind);
            if let Some(ref prefix_type) = prefix_type {
                log::debug!("step 6b: prefix_type kind={}, ident={}, symbol_id={}", prefix_type.kind, prefix_type.ident, prefix_type.symbol_id);
                // Look up the typedef to find methods or struct fields.
                if let Some(result) = resolve_member_on_type(&st, &prefix_type, &ctx.word) {
                    return Some(result);
                }
                log::debug!("step 6b: resolve_member_on_type returned None");
            } else {
                log::debug!("step 6b: prefix_type is None");
            }
        } else {
            log::debug!("step 6b: resolve_prefix_symbol returned None for prefix={}", prefix);
        }
    }

    None
}

/// Build a `ResolvedSymbol` from a symbol id.
///
/// Only uses `st` (no `module_db` lock), so it is safe to call while
/// `symbol_table` is held.
fn resolved_from_id(
    st: &SymbolTable,
    symbol_id: NodeId,
    ident: &str,
) -> Option<ResolvedSymbol> {
    let symbol = st.get_symbol_ref(symbol_id)?;
    let module_ident = module_ident_for_scope(st, symbol.defined_in)
        .unwrap_or_default();

    Some(ResolvedSymbol {
        symbol_id,
        kind: symbol.kind.clone(),
        ident: ident.to_string(),
        module_ident,
    })
}

// ─── Dotted access helpers ──────────────────────────────────────────────────────

/// Resolve the prefix identifier (left of the dot) to a symbol, using the same
/// lookup chain as `resolve_symbol` but for a single word.
fn resolve_prefix_symbol<'a>(
    st: &'a SymbolTable,
    ctx: &CursorContext,
    prefix: &str,
) -> Option<Symbol> {
    // Local scope walk.
    if let Some(id) = st.lookup_symbol(prefix, ctx.scope_id) {
        return st.get_symbol_ref(id).cloned();
    }
    // Same-module global.
    if let Some(id) = st.find_module_symbol_id(&ctx.module_ident, prefix) {
        return st.get_symbol_ref(id).cloned();
    }
    // Selective imports.
    for import in &ctx.dependencies {
        if !import.is_selective {
            continue;
        }
        if let Some(ref items) = import.select_items {
            for item in items {
                let local_name = item.alias.as_deref().unwrap_or(&item.ident);
                if local_name == prefix {
                    let global = format_global_ident(import.module_ident.clone(), item.ident.clone());
                    if let Some(id) = st.find_symbol_id(&global, st.global_scope_id) {
                        return st.get_symbol_ref(id).cloned();
                    }
                }
            }
        }
    }
    // Wildcard imports.
    for import in &ctx.dependencies {
        if import.as_name != "*" {
            continue;
        }
        if let Some(id) = st.find_module_symbol_id(&import.module_ident, prefix) {
            return st.get_symbol_ref(id).cloned();
        }
    }
    // Builtins.
    if let Some(sym) = st.find_global_symbol(prefix) {
        return Some(sym.clone());
    }
    None
}

/// Extract the type from a symbol kind (Var → type_, Const → type_, Fn → return_type).
fn extract_type_from_kind(kind: &SymbolKind) -> Option<Type> {
    match kind {
        SymbolKind::Var(v) => Some(v.lock().unwrap().type_.clone()),
        SymbolKind::Const(c) => Some(c.lock().unwrap().type_.clone()),
        SymbolKind::Fn(f) => Some(f.lock().unwrap().return_type.clone()),
        SymbolKind::Type(_) => None,
    }
}

/// Resolve a member (method or struct field) on a given type.
fn resolve_member_on_type(
    st: &SymbolTable,
    owner_type: &Type,
    member: &str,
) -> Option<ResolvedSymbol> {
    // Dereference pointer/ref types to get the underlying type.
    let base_type = match &owner_type.kind {
        crate::analyzer::common::TypeKind::Ptr(inner)
        | crate::analyzer::common::TypeKind::Ref(inner) => inner.as_ref(),
        // Handle unreduced ref<T>/ptr<T> stored as TypeKind::Ident with args.
        crate::analyzer::common::TypeKind::Ident
            if (owner_type.ident == "ref" || owner_type.ident == "ptr")
                && !owner_type.args.is_empty() =>
        {
            &owner_type.args[0]
        }
        _ => owner_type,
    };

    // Find the typedef by symbol_id or ident.
    log::debug!("resolve_member_on_type: member={}, base_type.kind={}, base_type.ident={}, base_type.symbol_id={}", member, base_type.kind, base_type.ident, base_type.symbol_id);
    let (typedef_symbol, typedef_symbol_id) = if base_type.symbol_id > 0 {
        log::debug!("resolve_member_on_type: looking up by symbol_id={}", base_type.symbol_id);
        (st.get_symbol_ref(base_type.symbol_id), base_type.symbol_id)
    } else if !base_type.ident.is_empty() {
        // Try direct lookup (module-qualified ident).
        let sym = st.find_global_symbol(&base_type.ident);
        log::debug!("resolve_member_on_type: find_global_symbol({}) = {}", base_type.ident, sym.is_some());
        if sym.is_some() {
            // Look up the symbol_id from the global scope
            let scope = st.get_scope(st.global_scope_id);
            let sid = scope.and_then(|s| s.symbol_map.get(&base_type.ident).copied()).unwrap_or(0);
            (sym, sid)
        } else {
            // Fallback: search all global symbols for a matching suffix.
            let scope = st.get_scope(st.global_scope_id)?;
            let found = scope.symbol_map.iter()
                .find(|(k, _)| {
                    k.rsplit('.').next() == Some(&base_type.ident)
                });
            if let Some((_, &id)) = found {
                (st.get_symbol_ref(id), id)
            } else {
                log::debug!("resolve_member_on_type: suffix fallback = false");
                (None, 0)
            }
        }
    } else {
        // Type has no ident and no symbol_id — check for inline struct.
        if let crate::analyzer::common::TypeKind::Struct(_, _, ref props) = base_type.kind {
            for prop in props {
                if prop.name == member {
                    let var = crate::analyzer::common::VarDeclExpr {
                        ident: prop.name.clone(),
                        symbol_id: 0,
                        symbol_start: prop.start,
                        symbol_end: prop.end,
                        type_: prop.type_.clone(),
                        be_capture: false,
                        heap_ident: None,
                        is_private: false,
                    };
                    return Some(ResolvedSymbol {
                        symbol_id: 0,
                        kind: SymbolKind::Var(std::sync::Arc::new(std::sync::Mutex::new(var))),
                        ident: prop.name.clone(),
                        module_ident: String::new(),
                    });
                }
            }
        }
        (None, 0)
    };

    if let Some(sym) = typedef_symbol {
        if let SymbolKind::Type(typedef_mutex) = &sym.kind {
            let typedef = typedef_mutex.lock().unwrap();
            let owner_module_ident = module_ident_for_scope(st, sym.defined_in)
                .unwrap_or_default();

            // Check method_table: try exact key first, then suffix match.
            // Keys may be fully-qualified (e.g. "module.Type.method") while
            // `member` is just the short name ("method").
            let method_fndef = typedef.method_table.get(member).cloned().or_else(|| {
                typedef.method_table.iter()
                    .find(|(k, _)| k.rsplit('.').next() == Some(member))
                    .map(|(_, v)| v.clone())
            });
            if let Some(fndef) = method_fndef {
                return Some(ResolvedSymbol {
                    symbol_id: 0, // method is in method_table, not symbol table
                    kind: SymbolKind::Fn(fndef),
                    ident: member.to_string(),
                    module_ident: owner_module_ident,
                });
            }

            // Check struct fields.
            if let crate::analyzer::common::TypeKind::Struct(_, _, ref props) = typedef.type_expr.kind {
                for prop in props {
                    if prop.name == member {
                        // Represent struct field as a Var for hover display.
                        let var = crate::analyzer::common::VarDeclExpr {
                            ident: prop.name.clone(),
                            symbol_id: 0,
                            symbol_start: prop.start,
                            symbol_end: prop.end,
                            type_: prop.type_.clone(),
                            be_capture: false,
                            heap_ident: None,
                            is_private: false,
                        };
                        return Some(ResolvedSymbol {
                            symbol_id: 0,
                            kind: SymbolKind::Var(std::sync::Arc::new(std::sync::Mutex::new(var))),
                            ident: prop.name.clone(),
                            module_ident: owner_module_ident,
                        });
                    }
                }
            }
        }
    }

    // Fallback: scan global scope for impl method symbols.
    // Method functions are registered as separate symbols with
    // `fndef.impl_type.symbol_id` matching the typedef. Cross-module
    // re-resolution may produce a different symbol_id, so also match
    // by ident (full or short name).
    let type_ident = &base_type.ident;
    let type_short_name = type_ident.rsplit('.').next().unwrap_or(type_ident);

    if let Some(scope) = st.get_scope(st.global_scope_id) {
        for (_, &sym_id) in &scope.symbol_map {
            if let Some(sym) = st.get_symbol_ref(sym_id) {
                if let SymbolKind::Fn(fndef_mutex) = &sym.kind {
                    let fndef = fndef_mutex.lock().unwrap();
                    if fndef.fn_name != member {
                        continue;
                    }
                    // Match by symbol_id or by ident (full or suffix).
                    let id_match = typedef_symbol_id > 0
                        && fndef.impl_type.symbol_id == typedef_symbol_id;
                    let ident_match = !fndef.impl_type.ident.is_empty()
                        && (fndef.impl_type.ident == *type_ident
                            || fndef.impl_type.ident.rsplit('.').next() == Some(type_short_name));
                    if id_match || ident_match {
                        let owner_module_ident = module_ident_for_scope(st, sym.defined_in)
                            .unwrap_or_default();
                        return Some(ResolvedSymbol {
                            symbol_id: sym_id,
                            kind: SymbolKind::Fn(fndef_mutex.clone()),
                            ident: member.to_string(),
                            module_ident: owner_module_ident,
                        });
                    }
                }
            }
        }
    }

    None
}

// ─── Scope helpers ──────────────────────────────────────────────────────────────

/// Find the deepest scope that contains `offset`.
fn find_scope_at_offset(st: &SymbolTable, root: NodeId, offset: usize) -> NodeId {
    let mut best = root;
    let mut stack = vec![root];

    while let Some(id) = stack.pop() {
        let Some(scope) = st.get_scope(id) else {
            continue;
        };

        if id != root && !scope_contains(scope, offset) {
            continue;
        }

        best = id;
        stack.extend_from_slice(&scope.children);
    }

    best
}

/// Whether a scope's range covers `offset`.
fn scope_contains(scope: &Scope, offset: usize) -> bool {
    let (start, end) = scope.range;
    if start == 0 && end == 0 {
        return true; // unbounded (module-level)
    }
    offset >= start && offset < end
}

/// Walk up from a scope to find the owning module ident.
fn module_ident_for_scope(st: &SymbolTable, scope_id: NodeId) -> Option<String> {
    let mut current = scope_id;
    while current > 0 {
        let scope = st.get_scope(current)?;
        match &scope.kind {
            ScopeKind::Module(ident) => return Some(ident.clone()),
            ScopeKind::Global => {
                // Symbol defined in the global scope (builtin).
                // Map it to whichever module — builtins are synthetic;
                // returning None means "no navigable source".
                return None;
            }
            _ => current = scope.parent,
        }
    }
    None
}

// ─── Location helpers ───────────────────────────────────────────────────────────

/// Get the char-offset range `(start, end)` of a symbol's definition site.
fn symbol_def_range(kind: &SymbolKind) -> (usize, usize) {
    match kind {
        SymbolKind::Var(v) => {
            let v = v.lock().unwrap();
            (v.symbol_start, v.symbol_end)
        }
        SymbolKind::Fn(f) => {
            let f = f.lock().unwrap();
            (f.symbol_start, f.symbol_end)
        }
        SymbolKind::Type(t) => {
            let t = t.lock().unwrap();
            (t.symbol_start, t.symbol_end)
        }
        SymbolKind::Const(c) => {
            let c = c.lock().unwrap();
            (c.symbol_start, c.symbol_end)
        }
    }
}

/// Convert a resolved symbol into an LSP `Location`.
fn symbol_to_location(project: &Project, symbol: &ResolvedSymbol) -> Option<Location> {
    let (def_start, def_end) = symbol_def_range(&symbol.kind);

    let db = project.module_db.lock().ok()?;
    let module = db.iter().find(|m| m.ident == symbol.module_ident)?;

    let start = offset_to_position(def_start, &module.rope)?;
    let end = offset_to_position(def_end, &module.rope)?;
    let uri = Url::from_file_path(&module.path).ok()?;

    Some(Location {
        uri,
        range: Range { start, end },
    })
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyzer::symbol::{Scope, ScopeKind, SymbolTable};

    #[test]
    fn scope_contains_unbounded() {
        let scope = Scope {
            parent: 0,
            symbols: vec![],
            children: vec![],
            symbol_map: Default::default(),
            range: (0, 0),
            kind: ScopeKind::Global,
            frees: Default::default(),
        };
        assert!(scope_contains(&scope, 0));
        assert!(scope_contains(&scope, 999));
    }

    #[test]
    fn scope_contains_bounded() {
        let scope = Scope {
            parent: 0,
            symbols: vec![],
            children: vec![],
            symbol_map: Default::default(),
            range: (10, 50),
            kind: ScopeKind::Local,
            frees: Default::default(),
        };
        assert!(!scope_contains(&scope, 9));
        assert!(scope_contains(&scope, 10));
        assert!(scope_contains(&scope, 49));
        assert!(!scope_contains(&scope, 50));
    }

    #[test]
    fn find_scope_picks_deepest() {
        let mut st = SymbolTable::new();

        // Create a module scope (root).
        let module_id = st.create_scope(ScopeKind::Module("test".into()), st.global_scope_id, 0, 0);
        // Create a child scope at [10, 50).
        let _child = st.create_scope(ScopeKind::Local, module_id, 10, 50);
        // Create a nested child at [20, 40).
        let nested = st.create_scope(ScopeKind::Local, _child, 20, 40);

        assert_eq!(find_scope_at_offset(&st, module_id, 5), module_id);
        assert_eq!(find_scope_at_offset(&st, module_id, 15), _child);
        assert_eq!(find_scope_at_offset(&st, module_id, 25), nested);
        assert_eq!(find_scope_at_offset(&st, module_id, 45), _child);
        assert_eq!(find_scope_at_offset(&st, module_id, 55), module_id);
    }

    #[test]
    fn symbol_def_range_var() {
        use crate::analyzer::common::VarDeclExpr;
        use std::sync::{Arc, Mutex};

        let var = Arc::new(Mutex::new(VarDeclExpr {
            ident: "x".into(),
            symbol_id: 0,
            symbol_start: 10,
            symbol_end: 11,
            type_: crate::analyzer::common::Type::default(),
            be_capture: false,
            heap_ident: None,
        }));
        assert_eq!(symbol_def_range(&SymbolKind::Var(var)), (10, 11));
    }
}
