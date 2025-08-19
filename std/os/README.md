# import [os](https://github.com/nature-lang/nature/blob/master/std/os/main.n)

Operating system interface providing file system operations and process utilities

## type signal

```
type signal = u8
```

Signal type for process signal handling

## fn args

```
fn args():[string]
```

Get command line arguments passed to the current program

## fn exe

```
fn exe():string!
```

Get the path of the current executable by reading /proc/self/exe

## fn dirs_sort

```
fn dirs_sort([string] dirs)
```

Sort directory entries in alphabetical order using bubble sort

## fn listdir

```
fn listdir(string path):[string]!
```

List all files and directories in the given path, excluding '.' and '..'

## fn mkdirs

```
fn mkdirs(string dir, u32 mode):void!
```

Create directories recursively with the specified permissions

## fn remove

```
fn remove(string full_path):void!
```

Remove a file at the specified path

## fn rmdir

```
fn rmdir(string dir, bool recursive):void!
```

Remove a directory, optionally removing all contents recursively