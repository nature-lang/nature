//! Parse `package.toml` into the analyzer's [`PackageConfig`].

use crate::analyzer::common::{AnalyzerError, PackageConfig};

/// Read and parse a `package.toml` file.
///
/// Returns `Ok(PackageConfig)` on success, or an [`AnalyzerError`] with span
/// information pointing at the TOML parse error location.
pub fn parse_package(path: &str) -> Result<PackageConfig, AnalyzerError> {
    let content = std::fs::read_to_string(path).map_err(|e| AnalyzerError {
        start: 0,
        end: 0,
        message: e.to_string(),
        is_warning: false,
    })?;

    match toml::from_str(&content) {
        Ok(package_data) => Ok(PackageConfig {
            path: path.to_string(),
            package_data,
        }),
        Err(e) => {
            let span = e.span().unwrap_or(0..1);
            Err(AnalyzerError {
                start: span.start,
                end: span.end,
                message: e.message().to_string(),
                is_warning: false,
            })
        }
    }
}
