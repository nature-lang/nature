//! Nature Language Server (NLS) — library root.
//!
//! Module boundaries:
//! - [`analyzer`]  — lexer, parser, semantic analysis, type system (existing).
//! - [`document`]  — thread-safe document store backed by Rope.
//! - [`package`]   — `package.toml` parsing.
//! - [`project`]   — workspace/project state and build pipeline.
//! - [`server`]    — LSP backend, capabilities, and request dispatch.
//! - [`utils`]     — position/offset conversion, identifier helpers.

pub mod analyzer;
pub mod document;
pub mod package;
pub mod project;
pub mod server;
pub mod utils;
