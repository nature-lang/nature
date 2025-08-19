# import [elf](https://github.com/nature-lang/nature/tree/master/std/elf/main.n)

ELF (可执行与可链接格式) 文件解析库，用于读取二进制文件信息。

## const EI_MAG0

```
const EI_MAG0 = 0
```

## const ELFMAG0

```
const ELFMAG0 = 0x7f
```

## const EI_MAG1

```
const EI_MAG1 = 1
```

## const ELFMAG1

```
const ELFMAG1 = 69
```

## const EI_MAG2

```
const EI_MAG2 = 2
```

## const ELFMAG2

```
const ELFMAG2 = 76
```

## const EI_MAG3

```
const EI_MAG3 = 3
```

## const ELFMAG3

```
const ELFMAG3 = 70
```

## const EM_AARCH64

```
const EM_AARCH64 = 183
```

## const EM_ARM

```
const EM_ARM = 40
```

## const EM_RISCV

```
const EM_RISCV = 243
```

## const EM_X86_64

```
const EM_X86_64 = 62
```

## const EM_386

```
const EM_386 = 3
```

## fn arch

```
fn arch(string path):string!
```

解析 ELF 文件并返回架构字符串 (arm64, armv7, riscv64, amd64, i386)。

## type elf64_ehdr_t

```
type elf64_ehdr_t = struct {
    [u8;16] ident
    u16 t
    u16 machine
    u32 version
    u64 entry
    u64 phoff
    u64 shoff
    u32 flags
    u16 ehsize
    u16 phentsize
    u16 phnum
    u16 shentsize
    u16 shnum
    u16 shstrndx
}
```

ELF64 文件头结构。