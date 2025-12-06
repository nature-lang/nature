use dashmap::DashMap;
use log::debug;
use nls::analyzer::completion::{extract_prefix_at_position, CompletionItemKind, CompletionProvider};
use nls::analyzer::lexer::{TokenType, LEGEND_TYPE};
use nls::analyzer::module_unique_ident;
use nls::package::parse_package;
use nls::project::Project;
use nls::utils::offset_to_position;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tower_lsp::jsonrpc::Result;
use tower_lsp::lsp_types::notification::Notification;
use tower_lsp::lsp_types::*;
use tower_lsp::{Client, LanguageServer, LspService, Server};

#[derive(Debug)]
struct Backend {
    client: Client,
    // document_map: DashMap<String, Rope>,
    projects: DashMap<String, Project>, // key 是工作区 URI，value 是对应的项目
}

// backend 除了实现自身的方法，还实现了 LanguageServer trait 的方法
#[tower_lsp::async_trait]
impl LanguageServer for Backend {
    async fn initialize(&self, params: InitializeParams) -> Result<InitializeResult> {
        // 获取工作区根目录
        if let Some(workspace_folders) = params.workspace_folders {
            for folder in workspace_folders {
                // folder.uri 是工作区根目录的 URI
                let project_root = folder
                    .uri
                    .to_file_path()
                    .expect("Failed to convert URI to file path")
                    .to_string_lossy()
                    .to_string();
                let project = Project::new(project_root.clone()).await;
                project.backend_handle_queue();
                debug!("project new success root: {}", project_root);

                // 多工作区处理
                self.projects.insert(project_root, project);
            }
        }

        Ok(InitializeResult {
            server_info: None,
            offset_encoding: None,
            capabilities: ServerCapabilities {
                //  开启内联提示
                inlay_hint_provider: Some(OneOf::Left(true)),
                // 文档同步配置
                text_document_sync: Some(TextDocumentSyncCapability::Options(TextDocumentSyncOptions {
                    open_close: Some(true),
                    change: Some(TextDocumentSyncKind::FULL),
                    save: Some(TextDocumentSyncSaveOptions::SaveOptions(SaveOptions { include_text: Some(true) })),
                    ..Default::default()
                })),
                // 代码补全配置
                completion_provider: Some(CompletionOptions {
                    resolve_provider: Some(false),
                    trigger_characters: Some(vec![".".to_string()]),
                    work_done_progress_options: Default::default(),
                    all_commit_characters: None,
                    completion_item: None,
                }),
                execute_command_provider: Some(ExecuteCommandOptions {
                    commands: vec!["dummy.do_something".to_string()],
                    work_done_progress_options: Default::default(),
                }),

                // 工作区配置
                workspace: Some(WorkspaceServerCapabilities {
                    workspace_folders: Some(WorkspaceFoldersServerCapabilities {
                        supported: Some(true),
                        change_notifications: Some(OneOf::Left(true)),
                    }),
                    file_operations: None,
                }),
                // 语义标记配置
                semantic_tokens_provider: Some(SemanticTokensServerCapabilities::SemanticTokensRegistrationOptions(
                    SemanticTokensRegistrationOptions {
                        text_document_registration_options: {
                            TextDocumentRegistrationOptions {
                                document_selector: Some(vec![DocumentFilter {
                                    language: Some("n".to_string()),
                                    scheme: Some("file".to_string()),
                                    pattern: None,
                                }]),
                            }
                        },
                        semantic_tokens_options: SemanticTokensOptions {
                            work_done_progress_options: WorkDoneProgressOptions::default(),
                            legend: SemanticTokensLegend {
                                // // LEGEND_TYPE 通常定义在 semantic_token.rs 中，包含所有支持的标记类型
                                token_types: LEGEND_TYPE.into(), // 支持的标记类型, 如函数、变量、字符串等
                                token_modifiers: vec![],         // 支持的标记修饰符, 例如 readonly, static 等
                            },
                            range: Some(true), // 范围增量更新语义
                            full: Some(SemanticTokensFullOptions::Bool(true)),
                        },
                        static_registration_options: StaticRegistrationOptions::default(),
                    },
                )),
                // definition: Some(GotoCapability::default()),
                definition_provider: Some(OneOf::Left(true)),
                references_provider: Some(OneOf::Left(true)),
                rename_provider: Some(OneOf::Left(true)),
                ..ServerCapabilities::default()
            },
        })
    }
    async fn initialized(&self, _: InitializedParams) {
        debug!("initialized");
    }

