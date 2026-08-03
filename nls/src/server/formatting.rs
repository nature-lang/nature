use log::debug;
use tower_lsp::lsp_types::{DocumentFormattingParams, Position, Range, TextEdit, Url};

use crate::formatter;

impl super::Backend {
    pub(crate) async fn handle_formatting(
        &self,
        params: DocumentFormattingParams,
    ) -> Option<Vec<TextEdit>> {
        let uri = &params.text_document.uri;
        let source = self.document_text(uri)?;

        match formatter::format_source(&source) {
            Ok(formatted) if formatted != source => Some(vec![TextEdit {
                range: full_document_range(&source),
                new_text: formatted,
            }]),
            Ok(_) => None,
            Err(err) => {
                debug!("formatter error for {}: {}", uri, err);
                None
            }
        }
    }

    fn document_text(&self, uri: &Url) -> Option<String> {
        if let Some(doc) = self.documents.get(uri) {
            return Some(doc.text());
        }

        let file_path = uri.to_file_path().ok()?;
        std::fs::read_to_string(&file_path).ok()
    }
}

fn full_document_range(source: &str) -> Range {
    let mut last_line = 0u32;
    let mut last_col = 0u32;

    for line in source.lines() {
        last_col = line.chars().count() as u32;
        last_line += 1;
    }

    if last_line > 0 {
        last_line -= 1;
    }

    Range::new(Position::new(0, 0), Position::new(last_line, last_col))
}
