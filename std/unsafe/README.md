# import unsafe

Unsafe operations that bypass normal safety checks, including raw pointer operations and memory manipulation

## fn vec_new

```
fn vec_new<T>(rawptr<T> p, int len):vec<T>!
```

Creates a new vector from a raw pointer and length. The length must be non-negative, otherwise throws an error

## fn ptr_to

```
fn ptr_to<T>(anyptr p):T
```

Dereferences an anyptr and returns the value of type T

## fn ptr_copy

```
fn ptr_copy<T>(anyptr dst, rawptr<T> src)
```

Copies memory from source pointer to destination pointer using the size of type T