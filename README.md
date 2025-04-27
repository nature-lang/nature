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

Official website: [https://nature-lang.org](https://nature-lang.org)

## Design Philosophy

golang is the programming language I work with. It has a simple syntax, a very good and high-performance cross-platform compiler and runtime implementation, an advanced concurrent design style and high-performance network I/O, and good support for standard libraries. However, there are some inconveniences

- The syntax is too simple, resulting in a lack of expressiveness.
- The type system is not well developed, lacking nullable, enumerated parameters, generic types (which exist now), etc.
- Cumbersome error handling mechanism
- Automatic GC and preemptive scheduling are excellent, but they limit the scope of golang.

nature is designed to be a continuation and optimization of the golang programming language, and to pursue certain differences, as described in the Features section.

Based on the existing features of the nature programming language, it is suitable for game engines and game development, scientific computing and AI, operating systems and the Internet of Things, and web development.


## Install

Download and extract the natrue installer from [releases](https://github.com/nature-lang/nature/releases) (note the correct permissions). Move the extracted nature folder to `/usr/local/` and add the `/usr/local/nature/bin` directory to the system environment variable.

> If you need to install into another directory you need to manually set the NATURE_ROOT environment variable to the corresponding directory

Run the `nature -v` command to check the version, and run the `nature -h` command to check the help

```sh 
> nature -v 
nature v0.5.0 - release build 2025-05-01 
``` 

create a main.n file

```js 
import fmt 
  
fn main() { 
 fmt.printf('hello nature') 
} 
``` 

compile and execute

```sh 
> nature build main.n && . /main 

hello nature 
``` 


--- 


## Documentation

Quick start [https://nature-lang.org/docs/get-started](https://nature-lang.org/docs/get-started)

Syntax documentation [https:// nature-lang.org/docs/syntax](https://nature-lang.org/docs/syntax)

Standard library documentation [https://nature-lang.org/stds](https://nature-lang.org/stds)

Try in browser [https://nature-lang.org/playground](https://nature-lang.org/playground)

LSP https://github.com/nature-lang/nls Reference README.md

## Project examples

1. [parker](https://github.com/weiwenhao/parker) Lightweight packaging tool
2. [llama.n](https://github.com/weiwenhao/llama.n) Llama2 nature language implementation
3. [ tetris](https://github.com/weiwenhao/tetris) Tetris implementation based on raylib, macos only
4. [playground](https://github.com/weiwenhao/playground) playground server api implementation

More syntax examples https://github.com/nature-lang/nature/tree/master/tests/features/cases

## Contribution guidelines

https://nature-lang.org/docs/contribute

## License

The source code for the compiler frontend, backend, and runtime of this project is restricted by the Apache License (Version 2.0). Nature source files (.n) such as standard libraries included in the current project use the MIT license.