use log::debug;
use std::collections::HashMap;
use std::path::Path;

/// Represents a top-level symbol found in a workspace file
#[derive(Debug, Clone)]
pub struct IndexedSymbol {
    pub name: String,         // The symbol name (e.g., "MyController")
    pub kind: IndexedSymbolKind,
    pub file_path: String,    // Absolute path to the .n file
}

#[derive(Debug, Clone, PartialEq)]
pub enum IndexedSymbolKind {
    Type,     // type definitions (struct, interface, alias, enum)
    Function, // fn definitions
    Variable, // var definitions
    Constant, // const definitions
}

/// Lightweight workspace index that scans all .n files for top-level declarations
/// without performing full semantic analysis.
#[derive(Debug, Clone)]
pub struct WorkspaceIndex {
    /// Map from symbol name -> list of indexed symbols (multiple files can define same name)
    pub symbols: HashMap<String, Vec<IndexedSymbol>>,
    /// Set of all indexed file paths (to track what's been indexed)
    pub indexed_files: HashMap<String, u64>, // path -> last modified timestamp (0 if unknown)
}

impl WorkspaceIndex {
    pub fn new() -> Self {
        Self {
            symbols: HashMap::new(),
            indexed_files: HashMap::new(),
        }
    }

    /// Scan a workspace root directory and index all .n files
    pub fn scan_workspace(&mut self, root: &str, nature_root: &str) {
        debug!("WorkspaceIndex: scanning workspace root '{}'", root);

        // 1. Scan project directory
        self.scan_directory(root, root);

        // 2. Scan standard library
        let std_dir = Path::new(nature_root).join("std");
        if std_dir.exists() {
            self.scan_directory(std_dir.to_str().unwrap_or(""), root);
        }

        debug!(
            "WorkspaceIndex: indexed {} files, {} unique symbol names",
            self.indexed_files.len(),
            self.symbols.len()
        );
    }

