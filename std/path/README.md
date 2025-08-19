# import [path](https://github.com/nature-lang/nature/blob/master/std/path/main.n)

Path manipulation utilities for file system paths

## fn exists

```
fn exists(string path):bool
```

Check if a file or directory exists at the given path

## fn base

```
fn base(string path):string
```

Return the last element of the path, similar to the Unix basename command

## fn dir

```
fn dir(string path):string
```

Return the directory portion of the path, similar to the Unix dirname command

## fn join

```
fn join(string dst, ...[string] list):string
```

Join multiple path segments into a single path with proper separators

## fn isdir

```
fn isdir(string path):bool
```

Check if the given path is a directory