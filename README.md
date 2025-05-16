# Nature Programming Language  
  
A general-purpose open-source programming language and compiler designed to provide developers with an **elegant and concise** development experience, enabling them to build secure and reliable cross-platform software **simply and efficiently**.  

## Features

- ✓ Simple, natural, consistent syntax design, even programming beginners can easily grasp, quickly get started!
- ✓ No dependency on llvm and VM, compiles directly to target platform machine code and supports cross-compilation
- ✓ Simple deployment, purely static linking based on musl libc, no additional dependencies, good cross-platform characteristics
- ✓ Comprehensive type system with support for generics, union types, interfaces, null-value safety, etc.
- ✓ Same high-performance GC implementation as go, with very short STW (Stop The World)
- ✓ High-performance memory allocator implementation like go, similar to google/tcmalloc
- ✓ High-performance shared-stack Coroutine implementation, capable of millions of Coroutine switches per second
- ✓ Built-in libuv to handle IO event loops in collaboration with the concatenators
- ✓ High-performance runtime and compiler based on a pure C implementation
- ✓ Modularity and package management system npkg
- ✓ Built-in implementations of common data structures and standard libraries
- ✓ Use try+catch for error handling, match for pattern matching, channel for concurrent communication, select for concurrent processing
- ✓ Follow the system ABI, built-in libc, you can directly call the C language standard library functions to accelerate code development
- ✓ editor lsp support
- ○ High-performance memory management to assist in automatic GC
- ○ Testing DSL in hopes of utilizing AI coding efficiently and consistently
- ○ macho cross-platform linker (lack of macho linker currently prevents cross-compilation on darwin platform)
- ○ Collaborative scheduling system
- ○ Cross-platform compilation support for wasm and risc64 platforms
- ○ Compile to readable go programming language
  
## Project Overview

The nature programming language has reached an early usable version, and the syntax API is basically stable and will not change significantly before version 1.0.

The current version of nature source code supports compilation on the following target platforms
- linux/amd64
- linux/arm64
- darwin/amd64
- darwin/arm64

nature includes a set of test cases and standard libraries to test the usability of basic functionality and syntax, and a small set of projects to test overall usability, but has not yet been tested in medium to large scale projects.

The core work to follow will be to improve the usability of the nature programming language, including standard library refinement, user feedback collection, and bug fixes.
  
Official website: [https://nature-lang.org](https://nature-lang.org)  


## Design Philosophy

Go is the programming language I use in my daily work. Its syntax is simple, and it offers very convenient cross-compilation and deployment, a highly efficient and high-performance runtime implementation, and an advanced concurrent design style. However, it also has some inconvenient aspects:

- The overly simplistic syntax leads to insufficient expressive power.
- The type system is not sufficiently comprehensive.
- The error handling mechanism is cumbersome.
- Although the automatic GC and preemptive scheduling designs are excellent, they also limit the application scope of go.
- Package management methods.
- `interface{}`
- ...

nature is designed to be a continuation and improvement of the go programming language, and pursues certain differences. While improving the above problems, nature has runtime, GMP model, allocator, collector, coroutine, channel, std, etc. similar to go. nature also has no dependency on llvm, high compilation speed, and easy cross-compilation and deployment.

Based on the existing features of the nature programming language, it is suitable for game engines and game development, scientific computing and AI, operating systems and the Internet of Things, the command line, and web development.


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
  
[parker](https://github.com/weiwenhao/parker) Lightweight packaging tool  

[llama.n](https://github.com/weiwenhao/llama.n) Llama2 nature language implementation  

[tetris](https://github.com/weiwenhao/tetris) Tetris implementation based on raylib, macos only  

[playground](https://github.com/weiwenhao/playground) playground server api implementation  
  
More syntax examples https://github.com/nature-lang/nature/tree/master/tests/features/cases  
  
## Contribution guidelines  
  
https://nature-lang.org/docs/contribute  
  
## License  
  
The source code for the compiler frontend, backend, and runtime of this project is restricted by the Apache License (Version 2.0). Nature source files (.n) such as standard libraries included in the current project use the MIT license.