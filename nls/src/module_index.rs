//! Package module index: scans a package directory into a `ModulePath -> ModuleUnit` index.
//!
//! Applies the same rules as `src/module_index.c`:
//! - a `.n` file without `mod` is a standalone file module
//! - a leading `mod X` makes the file join its directory's directory module
//! - `<directory>/<directory>.n` is the canonical directory-module entry
//! - only a single-file nested directory module may omit `mod`
//! - `mod` in the package root must equal `package.toml.name`, in a normal subdirectory the directory basename
//! - all variants of one logical source slot (target suffix stripped) must belong to the same module
//! - the scan stops at a nested package.toml

use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::sync::Mutex;

use lazy_static::lazy_static;

pub const MOD_DECL_IDENT: &str = "mod";
const PACKAGE_TOML: &str = "package.toml";

const OS_NAMES: [&str; 3] = ["linux", "darwin", "windows"];
const ARCH_NAMES: [&str; 4] = ["amd64", "arm64", "riscv64", "wasm"];

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum VariantKind {
    Plain = 0,
    Os = 1,
    OsArch = 2,
}

#[derive(Debug, Clone)]
pub struct ModuleUnit {
    /// "" is the package root module, otherwise like "utils" / "net.http"
    pub module_path: String,
    /// Symbol table prefix: <package_name>[.<module_path>]
    pub module_ident: String,
    /// Internal symbol/scope prefix, qualified by the concrete package directory
    pub module_key: String,
    /// true when the module is named by its directory
    pub is_dir_module: bool,
    /// Absolute paths of the active source parts, sorted in path byte order
    pub sources: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct ModuleIndexError {
    pub rel_path: String,
    pub message: String,
}

#[derive(Debug, Clone)]
pub struct PackageUnit {
    pub package_dir: String,
    pub package_name: String,
    /// module_path -> ModuleUnit
    pub modules: HashMap<String, ModuleUnit>,
    /// slot key (absolute path with the target suffix stripped) -> module_path
    pub slots: HashMap<String, String>,
    /// slot key -> the source file active for the current target
    pub slot_active: HashMap<String, String>,
    pub errors: Vec<ModuleIndexError>,
}

lazy_static! {
    static ref PACKAGE_UNITS: Mutex<HashMap<String, PackageUnit>> = Mutex::new(HashMap::new());
}

fn build_os() -> String {
    std::env::var("BUILD_OS")
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| crate::analyzer::target_os().to_string())
}

fn build_arch() -> String {
    std::env::var("BUILD_ARCH")
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| crate::analyzer::target_arch().to_string())
}

fn ident_is_valid(ident: &str) -> bool {
    ident.as_bytes().first().map_or(false, |c| c.is_ascii_alphabetic() || *c == b'_')
}

pub fn module_ident_join(package_name: &str, module_path: &str) -> String {
    if module_path.is_empty() {
        package_name.to_string()
    } else {
        format!("{}.{}", package_name, module_path)
    }
}

pub fn module_key_join(package_dir: &str, module_path: &str) -> String {
    let identity = std::fs::canonicalize(package_dir)
        .ok()
        .and_then(|path| path.to_str().map(str::to_string))
        .unwrap_or_else(|| package_dir.to_string());
    let hash = identity
        .as_bytes()
        .iter()
        .fold(1469598103934665603u64, |hash, byte| (hash ^ u64::from(*byte)).wrapping_mul(1099511628211));
    module_ident_join(&format!("__pkg_{hash:016x}"), module_path)
}

pub fn module_source_rel_path(package_dir: &str, source_path: &str) -> String {
    let rel = source_path.strip_prefix(package_dir).unwrap_or(source_path);
    rel.trim_start_matches(['/', '\\']).replace('\\', "/")
}

