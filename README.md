# packf

A lightweight binary serialization library for C/C++ that converts data structures to and from network byte order using a `printf`-style format string.

**Author:** Haipo Yang &lt;yang@haipo.me&gt; | **License:** Public Domain

---

## Table of Contents

- [Overview](#overview)
- [Advantages](#advantages)
- [API Reference](#api-reference)
- [Format String Syntax](#format-string-syntax)
- [Usage Examples](#usage-examples)
- [Building](#building)

---

## Overview

packf provides `packf`/`unpackf` and their variants to serialize and deserialize C data — scalars, arrays, strings, and packed structs — into a portable big-endian binary wire format. The interface mirrors the familiarity of `sprintf`/`sscanf`: a single format string drives the entire operation.

```c
char buf[256];

// Pack: host → network byte order
int len = packf(buf, sizeof(buf), "wd", 0x1234, 0xDEADBEEF);

// Unpack: network → host byte order
uint16_t a; uint32_t b;
unpackf(buf, len, "wd", &a, &b);
```

---

## Advantages

- **Zero dependencies** — a single `.c`/`.h` pair, no external libraries required.
- **Familiar API** — format-string driven, just like `sprintf`/`sscanf`.
- **Comprehensive type support** — 8/16/32/64-bit integers, float, double, strings, byte arrays, and nested structs.
- **LV (Length–Value) fields** — built-in support for variable-length arrays and strings with a 1-byte or 2-byte length prefix, eliminating manual length bookkeeping.
- **Automatic byte-swap** — all multi-byte types are converted to/from big-endian automatically. No manual `htons`/`ntohl` calls.
- **Struct-aware** — can pack/unpack directly from `#pragma pack(1)` structs, keeping your code in sync with the wire format.
- **Incremental packing** — `vpackf`/`vunpackf` variants update the current position and remaining length automatically, making multi-step message assembly easy.
- **Public domain** — use freely in any project, commercial or otherwise.

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
int vpackn  (void **current, int *left, void *buf, size_t n);
int vunpackn(void **current, int *left, void *buf, size_t n);
```

### Error reporting

```c
// Points to the format character where the last error occurred.
extern char *packf_error_format;

// Set to non-zero to print error messages to stderr automatically.
extern int packf_print_error;
```

### Error codes

| Code | Value | Meaning |
|------|-------|---------|
| `PACKF_OUT_OF_BUF`    | -1 | Destination/source buffer too small |
| `PACKF_NOT_FORMAT`    | -2 | Unknown format character |
| `PACKF_NOT_MATCH`     | -3 | Unmatched `[` / `]` in format string |
| `PACKF_EXPECT_FORMAT` | -4 | Format string ended unexpectedly |
| `PACKF_BE_CUT_OFF`    | -5 | Data would be truncated |
| `PACKF_NULL_POINTER`  | -6 | NULL pointer argument |

### Helper macros

```c
// Advance a pointer and decrease a remaining-length variable.
PACK_PASS(ptr, left_len, size);

// Stringify a macro-defined integer constant.
// e.g. STR_OF(MAX_LEN) where MAX_LEN=1024 expands to "1024"
STR_OF(macro);

// Return the negated line number if x is negative (useful for debugging).
NEG_RET_LN(x);
```

---

## Format String Syntax

```
format token: [-=][num]type
```

| Part | Meaning |
|------|---------|
| `-`  | LV field with a **1-byte** length prefix |
| `=`  | LV field with a **2-byte** length prefix |
| `num`| Array count / max length (optional) |
| `type` | One of the type characters below |

Spaces in the format string are ignored and can be used for readability.

### Type characters

| Type | Wire size | C type |
|------|-----------|--------|
| `c`  | 1 byte    | `int8_t` / `uint8_t` |
| `w`  | 2 bytes   | `int16_t` / `uint16_t` |
| `d`  | 4 bytes   | `int32_t` / `uint32_t` |
| `D`  | 8 bytes   | `int64_t` / `uint64_t` |
| `f`  | 4 bytes   | `float` |
| `F`  | 8 bytes   | `double` |
| `s`  | variable  | null-terminated string (pads remaining bytes with `\0` when `num` given) |
| `S`  | variable  | null-terminated string (no `\0` padding when shorter than `num`) |
| `a`  | variable  | zero byte(s) / padding |
| `[`…`]` | variable | struct (argument must be a `#pragma pack(1)` struct pointer) |

### Format rules

**No prefix (`[-=]` absent)**

| Format | Behaviour |
|--------|-----------|
| `type` | Single scalar value |
| `Ntype` | Array of N elements; argument is a pointer |
| `s` | Pack/unpack string using `strlen + 1` bytes |
| `Ns` | Fixed N-byte string field; pads with `\0` if shorter |
| `NS` | Like `Ns` but does not pad with `\0` |

**With LV prefix (`-` or `=`)**

| Format | Behaviour |
|--------|-----------|
| `-type` | 1-byte length + value (length supplied as an int argument when packing) |
| `=type` | 2-byte length + value |
| `-Ntype` | 1-byte length + up to N elements; N is the declared maximum |
| `-Ns` | 1-byte length + string bytes (no `\0` on wire); N is the max string length including `\0` |
| `-[…]` | 1-byte length + struct bytes (length computed automatically) |
| `=N[…]` | 2-byte element count + N struct instances |

> **Structs inside structs:** When an LV field (`-s`, `=N[…]`, etc.) appears inside a `[…]` block, the struct definition must contain the corresponding length member (to stay in sync with the protocol layout), but the member's value is read/written automatically — you do not pass it as an argument.

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
// out == "hello"
```

### 3. Arrays

```c
char buf[256];
int32_t arr[] = {10, 20, 30, 40, 50};

// LV array: 1-byte element count + array data (max 64 elements)
int len = packf(buf, sizeof(buf), "-64d", 5, arr);
// len == 1 + 5*4 == 21

int32_t out[64];
uint8_t count;
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
    uint8_t  name_len;
    char     name[64];
    char     passwd[32];
};

struct Message {
    uint32_t    type;
    uint16_t    user_count;    /* written by =10[...] automatically */
    struct User users[10];
};
#pragma pack()

struct Message msg = {0};
msg.type = 1;
msg.user_count = 2;
msg.users[0].uid = 1001;
msg.users[0].name_len = strlen("alice");
strcpy(msg.users[0].name,   "alice");
strcpy(msg.users[0].passwd, "s3cr3t");
/* ... fill users[1] ... */

char buf[1024];
// =10[...]: 2-byte count of users, up to 10 structs
// -64s:     1-byte name length + name bytes (max 64 chars including '\0')
// 32S:      fixed passwd field, no '\0' padding
int len = packf(buf, sizeof(buf),
                "[ d  =10[d -64s 32S]  ]",
                &msg);

struct Message out = {0};
unpackf(buf, len, "[ d  =10[d -64s 32S]  ]", &out);
```

### 6. Incremental packing with `vpackf`

Use `vpackf`/`vunpackf` to build or parse a message across multiple calls without manually tracking the write position.

```c
char buf[256];
void *pos = buf;
int  left = sizeof(buf);

vpackf(&pos, &left, "w", (int)0x0001);   // message type
vpackf(&pos, &left, "d", (int)42);        // payload length placeholder
vpackf(&pos, &left, "-32s", "hello");     // LV string payload

int total = sizeof(buf) - left;  // total bytes written
```

### 7. Error handling

```c
packf_print_error = 1;   // print errors to stderr automatically

char small[4];
int ret = packf(small, sizeof(small), "D", (int64_t)0x123456789ABCLL);
if (ret < 0) {
    // ret == PACKF_OUT_OF_BUF (-1)
    // packf_error_format points to "D" in the format string
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

> **Note:** packf uses `<endian.h>` and `<byteswap.h>`, which are available on Linux. On other platforms you may need to provide equivalent byte-swap primitives.