    async fn shutdown(&self) -> Result<()> {
        Ok(())
    }

    async fn did_open(&self, params: DidOpenTextDocumentParams) {
        debug!("file opened {}", params.text_document.uri);

        // 从 uri 所在目录开始逐层向上读取，判断是否存在 package.toml, 如果存在 package.toml, 那 package.toml 所在目录就是一个新的 project
        let file_path = params.text_document.uri.path();
        let file_dir = std::path::Path::new(file_path).parent();

        if let Some(dir) = file_dir {
            // Check if need to create new project
            let (project_root, log_message) = if let Some(package_dir) = self.find_package_dir(dir) {
                (package_dir.to_string_lossy().to_string(), "Found new project at")
            } else {
                // If no package_dir found, use current file's directory as project root
                (dir.to_string_lossy().to_string(), "Creating new project using file directory as root:")
            };

            // Check if this project already exists and create if needed
            if !self.projects.contains_key(&project_root) {
                debug!("{} {}", log_message, project_root);
                let project = Project::new(project_root.clone()).await;
                project.backend_handle_queue();
                debug!("project new success root: {}", project_root);
                self.projects.insert(project_root, project);
            }
        }

        self.on_change(TextDocumentItem {
            uri: params.text_document.uri,
            text: &params.text_document.text,
            version: Some(params.text_document.version),
        })
        .await
    }

    async fn did_change(&self, params: DidChangeTextDocumentParams) {
        self.on_change(TextDocumentItem {
            text: &params.content_changes[0].text,
            uri: params.text_document.uri,
            version: Some(params.text_document.version),
        })
        .await
    }

    async fn did_save(&self, params: DidSaveTextDocumentParams) {
        if let Some(text) = params.text {
            let item = TextDocumentItem {
                uri: params.text_document.uri,
                text: &text,
                version: None,
            };
            self.on_change(item).await;
            _ = self.client.semantic_tokens_refresh().await;
        }
        debug!("file saved!");
    }
    async fn did_close(&self, _: DidCloseTextDocumentParams) {
        debug!("file closed!");
    }

    // did open 中已经对 document 进行了处理，所以这里只需要从 semantic 中获取信息
    async fn goto_definition(&self, _params: GotoDefinitionParams) -> Result<Option<GotoDefinitionResponse>> {
        let definition = || -> Option<GotoDefinitionResponse> {
            // uri 标识当前文档的唯一标识
            // let uri = params.text_document_position_params.text_document.uri;

            // self 表示当前 backend, 通过 uri 标识快速获取对应的 semantic
            // let semantic = self.semantic_map.get(uri.as_str())?;

            // let rope = self.document_map.get(uri.as_str())?;

            // // 获取当前光标位置
            // let position = params.text_document_position_params.position;

            // // 将光标位置转换为偏移量
            // let offset = position_to_offset(position, &rope)?;

            // // 获取当前光标位置的符号
            // let interval = semantic.ident_range.find(offset, offset + 1).next()?;
            // let interval_val = interval.val;

            // let range = match interval_val {
            //     // 回到定义点
            //     IdentType::Binding(symbol_id) => {
            //         let span = &semantic.table.symbol_id_to_span[symbol_id];
            //         Some(span.clone())
            //     }

            //     // 通过引用获取符号定义的位置
            //     IdentType::Reference(reference_id) => {
            //         let reference = semantic.table.reference_id_to_reference.get(reference_id)?;
            //         let symbol_id = reference.symbol_id?;
            //         let symbol_range = semantic.table.symbol_id_to_span.get(symbol_id)?;
            //         Some(symbol_range.clone())
            //     }
            // };

            // // 将源代码转换为 lsp 输出需要的位置格式
            // range.and_then(|range| {
            //     let start_position = offset_to_position(range.start, &rope)?;
            //     let end_position = offset_to_position(range.end, &rope)?;
            //     Some(GotoDefinitionResponse::Scalar(Location::new(uri, Range::new(start_position, end_position))))
            // })
            None
        }();
        Ok(definition)
    }

