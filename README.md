# nature 编程语言

nature 是一种开源编程语言，语法上追求简洁优雅，关注使用者的编写与阅读体验。

当 1.0.0 正式版本发布时，其将会有稳定语法 API、GC、协程、泛型、包管理、基础标准库。nature 可以不依赖交叉编译工具编译到 amd64/riscv64/wasm 或解释到 nature-vm，解释时可选类型系统。
>❗️1.0.0 版本之前无法保证稳定语法 API

## hello nature

从 releases 中下载并解压 natrue 安装包。推荐将解压后的 nature 文件夹移动到 /usr/local/ 下，并将 /usr/local/nature/bin 目录加入到系统环境变量。

创建一个 main.n 文件，并写入以下内容
```nature
print("hello nature")
```

编译并执行
```shell
> nature build main.n && ./main
hello nature
```

## LICENSE
nature 作为编程语言，在使用过程中产生的源文件(.n 文件) 和编译后生成的二进制文件不受 Open-source license 限制，Open-source license 限制的是当前仓库中的源代码的相关权利。

nature 在首个正式版本发布前，源代码使用 [GPL-2.0](https://www.gnu.org/licenses/gpl-2.0.html) 作为 License。