/// Read the leading mod declaration of a source file, skipping BOM/whitespace/comments.
/// mod and the identifier after it must sit on the same line, matching syntax::is_mod_decl.
pub fn read_mod_decl(source: &str) -> Option<String> {
    let bytes = source.as_bytes();
    let mut i = 0usize;

    // UTF-8 BOM
    if bytes.len() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF {
        i = 3;
    }

    while i < bytes.len() {
        match bytes[i] {
            b' ' | b'\t' | b'\r' | b'\n' => i += 1,
            b'/' if i + 1 < bytes.len() && bytes[i + 1] == b'/' => {
                i += 2;
                while i < bytes.len() && bytes[i] != b'\n' {
                    i += 1;
                }
            }
            b'/' if i + 1 < bytes.len() && bytes[i + 1] == b'*' => {
                i += 2;
                while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                    i += 1;
                }
                i = (i + 2).min(bytes.len());
            }
            _ => break,
        }
    }

    let rest = &source[i.min(source.len())..];
    let rest = rest.strip_prefix(MOD_DECL_IDENT)?;

    // mod must be followed by a space or tab, otherwise this is just an identifier starting with mod
    if !rest.starts_with(' ') && !rest.starts_with('\t') {
        return None;
    }

    // skip spaces and inline block comments; the lexer drops comments, so this must behave the same
    let mut rest = rest;
    loop {
        rest = rest.trim_start_matches([' ', '\t']);

        if !rest.starts_with("/*") {
            break;
        }

        let end = rest.find("*/")?;
        // a block comment spanning lines puts mod and ident on different lines, so this is not a module declaration
        if rest[..end].contains('\n') {
            return None;
        }
        rest = &rest[end + 2..];
    }
    let ident: String = rest.chars().take_while(|c| c.is_ascii_alphanumeric() || *c == '_').collect();

    if ident.is_empty() {
        None
    } else {
        Some(ident)
    }
}

fn read_mod_decl_from_file(path: &str) -> Option<String> {
    let source = std::fs::read_to_string(path).ok()?;
    read_mod_decl(&source)
}

/// Parse the target suffix in a file name, returning (base, kind, os, arch)
fn parse_variant(filename: &str) -> (String, VariantKind, String, String) {
    let stem = filename.strip_suffix(".n").unwrap_or(filename);

    if let Some(dot) = stem.rfind('.') {
        let suffix = &stem[dot + 1..];

        if let Some(underscore) = suffix.find('_') {
            let os = &suffix[..underscore];
            let arch = &suffix[underscore + 1..];
            if OS_NAMES.contains(&os) && ARCH_NAMES.contains(&arch) {
                return (stem[..dot].to_string(), VariantKind::OsArch, os.to_string(), arch.to_string());
            }
        } else if OS_NAMES.contains(&suffix) {
            return (stem[..dot].to_string(), VariantKind::Os, suffix.to_string(), String::new());
        }
    }

    (stem.to_string(), VariantKind::Plain, String::new(), String::new())
}

/// Compute the slot key: the canonical path with the target suffix stripped
pub fn source_slot_key(source_path: &str) -> Option<String> {
    let path = Path::new(source_path);
    let filename = path.file_name()?.to_str()?;
    if !filename.ends_with(".n") {
        return None;
    }

    let (base, ..) = parse_variant(filename);
    let dir = path.parent()?.to_str()?;
    Some(format!("{}/{}.n", dir, base))
}

#[derive(Debug, Clone)]
struct ScanVariant {
    path: String,
    kind: VariantKind,
    os: String,
    arch: String,
    mod_ident: Option<String>,
}

#[derive(Debug, Clone)]
struct ScanSlot {
    slot_key: String,
    rel_dir: String,
    base: String,
    variants: Vec<ScanVariant>,
}

