# Nature Programming Language

A general-purpose open-source programming language and compiler designed to provide developers with an **elegant and concise** development experience, enabling them to build secure and reliable cross-platform software **simply and efficiently**.

## Features

### Implemented Features

- Clean, natural, and consistent syntax design
- Independent compilation system without LLVM dependency, supporting cross-platform compilation for linux/darwin on amd64/arm64
- Comprehensive type system with support for generics, null safety, and error handling
- Automatic garbage collection with very brief STW (Stop-The-World) pauses
- Modular design with npkg package management system
- Built-in shared-stack coroutines capable of millions of context switches per second
- Built-in libuv integration with coroutine-based IO event loop
- Built-in common data structures and standard library implementation with convenient C language interoperability
- LSP (Language Server Protocol) support for editors

### Planned Features

- Progressive garbage collection with manual memory management capabilities
- Test module with dedicated testing DSL, aimed at efficient and stable AI-assisted coding
- Enhanced GC and non-preemptive coroutine scheduling system
- Cross-platform compilation support for WASM and RISC64 platforms
- Compilation to readable Go code

## Project Status

The project is currently under development. While the current syntax and API are relatively stable, minor syntax changes may occur before version 1.0.

The project includes a test suite and standard library to validate syntax functionality, though it hasn't been tested in medium to large-scale projects yet.

Given Nature's current features, it is suitable for game engine and game development, scientific computing and AI, operating systems and IoT, as well as web development.

Future development priorities include:
- Standard library enhancement
- Project validation and bug fixes
- Implementation of planned features listed above

nature is a project developed out of interest. golang is the programming language I use in my daily work, so nature is designed to be a continuation and optimization of the golang programming language, as described in the Features section.

golang has an excellent and high-performance cross-platform compiler and runtime implementation, and in future releases nature will be compiled into the readable golang language, further increasing the usability of the nature programming language.

Official website: [https://nature-lang.org](https://nature-lang.org/)

## Installation

Download and extract the Nature package from [releases](https://github.com/nature-lang/nature/releases). It's recommended to move the extracted nature folder to `/usr/local/` and add `/usr/local/nature/bin` to your system's PATH.

Create a main.n file:

```js
import fmt

fn main() {
    fmt.printf('hello nature')
}
```

Compile and run:

```
> nature build main.n && ./main
hello nature
```

---

Editor support: https://github.com/nature-lang/nls

Code examples: https://github.com/nature-lang/nature/tree/master/tests/features/cases

## Documentation

[https://nature-lang.org/docs/getting-started/hello-world](https://nature-lang.org/docs/getting-started/hello-world)

## Contribution Guide

https://nature-lang.org/docs/prologue/contribution-guide

## License

The compiler frontend, backend, runtime, and other project source code are licensed under the Apache License (Version 2.0). Nature source files (.n) included in the current project, such as the standard library, are licensed under the MIT License.