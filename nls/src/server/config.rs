//! Configuration keys, accessors, and `apply_config` logic.

use dashmap::DashMap;
use log::debug;

use super::Backend;

/// Debounce delay for `did_change` analysis (milliseconds).
pub const DEBOUNCE_MS: u64 = 300;

// Configuration key constants -------------------------------------------------

pub const CFG_INLAY_HINTS_ENABLED: &str = "inlayHints.enabled";
pub const CFG_INLAY_TYPE_HINTS: &str = "inlayHints.typeHints";
pub const CFG_INLAY_PARAM_HINTS: &str = "inlayHints.parameterHints";
pub const CFG_DEBOUNCE_MS: &str = "analysis.debounceMs";

// Typed accessors -------------------------------------------------------------

/// Read a bool config value, returning `default` when absent or the wrong type.
pub fn cfg_bool(config: &DashMap<String, serde_json::Value>, key: &str, default: bool) -> bool {
    config.get(key).and_then(|v| v.as_bool()).unwrap_or(default)
}

/// Read a u64 config value, returning `default` when absent or the wrong type.
pub fn cfg_u64(config: &DashMap<String, serde_json::Value>, key: &str, default: u64) -> u64 {
    config.get(key).and_then(|v| v.as_u64()).unwrap_or(default)
}

// Backend impl ----------------------------------------------------------------

impl Backend {
    /// Flatten a JSON settings object (e.g. `{ "inlayHints": { "enabled": true } }`)
    /// into dot-separated config keys (`inlayHints.enabled`) and store them.
    pub(crate) fn apply_config(&self, settings: &serde_json::Value) {
        if let Some(obj) = settings.as_object() {
            for (section, value) in obj {
                if let Some(inner) = value.as_object() {
                    for (key, val) in inner {
                        let config_key = format!("{}.{}", section, key);
                        debug!("config: {} = {}", config_key, val);
                        self.config.insert(config_key, val.clone());
                    }
                } else {
                    debug!("config: {} = {}", section, value);
                    self.config.insert(section.clone(), value.clone());
                }
            }
        }
    }

    /// Get the configured debounce delay in milliseconds.
    pub(crate) fn debounce_delay(&self) -> u64 {
        cfg_u64(&self.config, CFG_DEBOUNCE_MS, DEBOUNCE_MS)
    }
}