fn scan_dir(dir: &Path, rel_dir: &str, slots: &mut HashMap<String, ScanSlot>, visited: &mut HashSet<String>, overlay: Option<(&str, &str)>) {
    let identity = std::fs::canonicalize(dir)
        .ok()
        .and_then(|path| path.to_str().map(str::to_string))
        .unwrap_or_else(|| dir.to_string_lossy().to_string());
    if !visited.insert(identity) {
        return;
    }

    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };

    let mut names: Vec<String> = entries
        .filter_map(|e| e.ok())
        .filter_map(|e| e.file_name().to_str().map(|s| s.to_string()))
        .filter(|name| !name.starts_with('.'))
        .collect();
    names.sort();

    for name in names {
        let full_path = dir.join(&name);

        if full_path.is_dir() {
            // a nested package owns a separate PackageInstanceId and is not part of this scan
            if full_path.join(PACKAGE_TOML).exists() {
                continue;
            }

            let child_rel = if rel_dir.is_empty() { name.clone() } else { format!("{}/{}", rel_dir, name) };
            scan_dir(&full_path, &child_rel, slots, visited, overlay);
            continue;
        }

        if !name.ends_with(".n") {
            continue;
        }

        let (base, kind, os, arch) = parse_variant(&name);
        let slot_key = format!("{}/{}.n", dir.to_str().unwrap_or(""), base);
        let path = full_path.to_str().unwrap_or("").to_string();
        let mod_ident = match overlay {
            Some((overlay_path, source)) if overlay_path == path => read_mod_decl(source),
            _ => read_mod_decl_from_file(&path),
        };

        slots
            .entry(slot_key.clone())
            .or_insert_with(|| ScanSlot {
                slot_key,
                rel_dir: rel_dir.to_string(),
                base,
                variants: Vec::new(),
            })
            .variants
            .push(ScanVariant {
                path,
                kind,
                os,
                arch,
                mod_ident,
            });
    }
}

fn select_variant<'a>(slot: &'a ScanSlot, os: &str, arch: &str) -> Option<&'a ScanVariant> {
    let mut result: Option<&ScanVariant> = None;

    for variant in &slot.variants {
        match variant.kind {
            VariantKind::OsArch if variant.os != os || variant.arch != arch => continue,
            VariantKind::Os if variant.os != os => continue,
            _ => {}
        }

        if result.as_ref().map_or(true, |current| variant.kind > current.kind) {
            result = Some(variant);
        }
    }

    result
}

fn is_dir_entry(slot: &ScanSlot, package_name: &str) -> bool {
    if slot.rel_dir.is_empty() {
        slot.base == package_name
    } else {
        slot.base == slot.rel_dir.rsplit('/').next().unwrap_or(&slot.rel_dir)
    }
}

fn layout_desc(package_dir: &str, unit: &ModuleUnit) -> String {
    let first = unit
        .sources
        .first()
        .map(|s| module_source_rel_path(package_dir, s))
        .unwrap_or_else(|| "?".to_string());
    format!(
        "{} ({})",
        first,
        if unit.is_dir_module {
            "part of directory module"
        } else {
            "standalone file module"
        }
    )
}

