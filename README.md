<p align="center"><a href="https://www.nature-lang.com" target="_blank"><img src="https://raw.githubusercontent.com/weiwenhao/pictures/main/blogslogo_300.png" width="400" alt="nature Logo"></a></p>

# The Nature Programming Language

Nature is a programming language that pursues simplicity and elegance in syntax, focusing on the writing and reading experience of its users.

When the official version is released, nature will have a stable syntax API, type system, GC, coroutine, generics, package management, and basic standard library.

Nature supports cross-compilation, which can be compiled to linux/darwin & amd64/riscv64/wasm, and can also be interpreted to execute on the nature-vm.

## Hello Nature

To get started with Nature, download and extract the nature installation package from the [releases](https://github.com/nature-lang/nature/releases). We recommend moving  
the extracted nature folder to `/usr/local/` and adding the `/usr/local/nature/bin` directory to the system's environment variables.

Create a main.n file with the following contents:

```nature
print("hello nature")
```

Compile and execute:

```shell
# docker run --rm -it -v $PWD:/app --name nature naturelang/nature:latest sh -c 'nature build main.n && ./main'
> nature build main.n && ./main
hello nature
```

## License

As a programming language, source files (.n files) and compiled binary files generated during use of Nature are not
subject to Open-source license restrictions. The Open-source license only restricts the relevant rights to the source
code in this repository.

Nature's source code uses [GPL-2.0](https://www.gnu.org/licenses/gpl-2.0.html) as its license until the first official
version is released.
