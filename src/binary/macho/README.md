即使把 mold 的超过来，也只能是编译成

`"/usr/local/bin/ld64.mold" -arch x86_64 -syslibroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk  main.o utils.o libuv.a -lSystem -dead_strip -fixup_chains -print-dependencies`

还需要解决 macho 下动态库的符号依赖问题。搬运也已经耗费了 3，4天，后续的搬运 + 调试估计还要 1 周左右。太过耗时

当前版本暂时放弃内置 macho linker, 进而采用 ld 命令进行链接的方式。