    async fn references(&self, params: ReferenceParams) -> Result<Option<Vec<Location>>> {
        let reference_list = || -> Option<Vec<Location>> {
            let _uri = params.text_document_position.text_document.uri;
            // let semantic = self.semantic_map.get(uri.as_str())?;
            // let rope = self.document_map.get(uri.as_str())?;
            // let position = params.text_document_position.position;
            // let offset = position_to_offset(position, &rope)?;
            // let reference_span_list = get_references(&semantic, offset, offset + 1, false)?;

            // let ret = reference_span_list
            //     .into_iter()
            //     .filter_map(|range| {
            //         let start_position = offset_to_position(range.start, &rope)?;
            //         let end_position = offset_to_position(range.end, &rope)?;

            //         let range = Range::new(start_position, end_position);

            //         Some(Location::new(uri.clone(), range))
            //     })
            //     .collect::<Vec<_>>();
            // Some(ret)
            None
        }();
        Ok(reference_list)
    }

    async fn semantic_tokens_full(&self, params: SemanticTokensParams) -> Result<Option<SemanticTokensResult>> {
        let file_path = params.text_document.uri.path();
        debug!("semantic_token_full");

        // semantic_tokens 是一个闭包, 返回 vscode 要求的 SemanticToken 结构
        let semantic_tokens = || -> Option<Vec<SemanticToken>> {
            let Some(project) = self.get_file_project(&file_path) else { unreachable!() };

            // 直接从 module_handled 中获取
            let module_index = {
                let module_handled = project.module_handled.lock().unwrap();
                module_handled.get(file_path)?.clone()
            };

            let mut module_db = project.module_db.lock().unwrap();
            let m = &mut module_db[module_index];

            // 获取 semantic_token_map 中的 token
            let im_complete_tokens = m.sem_token_db.clone();
            let rope = m.rope.clone();

            // im_complete_tokens.sort_by(|a, b| a.start.cmp(&b.start));

            let mut pre_line = 0;
            let mut pre_line_start = 0;
            let mut pre_start = 0;
            let semantic_tokens: Vec<SemanticToken> = im_complete_tokens
                .iter()
                .filter_map(|token| {
                    let line = rope.try_char_to_line(token.start).ok()? as u32;
                    let end_line = rope.try_char_to_line(token.end).ok()? as u32;
                    let line_first = rope.try_line_to_char(line as usize).ok()? as u32;
                    let line_start = token.start as u32 - line_first;

                    // dbg!(
                    //     "--------------",
                    //     token.clone(),
                    //     line,
                    //     end_line,
                    //     pre_line,
                    //     pre_line_start,
                    //     line_start,
                    //     line_start < pre_line_start,
                    //     "-------------"
                    // );

                    if token.start < pre_start && token.token_type == TokenType::StringLiteral {
                        // 多行字符串跳过
                        return None;
                    }

                    let delta_line = line - pre_line;
                    let delta_start = if delta_line == 0 { line_start - pre_line_start } else { line_start };

                    let ret = Some(SemanticToken {
                        delta_line,
                        delta_start,
                        length: token.length as u32,
                        token_type: token.semantic_token_type.clone() as u32,
                        token_modifiers_bitset: 0,
                    });
                    pre_line = line;
                    pre_line_start = line_start;
                    pre_start = token.start;
                    ret
                })
                .collect::<Vec<_>>();
            Some(semantic_tokens)
        }();
        if let Some(semantic_token) = semantic_tokens {
            return Ok(Some(SemanticTokensResult::Tokens(SemanticTokens {
                result_id: None,
                data: semantic_token,
            })));
        }
        Ok(None)
    }

