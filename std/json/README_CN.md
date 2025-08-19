# import [json](https://github.com/nature-lang/nature/tree/master/std/json/main.n)

JSON 序列化和反序列化库，用于在对象和 JSON 字符串之间进行转换。

## fn serialize

```
fn serialize<T>(T object):string!
```

将对象转换为 JSON 字符串。

## fn deserialize

```
fn deserialize<T>(string s):T!
```

将 JSON 字符串转换为对象。