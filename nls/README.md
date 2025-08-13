# NLS - Nature Language Server

This is the LSP (Language Server Protocol) server implementation for the [nature](https://github.com/nature-lang/nature) programming language, which includes both the server (nls) and VSCode client components.


## Features

The current version corresponds to nature version 0.5.x and implements:

- Semantic highlighting
- Syntax checking 
- Type checking
- Package dependency management
- Multi-workspace support
- Platform-specific module resolution

Code completion features are planned for future releases.


## Building

This project is implemented in Rust. To build the project:

bash
cargo build --release

This will generate the `nls` executable.

## Installation

### VSCode Extension

The LSP client can be downloaded from the VSCode extension marketplace by searching for "nature language".

### Server Installation

The nls LSP server needs to be installed in `/usr/local/nature/bin`. The VSCode client will automatically detect and launch the server from this path.

Note: The nls executable is also bundled in the nature-lang release package. If you have already installed nature-lang in `/usr/local/nature`, no additional server installation is required.

### Debugging

Launch configurations are provided for VSCode in `.vscode/launch.json` for debugging both the client and server components.

## License

MIT