    async fn semantic_tokens_range(&self, params: SemanticTokensRangeParams) -> Result<Option<SemanticTokensRangeResult>> {
        let file_path = params.text_document.uri.path();
        let semantic_tokens = || -> Option<Vec<SemanticToken>> {
            let Some(project) = self.get_file_project(&file_path) else { unreachable!() };

            // 直接从 module_handled 中获取
            let module_index = {
                let module_handled = project.module_handled.lock().unwrap();
                module_handled.get(file_path)?.clone()
            };

            let mut module_db = project.module_db.lock().unwrap();
            let m = &mut module_db[module_index];

            // 获取 semantic_token_map 中的 token
            let im_complete_tokens = m.sem_token_db.clone();
            let rope = m.rope.clone();

            let mut pre_line = 0;
            let mut pre_start = 0;
            let semantic_tokens = im_complete_tokens
                .iter()
                .filter_map(|token| {
                    let line = rope.try_byte_to_line(token.start).ok()? as u32;
                    let first = rope.try_line_to_char(line as usize).ok()? as u32;
                    let start = rope.try_byte_to_char(token.start).ok()? as u32 - first;
                    let ret = Some(SemanticToken {
                        delta_line: line - pre_line,
                        delta_start: if start >= pre_start { start - pre_start } else { start },
                        length: token.length as u32,
                        token_type: token.token_type.clone() as u32,
                        token_modifiers_bitset: 0,
                    });
                    pre_line = line;
                    pre_start = start;
                    ret
                })
                .collect::<Vec<_>>();
            Some(semantic_tokens)
        }();
        Ok(semantic_tokens.map(|data| SemanticTokensRangeResult::Tokens(SemanticTokens { result_id: None, data })))
    }

    async fn inlay_hint(&self, _params: tower_lsp::lsp_types::InlayHintParams) -> Result<Option<Vec<InlayHint>>> {
        // dbg!("inlay hint");
        // let uri = &params.text_document.uri;
        // let mut hashmap = HashMap::new();
        // if let Some(ast) = self.ast_map.get(uri.as_str()) {
        //     ast.iter().for_each(|(func, _)| {
        //         type_inference(&func.body, &mut hashmap);
        //     });
        // }

        // let document = match self.document_map.get(uri.as_str()) {
        //     Some(rope) => rope,
        //     None => return Ok(None),
        // };
        // let inlay_hint_list = hashmap
        //     .into_iter()
        //     .map(|(k, v)| {
        //         (
        //             k.start,
        //             k.end,
        //             match v {
        //                 nls::nrs_lang::Value::Null => "null".to_string(),
        //                 nls::nrs_lang::Value::Bool(_) => "bool".to_string(),
        //                 nls::nrs_lang::Value::Num(_) => "number".to_string(),
        //                 nls::nrs_lang::Value::Str(_) => "string".to_string(),
        //             },
        //         )
        //     })
        //     .filter_map(|item| {
        //         // let start_position = offset_to_position(item.0, document)?;
        //         let end_position = offset_to_position(item.1, &document)?;
        //         let inlay_hint = InlayHint {
        //             text_edits: None,
        //             tooltip: None,
        //             kind: Some(InlayHintKind::TYPE),
        //             padding_left: None,
        //             padding_right: None,
        //             data: None,
        //             position: end_position,
        //             label: InlayHintLabel::LabelParts(vec![InlayHintLabelPart {
        //                 value: item.2,
        //                 tooltip: None,
        //                 location: Some(Location {
        //                     uri: params.text_document.uri.clone(),
        //                     range: Range {
        //                         start: Position::new(0, 4),
        //                         end: Position::new(0, 10),
        //                     },
        //                 }),
        //                 command: None,
        //             }]),
        //         };
        //         Some(inlay_hint)
        //     })
        //     .collect::<Vec<_>>();

        // Ok(Some(inlay_hint_list))
        Ok(None)
    }

