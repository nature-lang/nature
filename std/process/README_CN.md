# import [process](https://github.com/nature-lang/nature/blob/master/std/process/main.n)

进程管理，包括进程创建、执行和状态管理

## fn run

```
fn run(string name, [string] args):ptr<state_t>!
```

执行命令并等待完成，返回包含标准输出、标准错误和退出码的进程状态

## fn command

```
fn command(string name, [string] args):ptr<command_t>!
```

创建用于进程执行的命令对象，可自定义标准输入、标准输出和标准错误

## type state_t

```
type state_t = struct{
    string stdout
    string stderr
    i32 exit_code
    i32 term_sig
}
```

进程执行状态，包含输出流和退出信息

## type process_t

```
type process_t = struct{
    int pid
    anyptr args
    anyptr envs
    anyptr p
    anyptr co
    bool exited
    i32 exit_code
    i32 term_sig
    command_t cmd
}
```

运行中的进程实例，包含进程ID、退出状态和关联的命令

### process_t.wait

```
fn process_t.wait():void!
```

等待进程完成执行

### process_t.read_stdout

```
fn process_t.read_stdout():string!
```

从进程的标准输出流读取数据

### process_t.read_stderr

```
fn process_t.read_stderr():string!
```

从进程的标准错误流读取数据

## type command_t

```
type command_t = struct{
    string name
    [string] args
    string cwd
    [string] env
    io.reader stdin
    io.writer stdout
    io.writer stderr
}
```

命令配置，包括可执行文件名、参数、工作目录和I/O流

### command_t.spawn

```
fn command_t.spawn():ptr<process_t>!
```

从命令配置生成新进程

### command_t.uv_spawn

```
fn command_t.uv_spawn(ptr<command_t> cmd):ptr<process_t>!
```

使用libuv后端的底层进程生成