fn build_package_unit(package_dir: &str, package_name: &str, overlay: Option<(&str, &str)>) -> PackageUnit {
    let mut pu = PackageUnit {
        package_dir: package_dir.to_string(),
        package_name: package_name.to_string(),
        modules: HashMap::new(),
        slots: HashMap::new(),
        slot_active: HashMap::new(),
        errors: Vec::new(),
    };

    let mut slot_table: HashMap<String, ScanSlot> = HashMap::new();
    scan_dir(Path::new(package_dir), "", &mut slot_table, &mut HashSet::new(), overlay);

    // directory enumeration order must not affect the result
    let mut slots: Vec<ScanSlot> = slot_table.into_values().collect();
    slots.sort_by(|a, b| a.slot_key.cmp(&b.slot_key));

    let os = build_os();
    let arch = build_arch();
    let mut dir_entries: HashMap<String, &ScanVariant> = HashMap::new();
    let mut conflicted_modules = HashSet::new();

    for slot in &slots {
        let mut slot_valid = true;

        // an inactive variant's mod header still has to be checked
        for variant in &slot.variants {
            let Some(mod_ident) = &variant.mod_ident else {
                continue;
            };
            let rel_path = module_source_rel_path(package_dir, &variant.path);

            if !ident_is_valid(mod_ident) {
                pu.errors.push(ModuleIndexError {
                    rel_path,
                    message: format!("'mod {}' is not a valid identifier", mod_ident),
                });
                slot_valid = false;
                continue;
            }

            if slot.rel_dir.is_empty() {
                if mod_ident != package_name {
                    pu.errors.push(ModuleIndexError {
                        rel_path,
                        message: format!("'mod {}' does not match package name '{}', use 'mod {}'", mod_ident, package_name, package_name),
                    });
                    slot_valid = false;
                }
                continue;
            }

            let dir_name = slot.rel_dir.rsplit('/').next().unwrap_or(&slot.rel_dir);
            if mod_ident != dir_name {
                pu.errors.push(ModuleIndexError {
                    rel_path,
                    message: format!("'mod {}' does not match directory '{}', use 'mod {}'", mod_ident, dir_name, dir_name),
                });
                slot_valid = false;
            }
        }

        // all variants of one slot must belong to the same module
        if let Some(first) = slot.variants.first() {
            for variant in slot.variants.iter().skip(1) {
                if variant.mod_ident != first.mod_ident {
                    pu.errors.push(ModuleIndexError {
                        rel_path: module_source_rel_path(package_dir, &variant.path),
                        message: format!("target variants of '{}' must belong to the same module", slot.base),
                    });
                    slot_valid = false;
                }
            }
        }

        // Do not derive more layout diagnostics from an already invalid declaration.
        if !slot_valid {
            continue;
        }

        let Some(active) = select_variant(slot, &os, &arch) else {
            continue;
        };

        pu.slot_active.insert(slot.slot_key.clone(), active.path.clone());

        let dir_entry = is_dir_entry(slot, package_name);
        let (module_path, is_dir_module) = if active.mod_ident.is_some() || dir_entry {
            (slot.rel_dir.replace('/', "."), true)
        } else if slot.rel_dir.is_empty() {
            (slot.base.clone(), false)
        } else {
            (format!("{}.{}", slot.rel_dir.replace('/', "."), slot.base), false)
        };

        if is_dir_module && dir_entry {
            dir_entries.insert(module_path.clone(), active);
        }

        pu.slots.insert(slot.slot_key.clone(), module_path.clone());

        if let Some(existing) = pu.modules.get_mut(&module_path) {
            // only an identical full ModuleId is a conflict
            if existing.is_dir_module && is_dir_module {
                existing.sources.push(active.path.clone());
                continue;
            }

            let conflict = ModuleUnit {
                module_path: module_path.clone(),
                module_ident: module_ident_join(package_name, &module_path),
                module_key: module_key_join(package_dir, &module_path),
                is_dir_module,
                sources: vec![active.path.clone()],
            };
            let existing_desc = layout_desc(package_dir, existing);
            if conflicted_modules.insert(module_path) {
                pu.errors.push(ModuleIndexError {
                    rel_path: module_source_rel_path(package_dir, &active.path),
                    message: format!(
                        "module {} is defined by two layouts: {} and {}",
                        conflict.module_ident,
                        existing_desc,
                        layout_desc(package_dir, &conflict)
                    ),
                });
            }
            continue;
        }

        pu.modules.insert(
            module_path.clone(),
            ModuleUnit {
                module_ident: module_ident_join(package_name, &module_path),
                module_key: module_key_join(package_dir, &module_path),
                module_path,
                is_dir_module,
                sources: vec![active.path.clone()],
            },
        );
    }

    for unit in pu.modules.values() {
        if !unit.is_dir_module {
            continue;
        }

        let expected = if unit.module_path.is_empty() {
            package_name
        } else {
            unit.module_path.rsplit('.').next().unwrap_or(&unit.module_path)
        };
        let Some(entry) = dir_entries.get(&unit.module_path) else {
            pu.errors.push(ModuleIndexError {
                rel_path: unit.sources.first().map(|path| module_source_rel_path(package_dir, path)).unwrap_or_default(),
                message: format!(
                    "directory module {} must have entry '{}.n' declaring 'mod {}'",
                    unit.module_ident, expected, expected
                ),
            });
            continue;
        };

        if (unit.module_path.is_empty() || unit.sources.len() > 1) && entry.mod_ident.is_none() {
            pu.errors.push(ModuleIndexError {
                rel_path: module_source_rel_path(package_dir, &entry.path),
                message: format!("module {} requires 'mod {}' in '{}.n'", unit.module_ident, expected, expected),
            });
        }
    }

    for unit in pu.modules.values_mut() {
        unit.sources.sort();
    }

    pu
}