    async fn completion(&self, params: CompletionParams) -> Result<Option<CompletionResponse>> {
        dbg!("completion requested");

        let uri = params.text_document_position.text_document.uri;
        let position: Position = params.text_document_position.position;

        let completions = || -> Option<Vec<tower_lsp::lsp_types::CompletionItem>> {
            let file_path = uri.path();
            let project = self.get_file_project(&file_path)?;

            // 获取模块索引
            let module_index = {
                let module_handled = project.module_handled.lock().unwrap();
                module_handled.get(file_path)?.clone()
            };

            let mut module_db = project.module_db.lock().unwrap();
            let module = &mut module_db[module_index];

            // 将LSP位置转换为字节偏移
            let rope = &module.rope;
            let line_char = rope.try_line_to_char(position.line as usize).ok()?;
            let byte_offset = line_char + position.character as usize;

            // 获取当前位置的前缀
            let text = rope.to_string();
            let prefix = extract_prefix_at_position(&text, byte_offset);
            debug!("Extracted prefix: '{}', module_ident '{}', raw_text '{}'", prefix, module.ident.clone(), text);

            // Get symbol table and package config
            let mut symbol_table = project.symbol_table.lock().unwrap();
            let package_config = project.package_config.lock().unwrap().clone();
            // Create completion provider and get completion items
            let completion_items = CompletionProvider::new(
                &mut symbol_table,
                module,
                project.nature_root.clone(),
                project.root.clone(),
                package_config,
            )
            .get_completions(byte_offset, &prefix);

            // 转换为LSP格式
            let lsp_items: Vec<tower_lsp::lsp_types::CompletionItem> = completion_items
                .into_iter()
                .map(|item| {
                    let lsp_kind = match item.kind {
                        CompletionItemKind::Variable => tower_lsp::lsp_types::CompletionItemKind::VARIABLE,
                        CompletionItemKind::Parameter => tower_lsp::lsp_types::CompletionItemKind::VARIABLE,
                        CompletionItemKind::Function => tower_lsp::lsp_types::CompletionItemKind::FUNCTION,
                        CompletionItemKind::Constant => tower_lsp::lsp_types::CompletionItemKind::CONSTANT,
                        CompletionItemKind::Module => tower_lsp::lsp_types::CompletionItemKind::MODULE,
                        CompletionItemKind::Struct => tower_lsp::lsp_types::CompletionItemKind::STRUCT,
                    };

                    // Check if insert_text contains snippet syntax
                    let has_snippet = item.insert_text.contains("$0");

                    // Convert additional_text_edits to LSP format
                    let additional_edits = if !item.additional_text_edits.is_empty() {
                        Some(
                            item.additional_text_edits
                                .into_iter()
                                .map(|edit| tower_lsp::lsp_types::TextEdit {
                                    range: tower_lsp::lsp_types::Range {
                                        start: tower_lsp::lsp_types::Position {
                                            line: edit.line as u32,
                                            character: edit.character as u32,
                                        },
                                        end: tower_lsp::lsp_types::Position {
                                            line: edit.line as u32,
                                            character: edit.character as u32,
                                        },
                                    },
                                    new_text: edit.new_text,
                                })
                                .collect(),
                        )
                    } else {
                        None
                    };
                    
                    tower_lsp::lsp_types::CompletionItem {
                        label: item.label,
                        kind: Some(lsp_kind),
                        detail: item.detail,
                        documentation: item.documentation.map(|doc| tower_lsp::lsp_types::Documentation::String(doc)),
                        insert_text: Some(item.insert_text),
                        insert_text_format: if has_snippet { 
                            Some(tower_lsp::lsp_types::InsertTextFormat::SNIPPET) 
                        } else { 
                            Some(tower_lsp::lsp_types::InsertTextFormat::PLAIN_TEXT) 
                        },
                        sort_text: item.sort_text,
                        additional_text_edits: additional_edits,
                        ..Default::default()
                    }
                })
                .collect();

            debug!("Returning {} completion items", lsp_items.len());
            Some(lsp_items)
        }();

        Ok(completions.map(CompletionResponse::Array))
    }

