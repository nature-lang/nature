# The Nature Programming Language

Nature is an open-source programming language that strives for simplicity and elegance in syntax, focusing on a pleasant experience for both writing and reading code.

When version 1.0.0 is released, it will have a stable syntax API, garbage collection, coroutines, generics, package management, and a basic standard library. Nature can be compiled to amd64/riscv64/wasm without relying on cross-compilation tools, or interpreted on nature-vm with an optional type system.

>❗️The stability of syntax API cannot be guaranteed before version 1.0.0.

## Hello Nature

To get started with Nature, download and extract the nature installation package from the releases. We recommend moving the extracted nature folder to `/usr/local/` and adding the `/usr/local/nature/bin` directory to the system's environment variables.

Create a main.n file with the following contents:
```nature
print("hello nature")
```

Compile and execute:
```shell
> nature build main.n && ./main
hello nature
```

## LICENSE
As a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions. The Open-source license only restricts the relevant rights to the source code in this repository. 

Nature's source code uses [GPL-2.0](https://www.gnu.org/licenses/gpl-2.0.html) as its license until the first official version is released.
