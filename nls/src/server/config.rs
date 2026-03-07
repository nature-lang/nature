//! Configuration keys, typed accessors, and `apply_config` on [`Backend`].

use dashmap::DashMap;
use log::debug;
use serde_json::Value;

use super::Backend;

// ─── Defaults ───────────────────────────────────────────────────────────────────

/// Default debounce delay (ms) before re-analyzing after a `did_change`.
pub const DEBOUNCE_MS: u64 = 50;

// ─── Configuration keys ─────────────────────────────────────────────────────────

pub const CFG_INLAY_HINTS_ENABLED: &str = "inlayHints.enabled";
pub const CFG_INLAY_TYPE_HINTS: &str = "inlayHints.typeHints";
pub const CFG_INLAY_PARAM_HINTS: &str = "inlayHints.parameterHints";
pub const CFG_DEBOUNCE_MS: &str = "analysis.debounceMs";

// ─── Typed accessors ────────────────────────────────────────────────────────────

/// Read a `bool` config value, returning `default` when absent or wrong type.
pub fn cfg_bool(store: &DashMap<String, Value>, key: &str, default: bool) -> bool {
    store.get(key).and_then(|v| v.as_bool()).unwrap_or(default)
}

/// Read a `u64` config value, returning `default` when absent or wrong type.
pub fn cfg_u64(store: &DashMap<String, Value>, key: &str, default: u64) -> u64 {
    store.get(key).and_then(|v| v.as_u64()).unwrap_or(default)
}

/// Read a `String` config value, returning `default` when absent or wrong type.
pub fn cfg_string(store: &DashMap<String, Value>, key: &str, default: &str) -> String {
    store
        .get(key)
        .and_then(|v| v.as_str().map(String::from))
        .unwrap_or_else(|| default.to_owned())
}

// ─── Backend impl ───────────────────────────────────────────────────────────────

impl Backend {
    /// Flatten a (possibly nested) JSON settings object into dot-separated keys.
    ///
    /// ```json
    /// { "inlayHints": { "enabled": true } }
    /// ```
    /// becomes `"inlayHints.enabled" → true`.
    pub(crate) fn apply_config(&self, settings: &Value) {
        if let Some(obj) = settings.as_object() {
            for (section, value) in obj {
                if let Some(inner) = value.as_object() {
                    for (key, val) in inner {
                        let config_key = format!("{section}.{key}");
                        debug!("config: {config_key} = {val}");
                        self.config.insert(config_key, val.clone());
                    }
                } else {
                    debug!("config: {section} = {value}");
                    self.config.insert(section.clone(), value.clone());
                }
            }
        }
    }

    /// Configured debounce delay in milliseconds.
    pub(crate) fn debounce_delay(&self) -> u64 {
        cfg_u64(&self.config, CFG_DEBOUNCE_MS, DEBOUNCE_MS)
    }
}

// ─── Tests ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cfg_bool_present() {
        let store = DashMap::new();
        store.insert("a.b".into(), Value::Bool(true));
        assert!(cfg_bool(&store, "a.b", false));
    }

    #[test]
    fn cfg_bool_missing_returns_default() {
        let store: DashMap<String, Value> = DashMap::new();
        assert!(!cfg_bool(&store, "missing", false));
        assert!(cfg_bool(&store, "missing", true));
    }

    #[test]
    fn cfg_bool_wrong_type_returns_default() {
        let store = DashMap::new();
        store.insert("key".into(), Value::Number(42.into()));
        assert!(!cfg_bool(&store, "key", false));
    }

    #[test]
    fn cfg_u64_present() {
        let store = DashMap::new();
        store.insert("delay".into(), Value::Number(500.into()));
        assert_eq!(cfg_u64(&store, "delay", 0), 500);
    }

    #[test]
    fn cfg_u64_missing() {
        let store: DashMap<String, Value> = DashMap::new();
        assert_eq!(cfg_u64(&store, "missing", 300), 300);
    }

    #[test]
    fn cfg_string_present() {
        let store = DashMap::new();
        store.insert("name".into(), Value::String("hello".into()));
        assert_eq!(cfg_string(&store, "name", ""), "hello");
    }

    #[test]
    fn cfg_string_missing() {
        let store: DashMap<String, Value> = DashMap::new();
        assert_eq!(cfg_string(&store, "missing", "fallback"), "fallback");
    }

    #[test]
    fn flatten_nested_settings() {
        let store = DashMap::new();
        let settings: Value = serde_json::json!({
            "inlayHints": {
                "enabled": true,
                "typeHints": false
            },
            "topLevel": "value"
        });

        // Simulate apply_config logic directly
        if let Some(obj) = settings.as_object() {
            for (section, value) in obj {
                if let Some(inner) = value.as_object() {
                    for (key, val) in inner {
                        store.insert(format!("{section}.{key}"), val.clone());
                    }
                } else {
                    store.insert(section.clone(), value.clone());
                }
            }
        }

        assert!(cfg_bool(&store, "inlayHints.enabled", false));
        assert!(!cfg_bool(&store, "inlayHints.typeHints", true));
        assert_eq!(cfg_string(&store, "topLevel", ""), "value");
    }
}