    async fn rename(&self, _params: RenameParams) -> Result<Option<WorkspaceEdit>> {
        let workspace_edit = || -> Option<WorkspaceEdit> {
            // let uri = params.text_document_position.text_document.uri;
            // let semantic = self.semantic_map.get(uri.as_str())?;
            // let rope = self.document_map.get(uri.as_str())?;
            // let position = params.text_document_position.position;
            // let offset = position_to_offset(position, &rope)?;
            // let reference_list = get_references(&semantic, offset, offset + 1, true)?;

            // let new_name = params.new_name;
            // (!reference_list.is_empty()).then_some(()).map(|_| {
            //     let edit_list = reference_list
            //         .into_iter()
            //         .filter_map(|range| {
            //             let start_position = offset_to_position(range.start, &rope)?;
            //             let end_position = offset_to_position(range.end, &rope)?;
            //             Some(TextEdit::new(Range::new(start_position, end_position), new_name.clone()))
            //         })
            //         .collect::<Vec<_>>();
            //     let mut map = HashMap::new();
            //     map.insert(uri, edit_list);
            //     WorkspaceEdit::new(map)
            // })
            None
        }();
        Ok(workspace_edit)
    }

    async fn did_change_configuration(&self, _: DidChangeConfigurationParams) {
        debug!("configuration changed!");
    }

    async fn did_change_workspace_folders(&self, _: DidChangeWorkspaceFoldersParams) {
        debug!("workspace folders changed!");
    }

    async fn did_change_watched_files(&self, params: DidChangeWatchedFilesParams) {
        debug!("package.toml files have changed!");

        for param in params.changes {
            let file_path = param.uri.path();
            let Some(project) = self.get_file_project(&file_path) else { unreachable!() };

            if !file_path.ends_with("package.toml") {
                panic!("unexpected package file '{}'", file_path);
            }

            match parse_package(&file_path) {
                Ok(package_conf) => {
                    {
                        let mut package_option = project.package_config.lock().unwrap();
                        *package_option = Some(package_conf);
                    }

                    self.client.publish_diagnostics(param.uri.clone(), vec![], None).await;
                }
                Err(e) => {
                    // read file content
                    if let Ok(content) = std::fs::read_to_string(&file_path) {
                        // 创建 rope 用于将偏移量转换为位置
                        let rope = ropey::Rope::from_str(&content);
                        let start_position = offset_to_position(e.start, &rope).unwrap_or(Position::new(0, 0));

                        let end_position = offset_to_position(e.end, &rope).unwrap_or(Position::new(0, 0));

                        let diagnostic = Diagnostic::new_simple(Range::new(start_position, end_position), format!("parser package.toml failed: {}", e.message));
                        self.client.publish_diagnostics(param.uri.clone(), vec![diagnostic], None).await;
                    }
                }
            }
        }

        debug!("package.toml updated");
    }

    async fn execute_command(&self, _: ExecuteCommandParams) -> Result<Option<Value>> {
        debug!("command executed!");

        match self.client.apply_edit(WorkspaceEdit::default()).await {
            Ok(res) if res.applied => self.client.log_message(MessageType::INFO, "applied").await,
            Ok(_) => self.client.log_message(MessageType::INFO, "rejected").await,
            Err(err) => self.client.log_message(MessageType::ERROR, err).await,
        }

        Ok(None)
    }
}
#[derive(Debug, Deserialize, Serialize)]
struct InlayHintParams {
    path: String,
}

