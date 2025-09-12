use crate::analyzer::common::AnalyzerError;
use crate::project::Module;
use ropey::Rope;
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use tower_lsp::lsp_types::Position;

pub fn offset_to_position(offset: usize, rope: &Rope) -> Option<Position> {
    let line = rope.try_char_to_line(offset).ok()?;
    let first_char_of_line = rope.try_line_to_char(line).ok()?;
    let column = offset - first_char_of_line;
    Some(Position::new(line as u32, column as u32))
}

pub fn position_to_offset(position: Position, rope: &Rope) -> Option<usize> {
    let line_char_offset = rope.try_line_to_char(position.line as usize).ok()?;
    let slice = rope.slice(0..line_char_offset + position.character as usize);
    Some(slice.len_bytes())
}

pub fn format_global_ident(prefix: String, ident: String) -> String {
    // 如果 prefix 为空，则直接返回 ident
    if prefix.is_empty() {
        return ident;
    }

    format!("{prefix}.{ident}")
}

pub fn format_impl_ident(impl_ident: String, key: String) -> String {
    format!("{impl_ident}.{key}")
}

pub fn format_generics_ident(ident: String, hash: u64) -> String {
    assert!(hash != 0, "hash must not be 0");

    if ident.contains('#') {
        return ident;
    }

    format!("{ident}#{}", hash)
}

pub fn calculate_hash<T: Hash>(t: &T) -> u64 {
    let mut hasher = DefaultHasher::new();
    t.hash(&mut hasher);
    hasher.finish()
}

pub fn align_up(n: u64, align: u64) -> u64 {
    if align == 0 {
        return n;
    }
    (n + align - 1) & !(align - 1)
}

pub fn errors_push(m: &mut Module, e: AnalyzerError) {
    // if m.index == 16 {
        //  panic!("TODO analyzer error");
    // }
    m.analyzer_errors.push(e);
}
