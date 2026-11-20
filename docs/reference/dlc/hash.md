# `DLC:hash`

```cuff
use DLC:hash
```

One-way hashes and checksums, message authentication, key derivation, and the encodings that write bytes as text. Every
function works on **bytes**: a `str` gives its bytes as they are (the UTF-8 of its text, or whatever bytes
[`file_read`](filesystem.md) gave), and a list of whole numbers from 0 to 255 gives those bytes. A hash is returned as lowercase
hexadecimal text. A wrong type is `E4-005`, a list item that is not a byte is `E4-025`, and the wrong number of arguments is
`E4-010`.

## Hashes and checksums

| Function | Result |
| :--- | :--- |
| `hash_sha256(data)` | SHA-256 (FIPS 180-4): 64 hexadecimal digits. The standard choice for a fingerprint of data. |
| `hash_sha512(data)` | SHA-512: 128 digits. |
| `hash_sha1(data)` | SHA-1: 40 digits. Broken for collisions; for compatibility with what still uses it (version control, WebSocket handshakes), not for security. |
| `hash_md5(data)` | MD5 (RFC 1321): 32 digits. Broken for collisions; for file checksums and old formats only. |
| `hash_crc32(data)` | CRC-32 as zip, PNG and Ethernet compute it: 8 digits. A fast check for accidental damage, with no security at all. |

## Authentication and key derivation

| Function | Result |
| :--- | :--- |
| `hash_hmac_sha256(key, message)` | HMAC-SHA-256 (RFC 2104): a code that only someone with the key can make for the message. |
| `hash_hmac_sha512(key, message)` | HMAC-SHA-512. |
| `hash_pbkdf2_sha256(password, salt, iterations [, length])` | PBKDF2 with HMAC-SHA-256 (RFC 8018): a key of `length` bytes (32 when left out) made slow to guess by `iterations` rounds; both have a [limit](../limits.md), and a number outside it is `E4-025`. For storing passwords, a salt of 16 random bytes and 600,000 rounds or more are the usual advice. |
| `hash_equal(a, b)` | Whether `a` and `b` are the same bytes, compared in a time that does not depend on where they differ. Use it to check a code or key that someone else sent; `is` stops at the first difference, which tells a patient attacker how much of a guess was right. |

## Bytes as text

| Function | Result |
| :--- | :--- |
| `hash_hex_encode(data)`, `hash_hex_decode(text)` | Base16: two lowercase digits per byte; decoding takes either case and an even number of digits (`E4-025` otherwise). |
| `hash_base64_encode(data)`, `hash_base64_decode(text)` | Base64 (RFC 4648, section 4), with `=` padding. Decoding accepts the text with or without its padding, and is `E4-025` for a character outside the alphabet, padding in the wrong place, or a length that Base64 cannot have. |
| `hash_base64url_encode(data)`, `hash_base64url_decode(text)` | The URL-safe Base64 (section 5): `-` and `_` in place of `+` and `/`, written without padding, as JSON Web Tokens and URLs use it. |
| `hash_to_bytes(text)` | The bytes of a `str`, as a list of numbers. |
| `hash_from_bytes(bytes)` | A `str` made of those bytes. |

The decoders give a `str` that holds the bytes, which need not be text: `file_write` writes them as they are, and `hash_to_bytes`
shows them as numbers.

```cuff
use DLC:hash
print(hash_sha256("abc"))
print(hash_crc32("123456789"), hash_md5(""))
str key = "secret"
str signature = hash_hmac_sha256(key, "amount=10")
print(hash_equal(signature, hash_hmac_sha256(key, "amount=10")), hash_equal(signature, hash_hmac_sha256(key, "amount=99")))
print(hash_base64_encode("hello"), hash_base64_decode("aGVsbG8="), hash_hex_encode("hi"))
print(hash_base64url_encode(hash_hex_decode("fbffbf")), hash_to_bytes(hash_base64url_decode("-_-_")))
```

```output
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
cbf43926 d41d8cd98f00b204e9800998ecf8427e
true false
aGVsbG8= hello 6869
-_-_ [251, 255, 191]
```

A hash of raw bytes rather than of their hexadecimal text, as many web APIs want, is `hash_hex_decode` of it:
`hash_base64_encode(hash_hex_decode(hash_sha256(data)))`.

## What this library does not do

- **It is not an audited cryptographic library.** The hashes are the engine's own code, checked against the published test vectors
  (FIPS 180-4, RFC 1321, RFC 4231, RFC 4648, RFC 7914) and against another implementation on thousands of random inputs, but not
  reviewed as security code. For a system whose security rests on them, use a vetted library in the host.
- **It keeps no secret and makes no random numbers.** A salt or a key has to come from somewhere random; [`DLC:random`](random.md)
  is not, and must not be used for one.
- **Only `hash_equal` takes care over timing.** The hashes themselves run in a time that depends on the length of the data only,
  but a comparison with `is` does not.
- **`hash_pbkdf2_sha256` runs to its end.** The most rounds it allows take a few seconds, and a `--timeout` is looked at when the
  call returns.
- **The data is held in memory**, so it is bounded by the [string limit](../limits.md); there is no hashing of a stream.
