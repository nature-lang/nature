https://github.com/madler/zlib

## build

```shell
make clean
./configure --static
make 
```

build file in ./libz.a

The `windows_amd64` fixture archive is built from zlib v1.3.1 with Zig 0.14.1:

```shell
make -fwin32/Makefile.gcc libz.a \
  'CC=zig cc -target x86_64-windows-gnu -fno-lto -fno-sanitize=all' \
  'AR=zig ar'
```