#[allow(unused)]
enum CustomNotification {}
impl Notification for CustomNotification {
    type Params = InlayHintParams;
    const METHOD: &'static str = "custom/notification";
}
struct TextDocumentItem<'a> {
    uri: Url,
    text: &'a str, // 'a 是声明周期引用，表示 str 的生命周期与 TextDocumentItem 的生命周期一致, 而不是 } 后就结束
    version: Option<i32>,
}

impl Backend {
    // 添加一个辅助方法来根据文件 URI 找到对应的项目
    fn get_file_project(&self, file_path: &str) -> Option<Project> {
        // 遍历所有项目，找到最匹配（路径最长）的项目
        let mut best_match: Option<(usize, Project)> = None;

        for entry in self.projects.iter() {
            let workspace_uri = entry.key();

            // 检查文件是否属于这个项目
            if file_path.starts_with(workspace_uri) {
                // 如果文件属于这个项目，检查这是否是最佳匹配（路径最长）
                let uri_len = workspace_uri.len();

                if let Some((best_len, _)) = &best_match {
                    if uri_len > *best_len {
                        // 找到更长的匹配，更新最佳匹配
                        best_match = Some((uri_len, entry.value().clone()));
                    }
                } else {
                    // 第一个匹配
                    best_match = Some((uri_len, entry.value().clone()));
                }
            }
        }

        // 返回最佳匹配的项目
        best_match.map(|(_, project)| project)
    }

    fn find_package_dir(&self, start_dir: &std::path::Path) -> Option<std::path::PathBuf> {
        let mut current_dir = Some(start_dir.to_path_buf());

        while let Some(dir) = current_dir {
            let package_path = dir.join("package.toml");
            if package_path.exists() {
                return Some(dir);
            }
            current_dir = dir.parent().map(|p| p.to_path_buf());
        }

        None
    }

    async fn on_change<'a>(&self, params: TextDocumentItem<'a>) {
        debug!(
            r#"Text content:
        {}
        "#,
            params.text
        );

        let file_path = params.uri.path();
        let Some(mut project) = self.get_file_project(&file_path) else {
            unreachable!()
        };

        // package.toml specail handle
        if file_path.ends_with("package.toml") {
            panic!("unexpected package file '{}'", file_path);
        }

        let module_ident = module_unique_ident(&project.root, &file_path);
        debug!("will build, module ident: {}", module_ident);

        //  基于 project path 计算 moudle ident
        let module_index = project.build(&file_path, &module_ident, Some(params.text.to_string())).await;
        debug!("build success");

        let diagnostics = {
            let mut module_db = project.module_db.lock().unwrap();
            let m = &mut module_db[module_index];

            dbg!(&m.ident, &m.analyzer_errors);

            let mut seen_positions = std::collections::HashSet::new();

            m.analyzer_errors
                .clone()
                .into_iter()
                .filter(|error| error.end > 0) // 过滤掉 start = end = 0 的错误
                .filter(|error| {
                    // 仅保留第一个相同 start 和 end 的错误
                    let position_key = (error.start, error.end);
                    seen_positions.insert(position_key)
                })
                .filter_map(|error| {
                    let start_position = offset_to_position(error.start, &m.rope)?;
                    let end_position = offset_to_position(error.end, &m.rope)?;
                    Some(Diagnostic::new_simple(Range::new(start_position, end_position), error.message))
                })
                .collect::<Vec<_>>()

        }; // MutexGuard 在这里被释放

        // 现在可以安全地使用 await
        self.client.publish_diagnostics(params.uri.clone(), diagnostics, params.version).await;
    }
}

#[tokio::main]
async fn main() {
    env_logger::init();

    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();

    let (service, socket) = LspService::build(|client| Backend {
        client,
        // document_map: DashMap::new(),
        projects: DashMap::new(),
    })
    .finish();

    Server::new(stdin, stdout, socket).serve(service).await;
}
