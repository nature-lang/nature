# import [crypto.hmac](https://github.com/nature-lang/nature/tree/master/std/crypto/hmac.n)

Cryptographic hash functions and HMAC implementation using mbedTLS.

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

Compute HMAC and return hexadecimal string.

## type hmac_t

```
type hmac_t = struct{
    anyptr md_info
    int hasher_type
    int output_size
    utils.mbedtls_md_context_t mbed_ctx
}
```

HMAC context for computing hash-based message authentication codes.

### new

```
fn new(int hasher_type, [u8] secret):ptr<hmac_t>
```

Create new HMAC context with specified hash algorithm and secret key.

### hmac_t.update

```
fn hmac_t.update([u8] message):ptr<hmac_t>
```

Update HMAC with message data.

### hmac_t.finish

```
fn hmac_t.finish():[u8]
```

Finalize HMAC computation and return raw bytes.

### hmac_t.hex

```
fn hmac_t.hex():string
```

Finalize HMAC computation and return hexadecimal string.

# import [crypto.md5](https://github.com/nature-lang/nature/tree/master/std/crypto/md5.n)

MD5 hash function implementation.

## fn hex

```
fn hex(string input):string
```

Compute MD5 hash and return hexadecimal string.

## type md5_t

```
type md5_t = struct{
    utils.mbedtls_md_context_t mbed_ctx
}
```

MD5 hash context.

### new

```
fn new():ptr<md5_t>
```

Create new MD5 hash context.

### md5_t.update

```
fn md5_t.update([u8] input):ptr<md5_t>
```

Update MD5 hash with input data.

### md5_t.finish

```
fn md5_t.finish():[u8]
```

Finalize MD5 computation and return raw bytes.

### md5_t.hex

```
fn md5_t.hex():string
```

Finalize MD5 computation and return hexadecimal string.

# import [crypto.sha256](https://github.com/nature-lang/nature/tree/master/std/crypto/sha256.n)

SHA-256 hash function implementation.

## fn hex

```
fn hex(string input):string
```

Compute SHA-256 hash and return hexadecimal string.

## type sha256_t

```
type sha256_t = struct{
    utils.mbedtls_sha256_context mbed_ctx
}
```

SHA-256 hash context.

### new

```
fn new():ptr<sha256_t>
```

Create new SHA-256 hash context.

### sha256_t.update

```
fn sha256_t.update([u8] input):ptr<sha256_t>
```

Update SHA-256 hash with input data.

### sha256_t.finish

```
fn sha256_t.finish():[u8]
```

Finalize SHA-256 computation and return raw bytes.

### sha256_t.hex

```
fn sha256_t.hex():string
```

Finalize SHA-256 computation and return hexadecimal string.

# import [crypto.bcrypt](https://github.com/nature-lang/nature/tree/master/std/crypto/bcrypt.n)

bcrypt password hashing implementation using Blowfish algorithm.

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

Generate bcrypt hash from password.

## fn verify

```
fn verify([u8] hashed_password, [u8] password):void!
```

Verify if password matches the hash.

# import [crypto.rsa](https://github.com/nature-lang/nature/tree/master/std/crypto/rsa.n)

RSA asymmetric encryption implementation using mbedTLS.

## const SHA1

```
const SHA1 = 0x05
```

## const SHA224

```
const SHA224= 0x08
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

## fn generate_key

```
fn generate_key(u32 key_bits):(ptr<rsa_public_key_t>, ptr<rsa_private_key_t>)!
```

Generate RSA key pair with specified key size in bits. Returns a tuple of public key and private key.

## fn public_key_from_pem

```
fn public_key_from_pem([u8] pem_data):ptr<rsa_public_key_t>!
```

Create RSA public key from PEM format data.

## fn private_key_from_pem

```
fn private_key_from_pem([u8] pem_data, [u8] password):ptr<rsa_private_key_t>!
```

Create RSA private key from PEM format data, supporting password-protected private keys.

## type rsa_key_generator_t

```
type rsa_key_generator_t = struct{
    utils.mbedtls_rsa_context rsa_ctx
    utils.mbedtls_entropy_context entropy
    utils.mbedtls_ctr_drbg_context ctr_drbg
}
```

RSA key pair generator context.

## type rsa_public_key_t

```
type rsa_public_key_t = struct{
    utils.mbedtls_rsa_context rsa_ctx
}
```

RSA public key type, used only for encryption operations.

### rsa_public_key_t.encrypt_oaep

```
fn rsa_public_key_t.encrypt_oaep(i32 hash_algo, [u8] plaintext, [u8]? label):[u8]!
```

Encrypt data using RSA public key with OAEP padding. Supports optional label parameter.

### rsa_public_key_t.encrypt_pkcs_v15

```
fn rsa_public_key_t.encrypt_pkcs_v15([u8] plaintext):[u8]!
```

Encrypt data using RSA public key with PKCS#1 v1.5 padding.

### rsa_public_key_t.to_pem

```
fn rsa_public_key_t.to_pem():[u8]!
```

Export RSA public key as PEM format.

### rsa_public_key_t.free

```
fn rsa_public_key_t.free():void
```

Release RSA public key resources.

## type rsa_private_key_t

```
type rsa_private_key_t = struct{
    utils.mbedtls_rsa_context rsa_ctx
}
```

RSA private key type, used only for decryption operations.

### rsa_private_key_t.decrypt_oaep

```
fn rsa_private_key_t.decrypt_oaep(i32 hash_algo, [u8] ciphertext, [u8]? label):[u8]!
```

Decrypt data using RSA private key with OAEP padding. Supports optional label parameter.

### rsa_private_key_t.decrypt_pkcs_v15

```
fn rsa_private_key_t.decrypt_pkcs_v15([u8] ciphertext):[u8]!
```

Decrypt data using RSA private key with PKCS#1 v1.5 padding.

### rsa_private_key_t.to_pem

```
fn rsa_private_key_t.to_pem():[u8]!
```

Export RSA private key as PEM format.

### rsa_private_key_t.free

```
fn rsa_private_key_t.free():void
```

Release RSA private key resources.

# import [crypto.utils](https://github.com/nature-lang/nature/tree/master/std/crypto/utils.n)

Cryptographic utility functions.

## fn to_hex

```
fn to_hex([u8] input):string
```

Convert input byte array to hexadecimal string.




# import [crypto.utils](https://github.com/nature-lang/nature/tree/master/std/crypto/utils.n)

## fn to_hex

```
fn to_hex([u8] input):string
```

Convert input byte dynamic array to hexadecimal string