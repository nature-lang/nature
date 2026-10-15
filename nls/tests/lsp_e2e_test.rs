//! End-to-end tests: they start the nls binary and talk to it over stdio the way an editor does,
//! with JSON-RPC messages framed by Content-Length headers.

use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{channel, Receiver};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tower_lsp::lsp_types::Url;

const SOURCE: &str = r#"import io

errdef TIMEOUT 'request timed out'
errdef CLOSED

fn read(int mode):int! {
    if mode == 1 {
        throw TIMEOUT
    }
    return 42
}

fn main():void! {
    var n = read(1) catch e {
        println(e.msg())
        -1
    }
    var c = read(2) catch e {
        match e {
            CLOSED -> -2
            _ -> -3
        }
    }
    println(n, c, io.EOF.msg())
}
"#;

struct Client {
    child: Child,
    stdin: ChildStdin,
    messages: Receiver<Value>,
    notifications: Vec<Value>,
    next_id: i64,
}

impl Client {
    // Starts nls on the std of this checkout and initializes it with root as the workspace folder.
    fn start(root: &Path) -> Client {
        let nature_root = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
        let mut child = Command::new(env!("CARGO_BIN_EXE_nls"))
            .env("NATURE_ROOT", nature_root)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let stdin = child.stdin.take().unwrap();
        let mut stdout = BufReader::new(child.stdout.take().unwrap());

        let (sender, messages) = channel();
        std::thread::spawn(move || loop {
            let mut length = 0;
            loop {
                let mut line = String::new();
                if stdout.read_line(&mut line).unwrap_or(0) == 0 {
                    return;
                }
                let line = line.trim_end();
                if line.is_empty() {
                    break;
                }
                if let Some(value) = line.strip_prefix("Content-Length: ") {
                    length = value.parse().unwrap();
                }
            }
            let mut body = vec![0; length];
            if stdout.read_exact(&mut body).is_err() || sender.send(serde_json::from_slice(&body).unwrap()).is_err() {
                return;
            }
        });

        let mut client = Client {
            child,
            stdin,
            messages,
            notifications: Vec::new(),
            next_id: 0,
        };
        let root_uri = Url::from_file_path(root).unwrap().to_string();
        client.request(
            "initialize",
            json!({
                "processId": null,
                "rootUri": root_uri,
                "capabilities": {},
                "workspaceFolders": [{"uri": root_uri, "name": "e2e"}],
            }),
        );
        client.notify("initialized", json!({}));
        client
    }

    fn send(&mut self, message: Value) {
        let body = message.to_string();
        write!(self.stdin, "Content-Length: {}\r\n\r\n{}", body.len(), body).unwrap();
        self.stdin.flush().unwrap();
    }

    fn notify(&mut self, method: &str, params: Value) {
        self.send(json!({"jsonrpc": "2.0", "method": method, "params": params}));
    }

    // The next response or notification from nls. A request from nls, such as workspace/configuration,
    // gets an empty answer here.
    fn next(&mut self) -> Value {
        loop {
            let message = self.messages.recv_timeout(Duration::from_secs(60)).expect("nls did not answer in 60s");
            if message.get("method").is_some() && message.get("id").is_some() {
                let result = if message["method"] == "workspace/configuration" { json!([null]) } else { Value::Null };
                self.send(json!({"jsonrpc": "2.0", "id": message["id"], "result": result}));
                continue;
            }
            return message;
        }
    }

    fn request(&mut self, method: &str, params: Value) -> Value {
        self.next_id += 1;
        let id = self.next_id;
        self.send(json!({"jsonrpc": "2.0", "id": id, "method": method, "params": params}));
        loop {
            let message = self.next();
            if message.get("id") == Some(&json!(id)) {
                return message["result"].clone();
            }
            self.notifications.push(message);
        }
    }