/// Load (cached) the package module index
pub fn package_unit_load(package_dir: &str, package_name: &str) -> PackageUnit {
    let mut cache = PACKAGE_UNITS.lock().unwrap();
    if let Some(pu) = cache.get(package_dir) {
        return pu.clone();
    }

    let pu = build_package_unit(package_dir, package_name, None);
    cache.insert(package_dir.to_string(), pu.clone());
    pu
}

/// Build an uncached package index with one in-memory source overlay.
pub fn package_unit_with_overlay(package_dir: &str, package_name: &str, source_path: &str, source: &str) -> PackageUnit {
    build_package_unit(package_dir, package_name, Some((source_path, source)))
}

/// The index must be rebuilt when a source file inside the package changes
pub fn package_unit_invalidate(source_path: &str) {
    let mut cache = PACKAGE_UNITS.lock().unwrap();
    cache.retain(|package_dir, _| !source_path.starts_with(package_dir.as_str()));
}

pub fn package_unit_reset() {
    PACKAGE_UNITS.lock().unwrap().clear();
}

impl PackageUnit {
    pub fn find_module(&self, module_path: &str) -> Option<&ModuleUnit> {
        self.modules.get(module_path)
    }

    pub fn find_source(&self, source_path: &str) -> Option<&ModuleUnit> {
        let slot_key = source_slot_key(source_path)?;
        let module_path = self.slots.get(&slot_key)?;
        self.modules.get(module_path)
    }

    /// Whether unit contains a source part whose basename is <name>.n
    pub fn unit_has_source_named(unit: &ModuleUnit, name: &str) -> bool {
        let expect = format!("{}.n", name);
        unit.sources.iter().any(|source| {
            source_slot_key(source)
                .and_then(|slot| Path::new(&slot).file_name()?.to_str().map(str::to_string))
                .as_deref()
                == Some(expect.as_str())
        })
    }

    pub fn slot_active(&self, source_path: &str) -> Option<&String> {
        let slot_key = source_slot_key(source_path)?;
        self.slot_active.get(&slot_key)
    }

    /// Collect the other source parts of the module owning source_path
    pub fn sibling_sources(&self, source_path: &str) -> Vec<String> {
        let Some(unit) = self.find_source(source_path) else {
            return Vec::new();
        };
        if !unit.is_dir_module {
            return Vec::new();
        }
        unit.sources.iter().filter(|s| s.as_str() != source_path).cloned().collect()
    }
}

/// Build the module path of an import path: ast_package[1..] joined with "."
pub fn import_module_path(ast_package: &[String]) -> String {
    ast_package.iter().skip(1).cloned().collect::<Vec<_>>().join(".")
}

/// Parent path of module_path, "net.http" -> "net", "net" -> ""
pub fn module_parent_path(module_path: &str) -> String {
    match module_path.rfind('.') {
        Some(i) => module_path[..i].to_string(),
        None => String::new(),
    }
}
