# import [base64](https://github.com/nature-lang/nature/tree/master/std/base64/main.n)

使用 mbedTLS 的 Base64 编码和解码库。

## fn encode

```
fn encode([u8] src):[u8]
```

将二进制数据编码为 Base64 格式。

## fn decode

```
fn decode([u8] src):[u8]!
```

将 Base64 编码的数据解码为二进制格式。