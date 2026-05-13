use dashmap::DashMap;
use std::env;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use tower_lsp::{LspService, Server};

use nls::analyzer::common::AnalyzerError;
use nls::document::DocumentStore;
use nls::formatter;
use nls::server::Backend;

#[tokio::main]
async fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() >= 2 && args[1] == "fmt" {
        std::process::exit(handle_fmt(&args[2..]).await);
    }

    env_logger::init();

    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();

    let (service, socket) = LspService::new(|client| Backend {
        client,
        documents: DocumentStore::new(),
        projects: DashMap::new(),
        debounce_versions: DashMap::new(),
        config: DashMap::new(),
    });

    Server::new(stdin, stdout, socket).serve(service).await;
}

const FMT_USAGE: &str =
    "usage: nls fmt [-w|--write] [--check] [--diff|-d] [-l|--list] [-e|--errors] [path ...]";

#[derive(Debug, Default, Clone, PartialEq, Eq)]
struct FmtOptions {
    write_back: bool,
    check_only: bool,
    diff_only: bool,
    list_only: bool,
    report_all_errors: bool,
    inputs: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum FmtTarget {
    Stdin,
    Path(PathBuf),
}

async fn handle_fmt(args: &[String]) -> i32 {
    let options = match parse_fmt_args(args) {
        Ok(options) => options,
        Err(message) => {
            eprintln!("{}", FMT_USAGE);
            eprintln!("{}", message);
            return 1;
        }
    };

    let targets = match collect_fmt_targets(&options.inputs) {
        Ok(targets) => targets,
        Err(message) => {
            eprintln!("{}", message);
            return 1;
        }
    };

    if options.write_back && targets.iter().any(|target| matches!(target, FmtTarget::Stdin)) {
        eprintln!("{}", FMT_USAGE);
        eprintln!("cannot use --write with stdin");
        return 1;
    }

    let mut exit_code = 0;
    for target in targets {
        let display_name = target.display_name();
        let source = match read_target_source(&target) {
            Ok(source) => source,
            Err(err) => {
                eprintln!("failed to read {}: {}", display_name, err);
                exit_code = 1;
                continue;
            }
        };

        match formatter::format_source_with_errors(&source) {
            Ok(formatted) => {
                let changed = formatted != source;

                if options.list_only && changed {
                    println!("{}", display_name);
                }

                if options.diff_only && changed {
                    print!("{}", unified_diff(&display_name, &source, &formatted));
                    exit_code = 1;
                }

                if options.check_only && changed {
                    eprintln!("{} would be reformatted", display_name);
                    exit_code = 1;
                }

                if options.write_back && changed {
                    if let Err(err) = write_target_output(&target, &formatted) {
                        eprintln!("failed to write {}: {}", display_name, err);
                        exit_code = 1;
                    }
                }

                if !options.write_back && !options.check_only && !options.diff_only && !options.list_only {
                    print!("{}", formatted);
                }
            }
            Err(errors) => {
                report_formatter_errors(&display_name, &errors, options.report_all_errors);
                exit_code = 1;
            }
        }
    }

    exit_code
}

fn parse_fmt_args(args: &[String]) -> Result<FmtOptions, String> {
    let mut options = FmtOptions::default();

    for arg in args {
        match arg.as_str() {
            "-w" | "--write" => options.write_back = true,
            "--check" => options.check_only = true,
            "--diff" | "-d" => options.diff_only = true,
            "-l" | "--list" => options.list_only = true,
            "-e" | "--errors" => options.report_all_errors = true,
            _ => options.inputs.push(arg.clone()),
        }
    }

    if options.write_back && (options.check_only || options.diff_only) {
        return Err("cannot use --write with --check or --diff".to_string());
    }

    if options.inputs.is_empty() {
        options.inputs.push("-".to_string());
    }

    Ok(options)
}

fn collect_fmt_targets(inputs: &[String]) -> Result<Vec<FmtTarget>, String> {
    let mut targets = Vec::new();
    let mut seen_paths = std::collections::BTreeSet::new();
    let mut seen_stdin = false;

    for input in inputs {
        if input == "-" {
            if !seen_stdin {
                targets.push(FmtTarget::Stdin);
                seen_stdin = true;
            }
            continue;
        }

        let path = PathBuf::from(input);
        if !path.exists() {
            return Err(format!("path does not exist: {}", input));
        }

        collect_path_targets(&path, &mut targets, &mut seen_paths)
            .map_err(|err| format!("failed to walk {}: {}", input, err))?;
    }

    Ok(targets)
}

fn collect_path_targets(
    path: &Path,
    targets: &mut Vec<FmtTarget>,
    seen_paths: &mut std::collections::BTreeSet<PathBuf>,
) -> io::Result<()> {
    if path.is_file() {
        let path_buf = path.to_path_buf();
        if seen_paths.insert(path_buf.clone()) {
            targets.push(FmtTarget::Path(path_buf));
        }
        return Ok(());
    }

    if !path.is_dir() {
        return Ok(());
    }

    let mut entries = fs::read_dir(path)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|entry| entry.path());

    for entry in entries {
        let entry_path = entry.path();
        if entry_path.is_dir() {
            collect_path_targets(&entry_path, targets, seen_paths)?;
            continue;
        }

        if entry_path.extension().and_then(|ext| ext.to_str()) == Some("n")
            && seen_paths.insert(entry_path.clone())
        {
            targets.push(FmtTarget::Path(entry_path));
        }
    }

