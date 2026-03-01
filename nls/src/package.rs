use crate::analyzer::common::{AnalyzerError, PackageConfig};

/**
 * 解析 toml 解析正确返回 package config, 如果解析错误则返回 AnalyzerError 错误信息
 */
pub fn parse_package(path: &str) -> Result<PackageConfig, AnalyzerError> {
    let content = std::fs::read_to_string(path).map_err(|e| AnalyzerError {
        start: 0,
        end: 0,
        message: e.to_string(),
        is_warning: false,
            })?;

    match toml::from_str(&content) {
        Ok(package) => {
            let package_config = PackageConfig {
                path: path.to_string(),
                package_data: package,
            };

            Ok(package_config)
        }
        Err(e) => {
            let span = e.span().unwrap_or(0..1);
            // let start_position = offset_to_position(span.start, &rope).unwrap_or_default();
            // let end_position = offset_to_position(span.end, &rope).unwrap_or_default();

            // let diagnostic = Diagnostic::new_simple(Range::new(start_position, end_position), e.message().to_string());
            // self.client
            // .publish_diagnostics(Url::parse(&format!("file://{}", path)).unwrap(), vec![diagnostic], None)
            // .await;

            Err(AnalyzerError {
                start: span.start,
                end: span.end,
                message: e.message().to_string(),
                is_warning: false,
                            })
        }
    }
}
