
# [import fs](https://github.com/nature-lang/nature/blob/master/std/fs/main.n)

File system, including opening and writing of files

## const FOO

```
const FOO = xxx
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

##  const BAR

```
const BAR = xxx
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

## var foo

```
var foo = xxxx
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

## var bar

```
var bar = xxxx
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


## fn stdout

```
fn stdout():ptr<file_t>!
```

Get the standard output of the system and convert it to file_t

## fn xxx

```
fn xxx()
```

A brief introduction to this function

## type file_t

```
type file_t:io.reader, io.writer, io.seeker = struct{  
	// ... Just need to display some fields that may be used externally
}
```

File represents an open file descriptor.

### fn open

```
fn open(...):...
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

### fn from

```
fn from(int fd, string name):ptr<file_t>!
```

Create a file_t structure with file descriptors

### fn file_t.content

```
fn file_t.content():string!
```

Read the complete file and return the string, which may return an error




# import fs.sub

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


## var xxx

```
var xxx = xxx
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx



## fn xxx

```
fn xxx(xx xx):xx
```

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