    /// Recursively scan a directory for .n files
    fn scan_directory(&mut self, dir: &str, project_root: &str) {
        let dir_path = Path::new(dir);
        if !dir_path.exists() || !dir_path.is_dir() {
            return;
        }

        let entries = match std::fs::read_dir(dir_path) {
            Ok(entries) => entries,
            Err(_) => return,
        };

        for entry in entries {
            let entry = match entry {
                Ok(e) => e,
                Err(_) => continue,
            };

            let path = entry.path();

            // Skip hidden directories and common non-source directories
            if let Some(name) = path.file_name().and_then(|n| n.to_str()) {
                if name.starts_with('.') || name == "build" || name == "build-runtime"
                    || name == "node_modules" || name == "target" || name == "release"
                {
                    continue;
                }
            }

            if path.is_dir() {
                self.scan_directory(path.to_str().unwrap_or(""), project_root);
            } else if path.is_file() {
                if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
                    if ext == "n" {
                        self.index_file(path.to_str().unwrap_or(""), project_root);
                    }
                }
            }
        }
    }

    /// Index a single .n file by doing a lightweight line-by-line scan for top-level declarations
    pub fn index_file(&mut self, file_path: &str, _project_root: &str) {
        let content = match std::fs::read_to_string(file_path) {
            Ok(c) => c,
            Err(_) => return,
        };

        // Get file modification time for incremental updates
        let mtime = std::fs::metadata(file_path)
            .and_then(|m| m.modified())
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).map_err(|_| std::io::Error::new(std::io::ErrorKind::Other, "")))
            .map(|d| d.as_secs())
            .unwrap_or(0);

        // Check if file hasn't changed
        if let Some(&cached_mtime) = self.indexed_files.get(file_path) {
            if cached_mtime == mtime && mtime > 0 {
                return; // File hasn't changed, skip re-indexing
            }
        }

        // Remove old symbols from this file
        self.remove_file_symbols(file_path);

        // Parse top-level symbols from the file content
        let symbols = Self::extract_top_level_symbols(&content, file_path);

        // Register symbols
        for symbol in symbols {
            self.symbols
                .entry(symbol.name.clone())
                .or_insert_with(Vec::new)
                .push(symbol);
        }

        self.indexed_files.insert(file_path.to_string(), mtime);
    }

    /// Remove all symbols associated with a file path
    pub fn remove_file_symbols(&mut self, file_path: &str) {
        // Remove symbols from each entry and clean up empty entries
        self.symbols.retain(|_, symbols| {
            symbols.retain(|s| s.file_path != file_path);
            !symbols.is_empty()
        });
        self.indexed_files.remove(file_path);
    }

    /// Lightweight extraction of top-level symbol names from source code.
    /// This does NOT do full parsing — it simply scans for declaration patterns
    /// at the top indentation level.
    fn extract_top_level_symbols(content: &str, file_path: &str) -> Vec<IndexedSymbol> {
        let mut symbols = Vec::new();
        let mut in_block_comment = false;

        let lines: Vec<&str> = content.lines().collect();

        // We need to track brace depth to know if we're at top level.
        // Process character by character but extract symbols line by line.
        let mut line_start_brace_depths: Vec<i32> = Vec::with_capacity(lines.len());

        // First pass: compute brace depth at the start of each line
        let mut depth: i32 = 0;
        for line in &lines {
            line_start_brace_depths.push(depth);

            let mut chars = line.chars().peekable();
            let mut prev = '\0';
            let mut in_str = false;
            let mut in_lc = false;

            while let Some(ch) = chars.next() {
                if in_lc {
                    break; // rest of line is comment
                }

                match ch {
                    '/' if !in_str => {
                        if let Some(&next) = chars.peek() {
                            if next == '/' {
                                in_lc = true;
                                chars.next();
                                continue;
                            } else if next == '*' {
                                in_block_comment = true;
                                chars.next();
                                continue;
                            }
                        }
                    }
                    '*' if in_block_comment => {
                        if let Some(&next) = chars.peek() {
                            if next == '/' {
                                in_block_comment = false;
                                chars.next();
                                continue;
                            }
                        }
                    }
                    _ if in_block_comment => continue,
                    '"' if prev != '\\' => {
                        in_str = !in_str;
                    }
                    '\'' if prev != '\\' && !in_str => {
                        // Skip character literals / string literals with single quotes
                        // In Nature, single-quoted strings are file imports, skip content
                    }
                    '{' if !in_str => depth += 1,
                    '}' if !in_str => depth -= 1,
                    _ => {}
                }
                prev = ch;
            }
        }

        // Second pass: extract top-level declarations
        for (i, line) in lines.iter().enumerate() {
            let depth = line_start_brace_depths[i];
            if depth != 0 {
                continue; // Not at top level
            }

            let trimmed = line.trim();

            // Skip empty lines and comments
            if trimmed.is_empty() || trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") {
                continue;
            }

            // Skip import statements
            if trimmed.starts_with("import ") {
                continue;
            }

            // Skip preprocessor/macro directives
            if trimmed.starts_with('#') {
                continue;
            }

            // type <name> = ...
            if trimmed.starts_with("type ") {
                if let Some(name) = Self::extract_type_name(trimmed) {
                    symbols.push(IndexedSymbol {
                        name,
                        kind: IndexedSymbolKind::Type,
                        file_path: file_path.to_string(),
                    });
                }
                continue;
            }

            // fn <name>(...) or fn <type>.<method>(...)
            if trimmed.starts_with("fn ") {
                if let Some(name) = Self::extract_fn_name(trimmed) {
                    // Only index top-level functions, not methods (type.method)
                    if !name.contains('.') {
                        symbols.push(IndexedSymbol {
                            name,
                            kind: IndexedSymbolKind::Function,
                            file_path: file_path.to_string(),
                        });
                    }
                }
                continue;
            }

            // const <name> = ...
            if trimmed.starts_with("const ") {
                if let Some(name) = Self::extract_const_name(trimmed) {
                    symbols.push(IndexedSymbol {
                        name,
                        kind: IndexedSymbolKind::Constant,
                        file_path: file_path.to_string(),
                    });
                }
                continue;
            }

            // var <name> = ... OR <type> <name> = ...
            if trimmed.starts_with("var ") {
                if let Some(name) = Self::extract_var_name(trimmed) {
                    symbols.push(IndexedSymbol {
                        name,
                        kind: IndexedSymbolKind::Variable,
                        file_path: file_path.to_string(),
                    });
                }
                continue;
            }

            // Explicit typed variable: <type> <name> = ...
            // This is trickier - we need to detect patterns like: string foo = "bar"
            // We skip this for now to avoid false positives; these are less common for
            // cross-file usage since they're typically module-level state.
        }

        symbols
    }

    /// Extract type name from "type <name> = ..." or "type <name>:<interfaces> = ..."
    fn extract_type_name(line: &str) -> Option<String> {
        let rest = line.strip_prefix("type ")?.trim_start();
        let name: String = rest.chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
        if name.is_empty() {
            return None;
        }
        Some(name)
    }

    /// Extract function name from "fn <name>(...)"
    fn extract_fn_name(line: &str) -> Option<String> {
        let rest = line.strip_prefix("fn ")?.trim_start();
        let name: String = rest.chars().take_while(|c| c.is_alphanumeric() || *c == '_' || *c == '.').collect();
        if name.is_empty() {
            return None;
        }
        Some(name)
    }

    /// Extract const name from "const <name> = ..."
    fn extract_const_name(line: &str) -> Option<String> {
        let rest = line.strip_prefix("const ")?.trim_start();
        let name: String = rest.chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
        if name.is_empty() {
            return None;
        }
        Some(name)
    }

    /// Extract var name from "var <name> = ..."
    fn extract_var_name(line: &str) -> Option<String> {
        let rest = line.strip_prefix("var ")?.trim_start();
        let name: String = rest.chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
        if name.is_empty() {
            return None;
        }
        Some(name)
    }

    /// Search for symbols matching a prefix (case-sensitive)
    pub fn find_symbols_by_prefix(&self, prefix: &str) -> Vec<&IndexedSymbol> {
        if prefix.is_empty() {
            return Vec::new();
        }

        let mut results = Vec::new();
        for (name, symbols) in &self.symbols {
            if name.starts_with(prefix) {
                for symbol in symbols {
                    results.push(symbol);
                }
            }
        }
        results
    }

    /// Search for symbols with case-insensitive prefix matching
    pub fn find_symbols_by_prefix_case_insensitive(&self, prefix: &str) -> Vec<&IndexedSymbol> {
        if prefix.is_empty() {
            return Vec::new();
        }

        let prefix_lower = prefix.to_lowercase();
        let mut results = Vec::new();
        for (name, symbols) in &self.symbols {
            if name.to_lowercase().starts_with(&prefix_lower) {
                for symbol in symbols {
                    results.push(symbol);
                }
            }
        }
        results
    }

    /// Compute the import statement and module name for a given file path, relative to either:
    /// - The current module's directory (file-based import: import 'foo.n')
    /// - The project package (package-based import: import pkg.subdir.module)
    /// - The standard library (import module_name)
    pub fn compute_import_info(
        &self,
        symbol_file: &str,
        current_module_dir: &str,
        current_module_path: &str,
        project_root: &str,
        nature_root: &str,
        package_name: Option<&str>,
    ) -> Option<ImportInfo> {
        let symbol_path = Path::new(symbol_file);

        // Don't import from the same file
        if symbol_file == current_module_path {
            return None;
        }

        let symbol_dir = symbol_path.parent()?.to_str()?;
        let file_stem = symbol_path.file_stem()?.to_str()?;

        // 1. Check if it's in the same directory -> file-based import
        if symbol_dir == current_module_dir {
            let file_name = symbol_path.file_name()?.to_str()?;
            return Some(ImportInfo {
                import_statement: format!("import '{}'\n", file_name),
                module_as_name: file_stem.to_string(),
                import_base: format!("import '{}'", file_name),
            });
        }

        // 2. Check if it's in the standard library
        let std_dir = Path::new(nature_root).join("std");
        if let Ok(std_canonical) = std_dir.canonicalize() {
            if let Ok(symbol_canonical) = symbol_path.canonicalize() {
                if symbol_canonical.starts_with(&std_canonical) {
                    // Get the relative path from std/
                    if let Ok(rel) = symbol_canonical.strip_prefix(&std_canonical) {
                        let components: Vec<&str> = rel
                            .components()
                            .filter_map(|c| c.as_os_str().to_str())
                            .collect();

                        if components.len() >= 1 {
                            // Standard library: first component is the package name
                            // e.g., std/fmt/... -> import fmt
                            let std_package = components[0];

                            if components.len() == 1 {
                                // It's the package root file (shouldn't usually happen)
                                let pkg = std_package.trim_end_matches(".n");
                                return Some(ImportInfo {
                                    import_statement: format!("import {}\n", pkg),
                                    module_as_name: pkg.to_string(),
                                    import_base: format!("import {}", pkg),
                                });
                            } else {
                                // It's a submodule of a std package
                                let module_path: Vec<String> = components.iter()
                                    .map(|c| c.trim_end_matches(".n").to_string())
                                    .collect();
                                let as_name = module_path.last()?.clone();
                                let import_path = module_path.join(".");
                                return Some(ImportInfo {
                                    import_statement: format!("import {}\n", import_path),
                                    module_as_name: as_name,
                                    import_base: format!("import {}", import_path),
                                });
                            }
                        }
                    }
                }
            }
        }

        // 3. Check if it's in the project -> package-based import
        if let Some(pkg_name) = package_name {
            if symbol_file.starts_with(project_root) {
                let rel = &symbol_file[project_root.len()..].trim_start_matches('/');
                let rel_no_ext = rel.trim_end_matches(".n");
                let import_path = format!("{}.{}", pkg_name, rel_no_ext.replace('/', "."));
                let as_name = file_stem.to_string();
                return Some(ImportInfo {
                    import_statement: format!("import {}\n", import_path),
                    module_as_name: as_name,
                    import_base: format!("import {}", import_path),
                });
            }
        }

        None
    }
}

/// Information about how to import a module
#[derive(Debug, Clone)]
pub struct ImportInfo {
    pub import_statement: String,            // Full module import: import 'foo.n'\n
    pub module_as_name: String,              // Module name for qualified access: foo
    pub import_base: String,                 // Base path for selective import (without \n): import 'foo.n' or import pkg.mod
}
