# import [runtime](https://github.com/nature-lang/nature/blob/master/std/runtime/main.n)

Runtime system utilities for memory management, garbage collection, and core data structures

## fn vec_new

```
fn vec_new<T>(int hash, int element_hash, int len, anyptr value_ref):vec<T>!
```

Create a new vector with specified hash, element hash, length and initial value reference

## fn vec_cap

```
fn vec_cap<T>(int hash, int element_hash, int cap):vec<T>!
```

Create a new vector with specified capacity

## fn vec_push

```
fn vec_push(anyptr list, int element_hash, anyptr val)
```

Push an element to the end of the vector

## fn vec_grow

```
fn vec_grow(anyptr list, int element_hash, int custom_capacity)
```

Grow vector capacity to the specified size

## fn vec_slice

```
fn vec_slice<T>(anyptr list, int start, int end):vec<T>!
```

Create a slice of the vector from start to end index

## fn vec_append

```
fn vec_append(anyptr list1, anyptr list2, int element_hash)
```

Append elements from list2 to list1

## fn vec_concat

```
fn vec_concat<T>(anyptr list1, anyptr list2, int element_hash):vec<T>
```

Concatenate two vectors and return a new vector

## fn set_new

```
fn set_new<T>(int hash, int key_hash):set<T>
```

Create a new set with specified hash and key hash

## fn set_add

```
fn set_add(anyptr s, anyptr key):bool
```

Add a key to the set, returns true if key was added

## fn set_contains

```
fn set_contains(anyptr s, anyptr key):bool
```

Check if the set contains the specified key

## fn set_delete

```
fn set_delete(anyptr s, anyptr key)
```

Remove a key from the set

## fn map_new

```
fn map_new<K,V>(int hash, int key_hash, int value_hash):map<K,V>
```

Create a new map with specified hash, key hash and value hash

## fn map_delete

```
fn map_delete(anyptr m, anyptr key)
```

Remove a key-value pair from the map

## fn map_contains

```
fn map_contains(anyptr m, anyptr key):bool
```

Check if the map contains the specified key

## fn map_assign

```
fn map_assign(anyptr m, anyptr key_ref):anyptr
```

Get or create a reference to the value for the specified key

## fn string_ref

```
fn string_ref(anyptr s):anyptr
```

Get a reference to the string

## fn processor_index

```
fn processor_index():int
```

Get the current processor index

## fn gc

```
fn gc()
```

Force garbage collection

## fn malloc_bytes

```
fn malloc_bytes():i64
```

Get the number of bytes allocated by malloc

## fn gc_malloc

```
fn gc_malloc(int hash):anyptr
```

Allocate memory using garbage collector with specified hash

## fn gc_malloc_size

```
fn gc_malloc_size(int size):anyptr
```

Allocate memory of specified size using garbage collector

## fn string_new

```
fn string_new(anyptr s):string
```

Create a new string from pointer

## fn string_ref_new

```
fn string_ref_new(anyptr s, int len):string
```

Create a new string from pointer with specified length
