# import [crypto.hmac](https://github.com/nature-lang/nature/tree/master/std/crypto/hmac.n)

使用 mbedTLS 的加密哈希函数和 HMAC 实现。

## const MD5

```
const MD5 = 0x03
```

## const RIPEMD160

```
const RIPEMD160 = 0x04
```

## const SHA1

```
const SHA1 = 0x05
```

## const SHA224

```
const SHA224 = 0x08
```

## const SHA256

```
const SHA256 = 0x09
```

## const SHA384

```
const SHA384 = 0x0a
```

## const SHA512

```
const SHA512 = 0x0b
```

## const SHA3_224

```
const SHA3_224 = 0x10
```

## const SHA3_256

```
const SHA3_256 = 0x11
```

## const SHA3_384

```
const SHA3_384 = 0x12
```

## const SHA3_512

```
const SHA3_512 = 0x13
```

## fn hex

```
fn hex(int hasher_type, [u8] secret, [u8] message):string
```

计算 HMAC 并返回十六进制字符串。

## type hmac_t

```
type hmac_t = struct{
    anyptr md_info
    int hasher_type
    int output_size
    types.mbedtls_md_context_t mbed_ctx
}
```

用于计算基于哈希的消息认证码的 HMAC 上下文。

### new

```
fn new(int hasher_type, [u8] secret):ptr<hmac_t>
```

使用指定的哈希算法和密钥创建新的 HMAC 上下文。

### hmac_t.update

```
fn hmac_t.update([u8] message):ptr<hmac_t>
```

使用消息数据更新 HMAC。

### hmac_t.finish

```
fn hmac_t.finish():[u8]
```

完成 HMAC 计算并返回原始字节。

### hmac_t.hex

```
fn hmac_t.hex():string
```

完成 HMAC 计算并返回十六进制字符串。

# import [crypto.md5](https://github.com/nature-lang/nature/tree/master/std/crypto/md5.n)

MD5 哈希函数实现。

## fn hex

```
fn hex(string input):string
```

计算 MD5 哈希并返回十六进制字符串。

## type md5_t

```
type md5_t = struct{
    types.mbedtls_md_context_t mbed_ctx
}
```

MD5 哈希上下文。

### new

```
fn new():ptr<md5_t>
```

创建新的 MD5 哈希上下文。

### md5_t.update

```
fn md5_t.update([u8] input):ptr<md5_t>
```

使用输入数据更新 MD5 哈希。

### md5_t.finish

```
fn md5_t.finish():[u8]
```

完成 MD5 计算并返回原始字节。

### md5_t.hex

```
fn md5_t.hex():string
```

完成 MD5 计算并返回十六进制字符串。

# import [crypto.sha256](https://github.com/nature-lang/nature/tree/master/std/crypto/sha256.n)

SHA-256 哈希函数实现。

## fn hex

```
fn hex(string input):string
```

计算 SHA-256 哈希并返回十六进制字符串。

## type sha256_t

```
type sha256_t = struct{
    types.mbedtls_sha256_context mbed_ctx
}
```

SHA-256 哈希上下文。

### new

```
fn new():ptr<sha256_t>
```

创建新的 SHA-256 哈希上下文。

### sha256_t.update

```
fn sha256_t.update([u8] input):ptr<sha256_t>
```

使用输入数据更新 SHA-256 哈希。

### sha256_t.finish

```
fn sha256_t.finish():[u8]
```

完成 SHA-256 计算并返回原始字节。

### sha256_t.hex

```
fn sha256_t.hex():string
```

完成 SHA-256 计算并返回十六进制字符串。

# import [crypto.bcrypt](https://github.com/nature-lang/nature/tree/master/std/crypto/bcrypt.n)

使用 Blowfish 算法的 bcrypt 密码哈希实现。

## const MIN_COST

```
const MIN_COST = 4
```

## const MAX_COST

```
const MAX_COST = 31
```

## const DEFAULT_COST

```
const DEFAULT_COST = 10
```

## fn hash

```
fn hash([u8] password, int cost):[u8]!
```

生成密码的 bcrypt 哈希值。

## fn verify

```
fn verify([u8] hashed_password, [u8] password):void!
```

验证密码是否与哈希值匹配。

# import [crypto.types](https://github.com/nature-lang/nature/tree/master/std/crypto/types.n)

加密类型定义和 mbedTLS 绑定。

## type mbedtls_md_context_t

```
type mbedtls_md_context_t = struct{
    anyptr md_info
    anyptr md_ctx
    anyptr hmac_ctx
}
```

mbedTLS 消息摘要上下文。

## type mbedtls_sha256_context

```
type mbedtls_sha256_context = struct{
    [u8;64] buffer
    [u32;2] total
    [u32;8] state
    i32 is224
}
```

mbedTLS SHA-256 上下文结构。