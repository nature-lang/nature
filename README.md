# Nature Programming Language

A general-purpose open-source programming language and compiler designed to provide developers with an **elegant and concise** development experience, enabling them to build secure and reliable cross-platform software **simply and efficiently**.


## Features

- ✓ Simple, natural, and consistent syntax design
- ✓ Independent compilation system, no dependency on llvm, supports cross-platform compilation
- ✓ Well-formed type system with support for generics, null-value safety, and error handling
- ✓ Automated GC, with very short STW
- ✓ Modularity and package management system, npkg
- ✓ Built-in shared-stack concatenation, millions of concatenation switches per second
- ✓ Built-in libuv cooperates with the concatenation to handle IO event loops
- ✓ Built-in implementations of common data structures and standard libraries, and easy interaction with C
- ✓ Editor lsp support
- ○ Asymptotic GC, capable of manual memory management
- ○ Tests DSLs, efficient and stable use of AI coding
- ○ macho cross-platform connector, currently lacks the macho linker darwin can't be cross-platform compilation.
- ○ Improvement of collaborative scheduling system
- ○ Cross-platform compilation support for wasm and risc64 platforms
- ○ Compilation to readable golang programming language

## Project Status

The nature programming language has reached a usable version, and the syntax APIs are basically stable, there will not be any drastic changes before version 1.0, but there are some minor syntax changes.

The current version of nature source code supports compilation on the following target platforms
- linux/amd64
- linux/arm64
- darwin/amd64
- darwin/arm64

nature includes a set of test cases and a standard library to test the usability of the syntax, but it has not been tested on a medium to large scale project, and so there are still many bugs.

Major work in the next versions of The main work is
- Standard library refinement
- Gathering user feedback
- Project validation and bug fixes
- Refinement of language features
- Compilation into a readable golang programming language to increase the usability of the nature programming language

Official website: [https://nature-lang.org](https://nature-lang.org)

## Design Philosophy

golang is the programming language I work with. It has a simple syntax, an excellent and high-performance cross-platform compiler and runtime implementations, an advanced concurrent design style, and a high-performance network. It has an excellent, high-performance cross-platform compiler and runtime implementation, an advanced concatenation design style, high-performance network I/O, and good support for standard libraries. However, there are some inconveniences.

- Syntax is too simple, leading to lack of expressiveness.
- Type system is not perfect, lack of nullable, enumerated parameter, generic (now available), etc.
- Error handling mechanism is cumbersome.
- GC and preemptive scheduling design is very good, but it also limits the scope of golang's application in areas such as GUI, OS, game development, and AI. nature is designed to be a continuation and optimization of the golang programming language, and to pursue certain differences, as described in the Features section.

Based on the existing features of the nature programming language, it is suitable for game engines and game development, scientific computing and AI, OS and IoT, and web development.


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