    Ok(())
}

fn read_target_source(target: &FmtTarget) -> io::Result<String> {
    match target {
        FmtTarget::Stdin => {
            let mut source = String::new();
            io::stdin().read_to_string(&mut source)?;
            Ok(source)
        }
        FmtTarget::Path(path) => fs::read_to_string(path),
    }
}

fn write_target_output(target: &FmtTarget, formatted: &str) -> io::Result<()> {
    match target {
        FmtTarget::Stdin => Err(io::Error::new(io::ErrorKind::InvalidInput, "stdin has no write target")),
        FmtTarget::Path(path) => fs::write(path, formatted),
    }
}

fn report_formatter_errors(display_name: &str, errors: &[AnalyzerError], report_all_errors: bool) {
    let limit = if report_all_errors { errors.len() } else { errors.len().min(1) };

    for error in errors.iter().take(limit) {
        eprintln!(
            "formatter error for {} [{}..{}]: {}",
            display_name, error.start, error.end, error.message
        );
    }
}

impl FmtTarget {
    fn display_name(&self) -> String {
        match self {
            FmtTarget::Stdin => "<stdin>".to_string(),
            FmtTarget::Path(path) => path.to_string_lossy().into_owned(),
        }
    }
}

fn unified_diff(file: &str, old: &str, new: &str) -> String {
    #[derive(Debug)]
    enum DiffOp {
        Equal(String),
        Remove(String),
        Add(String),
    }

    fn compute_diff(old: &[String], new: &[String]) -> Vec<DiffOp> {
        let n = old.len();
        let m = new.len();
        let mut dp = vec![vec![0; m + 1]; n + 1];

        for i in 0..n {
            for j in 0..m {
                if old[i] == new[j] {
                    dp[i + 1][j + 1] = dp[i][j] + 1;
                } else {
                    dp[i + 1][j + 1] = dp[i + 1][j].max(dp[i][j + 1]);
                }
            }
        }

        let mut ops = Vec::new();
        let mut i = n;
        let mut j = m;

        while i > 0 || j > 0 {
            if i > 0 && j > 0 && old[i - 1] == new[j - 1] {
                ops.push(DiffOp::Equal(old[i - 1].clone()));
                i -= 1;
                j -= 1;
            } else if j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j]) {
                ops.push(DiffOp::Add(new[j - 1].clone()));
                j -= 1;
            } else {
                ops.push(DiffOp::Remove(old[i - 1].clone()));
                i -= 1;
            }
        }

        ops.reverse();
        ops
    }

    let old_lines: Vec<String> = old.lines().map(str::to_string).collect();
    let new_lines: Vec<String> = new.lines().map(str::to_string).collect();
    let diff_ops = compute_diff(&old_lines, &new_lines);

    let mut result = String::new();
    use std::fmt::Write;
    writeln!(result, "diff --git a/{} b/{}", file, file).ok();
    writeln!(result, "--- a/{}", file).ok();
    writeln!(result, "+++ b/{}", file).ok();

    for op in diff_ops {
        match op {
            DiffOp::Equal(line) => {
                writeln!(result, " {}", line).ok();
            }
            DiffOp::Remove(line) => {
                writeln!(result, "-{}", line).ok();
            }
            DiffOp::Add(line) => {
                writeln!(result, "+{}", line).ok();
            }
        }
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn unified_diff_outputs_changed_lines() {
        let old = "fn add(a:int,b:int):int {\n    return a+b\n}\n";
        let new = "fn add(a: int, b: int): int {\n    return a + b\n}\n";
        let diff = unified_diff("/tmp/test.n", old, new);
        assert!(diff.contains("--- a//tmp/test.n"));
        assert!(diff.contains("+++ b//tmp/test.n"));
        assert!(diff.contains("-fn add(a:int,b:int):int {"));
        assert!(diff.contains("+fn add(a: int, b: int): int {"));
    }

    #[test]
    fn parse_fmt_args_defaults_to_stdin() {
        let options = parse_fmt_args(&[]).expect("parse succeeds");
        assert_eq!(options.inputs, vec!["-"]);
    }

    #[test]
    fn parse_fmt_args_supports_list_and_errors() {
        let args = vec!["-l".to_string(), "-e".to_string(), "src".to_string()];
        let options = parse_fmt_args(&args).expect("parse succeeds");
        assert!(options.list_only);
        assert!(options.report_all_errors);
        assert_eq!(options.inputs, vec!["src"]);
    }

    #[test]
    fn collect_fmt_targets_walks_directories_recursively() {
        let root = unique_test_dir("fmt_walk");
        let nested = root.join("nested");
        fs::create_dir_all(&nested).expect("create nested dir");
        fs::write(root.join("a.n"), "fn main(){}").expect("write a.n");
        fs::write(root.join("ignore.txt"), "skip").expect("write ignore.txt");
        fs::write(nested.join("b.n"), "fn main(){}").expect("write b.n");

        let inputs = vec![root.to_string_lossy().into_owned()];
        let targets = collect_fmt_targets(&inputs).expect("walk succeeds");

        assert_eq!(
            targets,
            vec![FmtTarget::Path(root.join("a.n")), FmtTarget::Path(nested.join("b.n"))]
        );

        let _ = fs::remove_dir_all(&root);
    }

    fn unique_test_dir(prefix: &str) -> PathBuf {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("time ok")
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("nls_{}_{}", prefix, unique));
        fs::create_dir_all(&dir).expect("create temp dir");
        dir
    }
}
