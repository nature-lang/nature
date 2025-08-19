# import [process](https://github.com/nature-lang/nature/blob/master/std/process/main.n)

Process management, including process creation, execution and state management

## fn run

```
fn run(string name, [string] args):ptr<state_t>!
```

Execute a command and wait for completion, returning the process state including stdout, stderr and exit code

## fn command

```
fn command(string name, [string] args):ptr<command_t>!
```

Create a command object for process execution with customizable stdin, stdout and stderr

## type state_t

```
type state_t = struct{
    string stdout
    string stderr
    i32 exit_code
    i32 term_sig
}
```

Process execution state containing output streams and exit information

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

Running process instance with process ID, exit status and associated command

### process_t.wait

```
fn process_t.wait():void!
```

Wait for the process to complete execution

### process_t.read_stdout

```
fn process_t.read_stdout():string!
```

Read data from the process's standard output stream

### process_t.read_stderr

```
fn process_t.read_stderr():string!
```

Read data from the process's standard error stream

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

Command configuration including executable name, arguments, working directory and I/O streams

### command_t.spawn

```
fn command_t.spawn():ptr<process_t>!
```

Spawn a new process from the command configuration

### command_t.uv_spawn

```
fn command_t.uv_spawn(ptr<command_t> cmd):ptr<process_t>!
```

Low-level process spawning using libuv backend