# import [strings](https://github.com/nature-lang/nature/blob/master/std/strings/main.n)

String manipulation utilities for common string operations

## fn from

```
fn from(anyptr p):string!
```

Create a nature string from a C string pointer

## fn join

```
fn join([string] list, string separator):string
```

Join array of strings with separator

## fn string.find_char

```
fn string.find_char(self, u8 char, int after):int
```

Find the index of character starting from after position, returns -1 if not found

## fn string.find_after

```
fn string.find_after(self, string sub, int after):int
```

Find the index of substring starting from after position, returns -1 if not found

## fn string.reverse

```
fn string.reverse(self):string
```

Reverse the characters in string

## fn string.rfind

```
fn string.rfind(self, string sub):int
```

Find the index of the last occurrence of substring, returns -1 if not found

## fn string.ends_with

```
fn string.ends_with(self, string ends):bool
```

Check if string ends with the given suffix

## fn string.starts_with

```
fn string.starts_with(self, string starts):bool
```

Check if string starts with the given prefix

## fn string.contains

```
fn string.contains(self, string sub):bool
```

Check if string contains the substring

## fn string.finish

```
fn string.finish(self, string cap):string
```

Ensure string ends with the given suffix, append if not present

## fn string.find

```
fn string.find(self, string sub):int
```

Find the index of the first occurrence of substring, returns -1 if not found

## fn string.slice

```
fn string.slice(self, int start, int end):string
```

Extract a substring from start to end index

## fn string.split

```
fn string.split(self, string separator):[string]
```

Split string by separator and return an array of substrings

## fn string.ascii

```
fn string.ascii(self):u8
```

Get the ASCII value of the first character

## fn string.ltrim

```
fn string.ltrim(self, [string] list):string
```

Remove leading characters specified in the list

## fn string.rtrim

```
fn string.rtrim(self, [string] list):string
```

Remove trailing characters specified in the list

## fn string.trim

```
fn string.trim(self, [string] list):string
```

Remove leading and trailing characters specified in the list

## fn string.replace

```
fn string.replace(self, string sub_old, string sub_new):string
```

Replace all occurrences of old substring with new substring

## fn string.to_int

```
fn string.to_int(self):int!
```

Convert string to integer, may throw error if invalid format

## fn string.to_float

```
fn string.to_float(self):float!
```

Convert string to float, may throw error if invalid format

## fn string.to_lower

```
fn string.to_lower(self):string
```

Convert string to lowercase

## fn string.to_upper

```
fn string.to_upper(self):string
```

Convert string to uppercase