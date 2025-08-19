# import [base64](https://github.com/nature-lang/nature/tree/master/std/base64/main.n)

Base64 encoding and decoding library using mbedTLS.

## fn encode

```
fn encode([u8] src):[u8]
```

Encode binary data to Base64 format.

## fn decode

```
fn decode([u8] src):[u8]!
```

Decode Base64 encoded data to binary format.