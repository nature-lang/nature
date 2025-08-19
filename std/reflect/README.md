# import [reflect](https://github.com/nature-lang/nature/blob/master/std/reflect/main.n)

Runtime reflection utilities for type introspection and value manipulation

## const NULL

```
const NULL = 1
```

Type constant for null type

## const BOOL

```
const BOOL = 2
```

Type constant for boolean type

## const I8

```
const I8 = 3
```

Type constant for 8-bit signed integer type

## const U8

```
const U8 = 4
```

Type constant for 8-bit unsigned integer type

## const I16

```
const I16 = 5
```

Type constant for 16-bit signed integer type

## const U16

```
const U16 = 6
```

Type constant for 16-bit unsigned integer type

## const I32

```
const I32 = 7
```

Type constant for 32-bit signed integer type

## const U32

```
const U32 = 8
```

Type constant for 32-bit unsigned integer type

## const I64

```
const I64 = 9
```

Type constant for 64-bit signed integer type

## const U64

```
const U64 = 10
```

Type constant for 64-bit unsigned integer type

## const INT

```
const INT = 9
```

Type constant for int type (translates to i64 at compile time)

## const UINT

```
const UINT = 10
```

Type constant for uint type (translates to u64 at compile time)

## const F32

```
const F32 = 11
```

Type constant for 32-bit floating point type

## const F64

```
const F64 = 12
```

Type constant for 64-bit floating point type

## const FLOAT

```
const FLOAT = 12
```

Type constant for float type (translates to f64 at compile time)

## const STRING

```
const STRING = 13
```

Type constant for string type

## const VEC

```
const VEC = 14
```

Type constant for vector type

## const ARR

```
const ARR = 15
```

Type constant for array type

## const MAP

```
const MAP = 16
```

Type constant for map type

## const SET

```
const SET = 17
```

Type constant for set type

## const TUP

```
const TUP = 18
```

Type constant for tuple type

## const STRUCT

```
const STRUCT = 19
```

Type constant for struct type

## const FN

```
const FN = 20
```

Type constant for function type

## const CHAN

```
const CHAN = 21
```

Type constant for channel type

## const COROUTINE_T

```
const COROUTINE_T = 22
```

Type constant for coroutine type

## const PTR

```
const PTR = 23
```

Type constant for pointer type

## const RAWPTR

```
const RAWPTR = 24
```

Type constant for raw pointer type

## const ANYPTR

```
const ANYPTR = 25
```

Type constant for any pointer type

## const UNION

```
const UNION = 26
```

Type constant for union type

## const PTR_SIZE

```
const PTR_SIZE = 8
```

Size of pointer in bytes

## fn find_rtype

```
fn find_rtype(int hash):rawptr<rtype_t>
```

Find runtime type information by hash

## fn find_data

```
fn find_data(int offset):anyptr
```

Find data at specified offset

## fn find_strtable

```
fn find_strtable(int offset):libc.cstr
```

Find string in string table at specified offset

## fn typeof_hash

```
fn typeof_hash(i64 hash):type_t!
```

Get type information from hash value

## fn sizeof_rtype

```
fn sizeof_rtype(rawptr<rtype_t> r):int
```

Get size of runtime type

## fn typeof

```
fn typeof<T>(T v):type_t!
```

Get type information of a value

## fn valueof

```
fn valueof<T>(T v):value_t!
```

Create a value wrapper for supported types (vec, map, set)

## type field_t

```
type field_t = struct{
    string name
    int hash
    int offset
}
```

Represents a struct field with name, hash and offset information

## type type_t

```
type type_t = struct{
    int size
    int hash
    int kind
    u8 align
    [int] hashes
    [field_t] fields
}
```

Represents type information including size, hash, kind and field details

### fn type_t.to_string

```
fn type_t.to_string():string
```

Convert type kind to string representation

## type rtype_struct_t

```
type rtype_struct_t = struct{
    i64 name_offset
    i64 hash
    i64 offset
}
```

Runtime struct type information

## type rtype_t

```
type rtype_t = struct{
    u64 ident_offset
    u64 size
    u8 in_heap
    i64 hash
    u64 last_ptr
    u32 kind
    i64 malloc_gc_bits_offset
    u64 gc_bits
    u8 align
    u16 length
    i64 hashes_offset
}
```

Runtime type information structure

## type vec_t

```
type vec_t = struct{
    anyptr data
    i64 length
    i64 capacity
    i64 element_size
    i64 hash
}
```

Internal vector structure representation

## type string_t

```
type string_t = vec_t
```

Internal string structure (alias for vec_t)

## type map_t

```
type map_t = struct{
    anyptr hash_table
    anyptr key_data
    anyptr value_data
    i64 key_hash
    i64 value_hash
    i64 length
    i64 capacity
}
```

Internal map structure representation

## type set_t

```
type set_t = struct{
    anyptr hash_table
    anyptr key_data
    i64 key_hash
    i64 length
    i64 capacity
}
```

Internal set structure representation

## type union_t

```
type union_t = struct{
    anyptr value
    rawptr<rtype_t> rtype
}
```

Internal union structure representation

## type value_t

```
type value_t = struct{
    type_t t
    anyptr v
}
```

Value wrapper containing type information and value pointer

### fn value_t.len

```
fn value_t.len():int
```

Get the length of the wrapped value (for vec, map, set types)