# import [json](https://github.com/nature-lang/nature/tree/master/std/json/main.n)

JSON serialization and deserialization library for converting between objects and JSON strings.

## fn serialize

```
fn serialize<T>(T object):string!
```

Convert object to json string.

## fn deserialize

```
fn deserialize<T>(string s):T!
```

Convert json string to object.