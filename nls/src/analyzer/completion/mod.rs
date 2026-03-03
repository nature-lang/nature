mod context;
mod imports;
mod members;

pub use context::{
    extract_last_ident_part_pub, extract_module_member_context, extract_prefix_at_position,
    extract_selective_import_context, extract_struct_init_context,
};

use crate::analyzer::common::AstFnDef;
use crate::analyzer::symbol::{NodeId, Symbol, SymbolKind, SymbolTable};
use crate::analyzer::workspace_index::WorkspaceIndex;
use crate::project::Module;
use log::debug;

use context::extract_last_ident_part;

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
    pub(crate) symbol_table: &'a mut SymbolTable,
    pub(crate) module: &'a mut Module,
    pub(crate) nature_root: String,
    pub(crate) project_root: String,
    pub(crate) package_config: Option<crate::analyzer::common::PackageConfig>,
    pub(crate) workspace_index: Option<&'a WorkspaceIndex>,
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
            let struct_completions = self.get_struct_field_completions(&type_name, &field_prefix, current_scope_id);
            if !struct_completions.is_empty() {
                return struct_completions;
            }
            // Fall through to try other completion types when the detected
            // "struct init" context doesn't actually resolve to any fields.
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
        debug!("Found scope_id {} by positon {}, module.scope_id={}", current_scope_id, position, self.module.scope_id);

        // cannot auto-import in global module scope
        if current_scope_id == self.module.scope_id {
            debug!("cursor is at module scope, returning empty");
            return Vec::new();
        }

        let has_prefix = !prefix.is_empty();
        debug!("prefix='{}', has_prefix={}", prefix, has_prefix);

        // 2. Collect all visible variable symbols
        let mut completions = Vec::new();
        self.collect_variable_completions(current_scope_id, &prefix, &mut completions, position);
        debug!("after collect_variable_completions: {} items", completions.len());

        // 2b. Collect functions/types defined at the current module scope
        self.collect_module_scope_fn_completions(&prefix, &mut completions);
        debug!("after collect_module_scope_fn_completions: {} items, labels: {:?}",
            completions.len(),
            completions.iter().map(|c| c.label.clone()).collect::<Vec<_>>()
        );

        // 2c. Collect symbols from selective imports (import module.{symbol1, symbol2})
        self.collect_selective_import_symbol_completions(&prefix, &mut completions);

        // 3. Collect already-imported modules — always shown (they're in
        //    scope and useful even on an empty-prefix Cmd+Enter trigger).
        self.collect_imported_module_completions(&prefix, &mut completions);

        // 4. Collect available modules (for auto-import) — always shown,
        //    matching gopls which lists std packages on empty prefix.
        self.collect_module_completions(&prefix, &mut completions);

        // The remaining sources (workspace symbols, keywords) are only
        // useful when the user has started typing.
        if has_prefix {
            // 5. Collect cross-file symbol completions (workspace index)
            self.collect_workspace_symbol_completions(&prefix, &mut completions);

            // 6. Collect keyword completions
            self.collect_keyword_completions(&prefix, &mut completions);
        }

        // 7. Sort and filter
        self.sort_and_filter_completions(&mut completions, &prefix);

        debug!("Found {} completions", completions.len());

        completions
    }

    /// Check if function has parameters (excluding self)
    pub(crate) fn has_parameters(&self, fndef: &AstFnDef) -> bool {
        fndef.params.iter().any(|param| {
            let param_locked = param.lock().unwrap();
            param_locked.ident != "self"
        })
    }

    /// Format a function signature for display
    pub(crate) fn format_function_signature(&self, fndef: &AstFnDef) -> String {
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
    pub(crate) fn find_innermost_scope(&self, scope_id: NodeId, position: usize) -> NodeId {
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

    /// Create completion item
    pub(crate) fn create_completion_item(&self, symbol: &Symbol) -> CompletionItem {
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
    pub(crate) fn create_module_completion_member(&self, symbol: &Symbol) -> CompletionItem {
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
    pub(crate) fn collect_keyword_completions(&self, prefix: &str, completions: &mut Vec<CompletionItem>) {
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
    pub(crate) fn sort_and_filter_completions(&self, completions: &mut Vec<CompletionItem>, prefix: &str) {
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
