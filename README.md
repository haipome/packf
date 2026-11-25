# packf

A lightweight binary serialization library for C/C++ that converts data structures to and from a binary wire format using a `printf`-style format string.

**Author:** Haipo Yang &lt;yang@haipo.me&gt; | **License:** Public Domain

---

## Table of Contents

- [Overview](#overview)
- [Advantages](#advantages)
- [Format String Syntax](#format-string-syntax)
  - [Types](#types)
  - [Every form at a glance](#every-form-at-a-glance)
  - [Fields inside a struct](#fields-inside-a-struct)
  - [Rules that are easy to mix up](#rules-that-are-easy-to-mix-up)
  - [Byte order](#byte-order)
  - [`s` vs `S`](#s-vs-s)
  - [Forward compatibility with `-[...]`](#forward-compatibility-with--)
  - [Untrusted input](#untrusted-input)
- [API Reference](#api-reference)
- [Usage Examples](#usage-examples)
- [Building](#building)

---

## Overview

packf provides `packf`/`unpackf` and their variants to serialize and deserialize C data — scalars, arrays, strings, and packed structs — into a portable binary wire format (big-endian by default, little-endian on request). The interface mirrors `sprintf`/`sscanf`: a single format string describes the wire layout and drives both directions.

```c
char buf[256];

// Pack: host → wire (network byte order)
int len = packf(buf, sizeof(buf), "wd", 0x1234, 0xDEADBEEF);

// Unpack: wire → host
uint16_t a; uint32_t b;
unpackf(buf, len, "wd", &a, &b);
```

A whole message, including variable-length parts, fits in one line:

```c
// 4-byte type, then up to 10 users, each: 4-byte id, LV name, compact password
packf(buf, sizeof(buf), "[d =10[d -64s 32S]]", &msg);
```

---

## Advantages

- **Zero dependencies** — a single `.c`/`.h` pair, no external libraries required.
- **Familiar API** — format-string driven, just like `sprintf`/`sscanf`.
- **One description, two directions** — the same format string packs and unpacks.
- **Comprehensive type support** — 8/16/32/64-bit integers, float, double, strings, padding, arrays, and nested structs.
- **LV (Length–Value) fields** — built-in variable-length arrays, strings and structs with a 1-byte or 2-byte length prefix; no manual length bookkeeping.
- **Automatic byte-swap** — multi-byte values are converted to/from the wire byte order (big-endian by default, `<` for little-endian). No manual `htons`/`ntohl` calls.
- **Struct-aware** — packs/unpacks directly from `#pragma pack(1)` structs, so the struct definition mirrors the wire format.
- **Incremental packing** — `vpackf`/`vunpackf` update the current position and remaining length, making multi-step message assembly easy.
- **No allocation, no global state** — thread-safe; errors are reported by return code only.
- **Public domain** — use freely in any project, commercial or otherwise.

---

## Format String Syntax

A format string is a sequence of fields. Each field has up to three parts:

```
[-=][num]type
```

| Part | Meaning |
|------|---------|
| `-` or `=` | Optional. Puts a length prefix on the wire — **1 byte** for `-`, **2 bytes** for `=` — which makes the field variable-length (an *LV* field). |
| `num` | Optional. Without a prefix: the fixed count. With a prefix: the **maximum** count. |
| `type` | One type character from the table below. |

Between fields you may put spaces (for readability) and the byte-order switches `<` and `>`. Nothing may come between the parts of one field: `"-64 d"` and `"-<d"` are errors.

### Types

| Type | Wire size | C type |
|------|-----------|--------|
| `c`  | 1 byte    | `int8_t` / `uint8_t` |
| `w`  | 2 bytes   | `int16_t` / `uint16_t` |
| `d`  | 4 bytes   | `int32_t` / `uint32_t` |
| `D`  | 8 bytes   | `int64_t` / `uint64_t` |
| `f`  | 4 bytes   | `float` |
| `F`  | 8 bytes   | `double` |
| `s`  | variable  | `char` string |
| `S`  | variable  | `char` string, compact on the wire (see [`s` vs `S`](#s-vs-s)) |
| `a`  | variable  | zero byte(s) / padding; takes no value |
| `[`…`]` | variable | struct, described by the fields between the brackets |

### Every form at a glance

There are 16 forms in total. The tables use `-`; `=` works the same with a 2-byte prefix and a `uint16_t` count. "Pack" and "Unpack" are the arguments you pass for a field at the top level of the format.

**Numbers** — `c w d D f F`, shown with `d`:

| Format | Meaning | On the wire | Pack | Unpack |
|--------|---------|-------------|------|--------|
| `d`    | one value | 4 bytes | the value | pointer to it |
| `4d`   | array of exactly 4 | 4 × 4 bytes | `int32_t *` | `int32_t *` |
| `-4d`  | array of 0–4 | prefix (**element count**) + count × 4 bytes | `int` count, `int32_t *` | `uint8_t *` count, `int32_t *` |
| `-d`   | array, no maximum | same | same | same |

Values are passed the way C promotes variadic arguments: `c`, `w`, `d` as `int`, `D` as `int64_t`, `f` and `F` as `double`.

**Strings** — `s S`. The argument is always a `char *`; no length is passed.

| Format | Meaning | On the wire |
|--------|---------|-------------|
| `s`    | C string | `strlen + 1` bytes, `\0` included |
| `16s`  | fixed-width field, up to 15 chars | always 16 bytes, padded with `\0` |
| `16S`  | compact string, up to 15 chars | `strlen + 1` bytes, `\0` included |
| `-16s` | LV string, up to 15 chars | prefix (**byte count**) + the chars, no `\0` |
| `-s`   | LV string, no maximum | same |

**Padding** — `a`:

| Format | Meaning | On the wire | Pack | Unpack |
|--------|---------|-------------|------|--------|
| `a`    | one zero byte | 1 byte | — | — |
| `4a`   | four zero bytes | 4 bytes | — | — |
| `-4a`, `-a` | variable padding | prefix + that many zero bytes | `int` count | `uint8_t *` count |

When unpacking, padding is skipped without checking that it is zero.

**Structs** — `[...]`. The argument is a pointer to a `#pragma pack(1)` struct (or array of them).

| Format | Meaning | On the wire | Pack | Unpack |
|--------|---------|-------------|------|--------|
| `[...]`   | one struct | its fields in order | struct pointer | struct pointer |
| `3[...]`  | array of exactly 3 | 3 structs | array pointer | array pointer |
| `-3[...]` | array of 0–3 | prefix (**element count**) + count structs | `int` count, array pointer | `uint8_t *` count, array pointer |
| `-[...]`  | one length-prefixed struct | prefix (**byte length** of the body) + the struct | struct pointer | struct pointer |

### Fields inside a struct

Inside `[...]` no arguments are passed; each field maps to a member of the struct instead. An LV field needs a length member right before its data, sized like the prefix (`uint8_t` for `-`, `uint16_t` for `=`), so the struct keeps the same shape as the protocol.

| Field | Declare in the struct | Length member |
|-------|-----------------------|---------------|
| `d` | `int32_t x;` | — |
| `4d` | `int32_t x[4];` | — |
| `-4d` | `uint8_t n; int32_t x[4];` | **you set it** before packing; filled in when unpacking |
| `16s`, `16S` | `char s[16];` | — |
| `-16s` | `uint8_t n; char s[16];` | automatic: ignored when packing (`strlen` is used), filled in when unpacking |
| `4a` | `uint8_t pad[4];` | — |
| `[...]`, `3[...]` | `struct T t;` / `struct T t[3];` | — |
| `-3[...]` | `uint8_t n; struct T t[3];` | **you set it** before packing; filled in when unpacking |
| `-[...]` | `uint8_t n; struct T t;` | automatic: ignored when packing, filled in when unpacking |

- Length members in a struct are always in host byte order; only the prefix on the wire is converted.
- With `num`, the struct always reserves the full maximum (`x[4]`), whatever the actual count.
- Forms without a fixed local size — `s`, `-s`, `-d` and other prefixed fields without `num` — take a variable number of bytes in the struct, so the members after them have no fixed offset. Avoid them inside structs, or put them last.
- All elements of a struct array must have the same local size.

### Rules that are easy to mix up

- **What the prefix counts.** Elements for number arrays and struct arrays; bytes for strings and for `-[...]`.
- **Whether you pass a count.** Yes for number arrays, struct arrays and LV padding; no for strings and `-[...]`, where it is computed.
- **Length members exist only inside a struct.** At the top level, `-16s` takes a plain `char *` and `-[...]` takes a pointer to the struct body itself; what you pass must **not** start with a length member, or every field is shifted by it without any error. The length read from the wire is not returned there — use `strlen` for a string, or wrap the field in a struct (`[-[...]]`) to get it in a length member.
- **Automatic length members are written only when unpacking.** For `-16s` and `-[...]` inside a struct, packing computes the length for the wire but leaves the member in your struct untouched; unpacking stores the length declared on the wire into it.
- **Count type differs by direction.** Packing takes an `int`; unpacking takes a pointer sized like the prefix — `uint8_t *` for `-`, `uint16_t *` for `=`. Only that many bytes are written, so never pass an `int *`.
- **`num` on a string includes the `\0`.** `16s`, `16S` and `-16s` all hold at most 15 characters; a longer string fails with `PACKF_BE_CUT_OFF`. `num` on anything else is a plain element count.
- **Exceeding a maximum is an error, not a truncation.** `-4d` with a count of 5, or a prefix value too large for 1 or 2 bytes, returns `PACKF_BE_CUT_OFF`.

### Byte order

The wire format is big-endian (network byte order) by default. Two switches change it:

| Switch | Meaning |
|--------|---------|
| `<`    | following fields are **little-endian** on the wire |
| `>`    | following fields are **big-endian** on the wire (default) |

```c
packf(buf, sizeof(buf), "<wd", a, b);          // all little-endian
packf(buf, sizeof(buf), "w <d >w", a, b, c);   // mixed in one message
```

- A switch is a token of its own, placed between fields like a space.
- It affects the values of `w d D f F` and the 2-byte length prefix of `=`. `c`, `s`, `S`, `a` and the 1-byte prefix of `-` have no byte order.
- A struct inherits the byte order in effect at its `[`. A switch inside `[...]` ends at the matching `]`, so every element of a struct array has the same layout.

### `s` vs `S`

`S` differs from `s` in exactly one case: **no prefix, with `num`**. Without `num`, or with a `-`/`=` prefix, they are identical.

| Format | Wire bytes | Bytes in the local struct | Use case |
|--------|-----------|---------------------------|----------|
| `s`    | `strlen + 1` | `strlen + 1` | plain C string |
| `16s`  | always 16 | 16 | fixed-width wire field |
| `16S`  | `strlen + 1` | 16 | compact wire, fixed-size `char field[16]` locally |

A member like `char passwd[30]` occupies 30 bytes in memory, but the actual string is usually shorter. `30S` sends only the real string while still stepping over the full 30-byte member:

```
Struct in memory:   [b][a][z][i][n][g][a][\0][\0]..[\0]   ← 30 bytes
Wire with 30s:      [b][a][z][i][n][g][a][\0][\0]..[\0]   ← always 30 bytes
Wire with 30S:      [b][a][z][i][n][g][a][\0]             ← only 8 bytes
```

Compared with `-30s`, `30S` needs no length member in the struct and ends with `\0` on the wire instead of starting with a length.

### Forward compatibility with `-[...]`

`-[...]` and `=[...]` write the byte length of the struct body in front of it. When unpacking, the reader parses the fields it knows and then jumps to the end of the declared length. So a newer sender can **append** fields to the struct and older readers keep working:

```c
// New sender: struct has an extra trailing field
packf(buf, sizeof(buf), "-[dwd] w", &v2, 0xABCD);
// wire: 0A | 00 00 00 07 | 00 1E | 00 00 00 63 | AB CD
//       len=10  id=7       age=30  score=99      next field

// Old reader: knows only the first two fields
unpackf(buf, len, "-[dw] w", &v1, &next);
// v1.id == 7, v1.age == 30, the 4 unknown bytes are skipped, next == 0xABCD
```

Limits:

- It works one way only. A newer reader fails with `PACKF_OUT_OF_BUF` on older data, because the declared length is too short for its format; check a version field first and pick the format accordingly.
- Fields can only be added at the end, never inserted, removed or retyped.
- The form with `num` (`-3[...]`) is an element count and cannot skip anything. To make array elements extensible, give each its own length: `=10[ -[...] ]`.
- The body is limited to 255 bytes with `-` and 65535 bytes with `=`.

### Untrusted input

`unpackf` checks every read against the source buffer, but it cannot know how large your destinations are. When the data comes from the network:

- **Always give `num` for strings and LV arrays.** Without it the input alone decides how much is written, like `sscanf("%s")`:
  - `s`, `S`, `-s`, `=s` — up to the rest of the buffer, or 65535 bytes plus `\0` for `=s`
  - `-d`, `=d` and the other number types — up to 255 or 65535 elements

  With `num` (`-32s`, `32s`, `32S`, `-64d`), anything larger fails with `PACKF_BE_CUT_OFF`. `-[...]` is safe without `num`: its body is bounded by the format.
- **Size destinations for the maximum.** `-64d` needs room for 64 elements; `-32s` needs 32 bytes.
- **Check the return value.** On failure, outputs may be partially written.
- **Never take the format string from input.** It must match the arguments exactly, as with `printf`; the compiler cannot check it.

---

## API Reference

### Core functions

```c
// Pack variadic arguments into dest according to format.
// Returns total bytes written, or a negative error code on failure.
int packf(void *dest, size_t max, const char *format, ...);

// Unpack binary data from src into variadic pointer arguments.
// Returns total bytes consumed, or a negative error code on failure.
int unpackf(void *src, size_t max, const char *format, ...);
```

### Incremental (cursor-based) variants

These update `*current` and `*left` on success, letting you pack/unpack a message in multiple calls without tracking offsets manually.

```c
int vpackf  (void **current, int *left, const char *format, ...);
int vunpackf(void **current, int *left, const char *format, ...);

// Same as above but accept a va_list instead of variadic arguments.
int vpacka  (void **current, int *left, const char *format, va_list arg);
int vunpacka(void **current, int *left, const char *format, va_list arg);

// Pack/unpack a raw buffer of exactly n bytes.
int vpackn  (void **current, int *left, const void *buf, size_t n);
int vunpackn(void **current, int *left, void *buf, size_t n);
```

### Failure semantics

- **`packf` / `unpackf`:** on failure the destination buffer / output arguments may already be partially written — discard or retry; do not trust the contents.
- **`vpackf` / `vunpackf` / `vpacka` / `vunpacka` / `vpackn` / `vunpackn`:** on failure `*current` and `*left` are left unchanged. The buffer at `*current` (or output args for unpack) may still have been partially written.

There is no global error state — the negative return code alone identifies the failure (thread-safe).

### Error codes

| Code | Value | Meaning |
|------|-------|---------|
| `PACKF_OUT_OF_BUF`    | -1 | Destination/source buffer too small, or bare string has no `\0` in remaining input |
| `PACKF_NOT_FORMAT`    | -2 | Unknown type character in the format string |
| `PACKF_NOT_MATCH`     | -3 | Unmatched `[` / `]` in the format string |
| `PACKF_EXPECT_FORMAT` | -4 | Format string ended where a type was expected |
| `PACKF_BE_CUT_OFF`    | -5 | Data would be truncated (LV/string longer than declared max, or length too large for the prefix) |
| `PACKF_NULL_POINTER`  | -6 | A required pointer argument was NULL |
| `PACKF_INVALID_ARG`   | -7 | Invalid size/length (negative LV length, size > `INT_MAX`, format number overflow, etc.) |

### Helper macros

```c
// Advance a pointer and decrease a remaining-length variable.
PACKF_PASS(ptr, left_len, size);

// Stringify a macro-defined integer constant, to build format strings.
// e.g. with MAX_NAME defined as 64, "-" PACKF_STR(MAX_NAME) "s" is "-64s"
PACKF_STR(macro);
```

---

## Usage Examples

### 1. Scalar values

```c
#include "packf.h"
#include <stdint.h>

char buf[64];

// Pack: 1-byte, 2-byte, 4-byte, 8-byte integers + float + double
int len = packf(buf, sizeof(buf), "c w d D f F",
                (int)0xAB,
                (int)0x1234,
                (int)0xDEADBEEF,
                (int64_t)0x0102030405060708LL,
                3.14f,
                2.718281828);
// len == 1+2+4+8+4+8 == 27

// Unpack back
uint8_t  a; uint16_t b; uint32_t c; uint64_t d; float e; double f_val;
unpackf(buf, len, "c w d D f F", &a, &b, &c, &d, &e, &f_val);
```

### 2. Strings

```c
char buf[64];

// Variable-length string (strlen + 1 bytes on the wire, including '\0')
int len = packf(buf, sizeof(buf), "s", "hello");
// len == 6  ("hello\0")

// Fixed-width 16-byte string field (padded with '\0' if shorter)
len = packf(buf, sizeof(buf), "16s", "hello");
// len == 16

// LV string: 1-byte length prefix + string bytes (no '\0' on wire)
len = packf(buf, sizeof(buf), "-32s", "hello");
// len == 6  (1 byte length=5, then "hello")

char out[32];
unpackf(buf, len, "-32s", out);
// out == "hello" ('\0' terminated)
```

### 3. Arrays

```c
char buf[256];
int32_t arr[] = {10, 20, 30, 40, 50};

// Fixed array: exactly 5 elements, no prefix
int len = packf(buf, sizeof(buf), "5d", arr);
// len == 5*4 == 20

// LV array: 1-byte element count + array data (max 64 elements)
len = packf(buf, sizeof(buf), "-64d", 5, arr);
// len == 1 + 5*4 == 21

int32_t out[64];
uint8_t count;                 // uint8_t for '-', uint16_t for '='
unpackf(buf, len, "-64d", &count, out);
// count == 5, out[0..4] == {10,20,30,40,50}
```

### 4. Structs

Structs must be declared inside `#pragma pack(1)` to ensure no alignment padding.

```c
#include "packf.h"
#include <string.h>

char buf[256];

#pragma pack(1)
struct Point {
    int32_t  x;
    int32_t  y;
};

struct Packet {
    uint16_t  version;
    struct Point origin;
    float     value;
};
#pragma pack()

struct Packet pkt = { .version = 1, .origin = {100, 200}, .value = 3.14f };

// Pack the whole struct in one call
int len = packf(buf, sizeof(buf), "[w [dd] f]", &pkt);

// Unpack into another struct
struct Packet out;
unpackf(buf, len, "[w [dd] f]", &out);
```

### 5. Nested structs with LV arrays

This mirrors the pattern used in `test.c`:

```c
#pragma pack(1)
struct User {
    uint32_t uid;
    uint8_t  name_len;     /* for -64s: computed when packing, filled in when unpacking */
    char     name[64];
    char     passwd[32];
};

struct Message {
    uint32_t    type;
    uint16_t    user_count;    /* for =10[...]: set it before packing */
    struct User users[10];
};
#pragma pack()

struct Message msg = {0};
msg.type = 1;
msg.user_count = 2;
msg.users[0].uid = 1001;
strcpy(msg.users[0].name,   "alice");
strcpy(msg.users[0].passwd, "s3cr3t");
/* ... fill users[1] ... */

char buf[1024];
// =10[...]: 2-byte user count + up to 10 user structs
// -64s:     1-byte length prefix + name bytes, no '\0' on wire (max 63 chars)
// 32S:      compact wire (strlen+1 bytes); steps over the full char passwd[32] locally
int len = packf(buf, sizeof(buf),
                "[ d  =10[d -64s 32S]  ]",
                &msg);

struct Message out = {0};
unpackf(buf, len, "[ d  =10[d -64s 32S]  ]", &out);
// out.user_count == 2, out.users[0].name_len == 5
```

### 6. Incremental packing with `vpackf`

Use `vpackf`/`vunpackf` to build or parse a message across multiple calls without manually tracking the write position.

```c
char buf[256];
void *pos = buf;
int  left = sizeof(buf);

vpackf(&pos, &left, "w", (int)0x0001);   // message type
vpackf(&pos, &left, "d", (int)42);        // sequence number
vpackf(&pos, &left, "-32s", "hello");     // LV string payload

int total = sizeof(buf) - left;  // total bytes written
```

### 7. Error handling

```c
char small[4];
int ret = packf(small, sizeof(small), "D", (int64_t)0x123456789ABCLL);
if (ret < 0) {
    // ret == PACKF_OUT_OF_BUF (-1) — see error codes above
}
```

---

## Building

packf is a single translation unit — just add `packf.c` and `packf.h` to your project.

```sh
# Compile the test program
gcc -o test test.c packf.c

# Run it
./test
```

> **Note:** packf uses `<endian.h>` to detect the host byte order, which is available on Linux. On other platforms you may need to provide an equivalent header.