    // The next diagnostics nls publishes for uri, errors only.
    fn errors(&mut self, uri: &str) -> Vec<Value> {
        loop {
            let found = self
                .notifications
                .iter()
                .position(|n| n["method"] == "textDocument/publishDiagnostics" && n["params"]["uri"] == uri);
            if let Some(i) = found {
                let notification = self.notifications.remove(i);
                let diagnostics = notification["params"]["diagnostics"].as_array().unwrap().clone();
                return diagnostics.into_iter().filter(|d| d["severity"] == 1).collect();
            }
            let message = self.next();
            self.notifications.push(message);
        }
    }

    fn open(&mut self, uri: &str, text: &str) {
        self.notify(
            "textDocument/didOpen",
            json!({"textDocument": {"uri": uri, "languageId": "nature", "version": 1, "text": text}}),
        );
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        let _ = self.child.kill();
    }
}

// A project directory with main.n holding text, and the uri of main.n.
fn project(name: &str, text: &str) -> (PathBuf, String) {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let root = std::env::temp_dir().join(format!("nls_e2e_{}_{}", name, nanos));
    std::fs::create_dir_all(&root).unwrap();
    let file = root.join("main.n");
    std::fs::write(&file, text).unwrap();
    (root, Url::from_file_path(&file).unwrap().to_string())
}

// The position of the nth occurrence of needle in text.
fn position(text: &str, needle: &str, nth: usize) -> Value {
    let offset = text.match_indices(needle).nth(nth).unwrap().0;
    let line = text[..offset].matches('\n').count();
    let character = offset - text[..offset].rfind('\n').map_or(0, |i| i + 1);
    json!({"line": line, "character": character})
}

#[test]
fn e2e_errdef_diagnostics() {
    let (root, uri) = project("diagnostics", SOURCE);
    let mut client = Client::start(&root);

    client.open(&uri, SOURCE);
    let errors = client.errors(&uri);
    assert!(errors.is_empty(), "expected no errors, actual: {:?}", errors);

    // an integer is not an errset
    let changed = SOURCE.replace("throw TIMEOUT", "throw 5");
    client.notify(
        "textDocument/didChange",
        json!({"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": changed}]}),
    );
    let errors = client.errors(&uri);
    assert_eq!(errors.len(), 1, "expected one error, actual: {:?}", errors);
    assert!(
        errors[0]["message"].as_str().unwrap().contains("expect 'errset_t(u32)', actual 'i64'"),
        "actual: {:?}",
        errors
    );
    assert_eq!(errors[0]["range"]["start"], position(&changed, "5", 0));
}

#[test]
fn e2e_errset_hover_and_definition() {
    let (root, uri) = project("hover", SOURCE);
    let mut client = Client::start(&root);
    client.open(&uri, SOURCE);
    client.errors(&uri);

    let at = json!({"textDocument": {"uri": uri}, "position": position(SOURCE, "TIMEOUT", 1)});

    let hover = client.request("textDocument/hover", at.clone());
    let value = hover["contents"]["value"].as_str().unwrap();
    assert!(value.contains("errdef TIMEOUT 'request timed out'"), "actual: {}", value);

    let definition = client.request("textDocument/definition", at);
    let location = if definition.is_array() { definition[0].clone() } else { definition };
    assert_eq!(location["uri"], uri);
    assert_eq!(location["range"]["start"], position(SOURCE, "TIMEOUT", 0));
}

#[test]
fn e2e_module_errset_completion() {
    let (root, uri) = project("completion", SOURCE);
    let mut client = Client::start(&root);
    client.open(&uri, SOURCE);
    client.errors(&uri);

    let mut at = position(SOURCE, "io.EOF", 0);
    at["character"] = json!(at["character"].as_u64().unwrap() + "io.".len() as u64);
    let completion = client.request("textDocument/completion", json!({"textDocument": {"uri": uri}, "position": at}));
    let items = completion["items"].as_array().unwrap();
    let eof = items.iter().find(|item| item["label"] == "EOF").expect("io.EOF is not completed");
    assert_eq!(eof["kind"], 21, "EOF should complete as a constant: {}", eof);
}
