/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */

import {
    languages,
    workspace,
    EventEmitter,
    ExtensionContext,
    window,
    InlayHintsProvider,
    TextDocument,
    CancellationToken,
    Range,
    InlayHint,
    TextDocumentChangeEvent,
    ProviderResult,
    commands,
    WorkspaceEdit,
    TextEdit,
    Selection,
    Uri,
    FileType,
} from "vscode";

import {
    Disposable,
    Executable,
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    Trace,
    TransportKind
} from "vscode-languageclient/node";

let client: LanguageClient;
// type a = Parameters<>;

// 添加这个辅助函数来检查文件是否存在
async function exists(uri: Uri): Promise<boolean> {
    try {
        await workspace.fs.stat(uri);
        return true;
    } catch {
        return false;
    }
}

export async function activate(context: ExtensionContext) {
    const traceOutputChannel = window.createOutputChannel("Nature Language Server");
    traceOutputChannel.show(); // 强制显示输出通道

    // 按优先级检查服务器路径
    let serverPath: string;
    const defaultPath = "/usr/local/nature/bin/nls";

    if (process.env.SERVER_PATH) {
        // 1. 首先检查环境变量
        serverPath = process.env.SERVER_PATH;
        traceOutputChannel.appendLine(`[LSP] Using server from SERVER_PATH: ${serverPath}`);
    } else if (await exists(Uri.file(defaultPath))) {
        // 2. 检查默认安装路径
        serverPath = defaultPath;
        traceOutputChannel.appendLine(`[LSP] Using server from default path: ${serverPath}`);
    } else {
        // 3. 最后使用 PATH 中的 nls
        serverPath = 'nls';
        traceOutputChannel.appendLine(`[LSP] Using 'nls' from PATH`);
    }

    const command = serverPath;
    traceOutputChannel.appendLine(`[LSP] Version: 0.1.2,  Final server command: ${command}`);

    const run: Executable = {
        command,
        options: {
            env: {
                ...process.env,
                // eslint-disable-next-line @typescript-eslint/naming-convention
                RUST_LOG: "debug",
                RUST_BACKTRACE: "1",
            },
        },
    };

    const serverOptions: ServerOptions = {
        run,
        debug: run
    };
    // If the extension is launched in debug mode then the debug server options are used
    // Otherwise the run options are used
    // Options to control the language client
    let clientOptions: LanguageClientOptions = {
        // Register the server for plain text documents
        documentSelector: [{ scheme: "file", language: "n" }],
        synchronize: {
            // Notify the server about file changes to '.clientrc files contained in the workspace
            fileEvents: workspace.createFileSystemWatcher("**/package.toml"),
        },
        traceOutputChannel,
        outputChannel: traceOutputChannel,
    };

    try {
        // Create the language client and start the client.
        client = new LanguageClient("nature-language-server", "Nature Language Server", serverOptions, clientOptions);
        // activateInlayHints(context);
         // 启用详细日志
        client.setTrace(Trace.Verbose);

        await client.start();
        traceOutputChannel.appendLine('[LSP] Language server started successfully');
    } catch (error) {
        traceOutputChannel.appendLine(`[LSP] Server start failed: ${error}`);
        if (error instanceof Error) {
            traceOutputChannel.appendLine(`Stack: ${error.stack}`);  // 添加堆栈信息
        }
        throw error;
    }
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

export function activateInlayHints(ctx: ExtensionContext) {
    const maybeUpdater = {
        hintsProvider: null as Disposable | null,
        updateHintsEventEmitter: new EventEmitter<void>(),

        async onConfigChange() {
            this.dispose();

            const event = this.updateHintsEventEmitter.event;
        },

        onDidChangeTextDocument({ contentChanges, document }: TextDocumentChangeEvent) {
            // debugger
            // this.updateHintsEventEmitter.fire();
        },

        dispose() {
            this.hintsProvider?.dispose();
            this.hintsProvider = null;
            this.updateHintsEventEmitter.dispose();
        },
    };

    workspace.onDidChangeConfiguration(maybeUpdater.onConfigChange, maybeUpdater, ctx.subscriptions);
    workspace.onDidChangeTextDocument(maybeUpdater.onDidChangeTextDocument, maybeUpdater, ctx.subscriptions);

    maybeUpdater.onConfigChange().catch(console.error);
}
