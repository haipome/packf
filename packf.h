/*
 * Binary serialization library for c/c++
 *
 * By Haipo Yang <yang@haipo.me>, 2012, 2013.
 *
 * This code is in the public domain.
 * You may use this code any way you wish, private, educational,
 * or commercial. It's free.
 */

# ifndef _PACKF_H_
# define _PACKF_H_

# include <stdint.h>
# include <stddef.h>
# include <stdarg.h>

# ifdef  __cplusplus
extern "C"
{
# endif

/*
 * packf/vpackf and their counterparts unpackf/vunpackf provide formatting
 * serialization and deserialization functions similar to sprintf and sscanf.
 * By passing a format string that describes the data layout, various data
 * types (including structs and arrays) can be conveniently converted between
 * host byte order and network byte order for network transmission.
 *
 * format: [-=][num]type     [] means optional
 *
 * ---------------------------------------------------------------
 *  type   |    means
 * ---------------------------------------------------------------
 *    s    |    string ('\0' terminated)
 *    a    |    null byte ('\0')
 *    c    |    char   (int8_t  | uint8_t)
 *    w    |    word   (int16_t | uint16_t)
 *    d    |    dword  (int32_t | uint32_t)
 *    D    |    ddword (int64_t | uint64_t) (c99)
 *    f    |    float  (4 bytes)
 *    F    |    double (8 bytes)
 *    [    |    struct begin
 * --------------------------------------------------------------
 *    ]    |    struct end
 *  space  |    spaces may be used to make the format string more readable,
 *         |    but not between -/= num and type
 *    <    |    switch the wire byte order to little endian
 *    >    |    switch the wire byte order to big endian (the default)
 * --------------------------------------------------------------
 *
 * byte order:
 *    The wire format is big endian (network byte order) unless < is used.
 *    < and > are tokens of their own, placed between fields like a space;
 *    they apply to all following fields: the values of w d D f F and the
 *    2-byte length prefix of =. (c, s, a and the 1-byte prefix of - have
 *    no byte order; length fields in a local struct stay in host order.)
 *    A struct inherits the byte order in effect at its [, and a switch
 *    inside [] ends at the matching ].
 *    example: packf(buf, sizeof(buf), "<wd", a, b);       all little endian
 *             packf(buf, sizeof(buf), "w <d >w", a, b, c); mixed
 *
 * note:
 * 1. When - or = is present, it denotes an LV (len + value) field;
 *    num, if present, represents the maximum count of values.
 *      1): - means the length field is 1 byte; = means 2 bytes.
 *
 *      2): For types acwdDfF, a len argument must be supplied before the value.
 *          example: char buf[1024]; int array[64];
 *          packf(buf, sizeof(buf), "-64d", 5, array);
 *          Here 5 is the number of elements in array, 64 is the max length.
 *          This packs 21 bytes into buf (1-byte length + 20 bytes of data),
 *          and returns 21.
 *          When unpacking, the len argument is a pointer whose type matches
 *          the length prefix: uint8_t * for -, uint16_t * for =. Only that
 *          many bytes are written, so do NOT pass an int *.
 *          example: uint8_t n; unpackf(buf, len, "-64d", &n, array);
 *
 *      3): For type s, no explicit len is needed; strlen determines the length.
 *          example: packf(buf, sizeof(buf), "-128s", "damonyang");
 *          Here 128 is the maximum string length (including the '\0' terminator).
 *          This packs 10 bytes into buf (1-byte length + 9 character bytes,
 *          not including the '\0' terminator), and returns 10.
 *          **When s appears inside a struct, a len field must be declared before
 *          it in the struct definition but does not need to be assigned a value
 *          (to keep the struct definition consistent with the protocol format).
 *
 *      4): For struct type [, if num is absent, no len needs to be specified;
 *          the length is determined by the actual packed struct size. Like s,
 *          when this construct appears inside a struct, a len field must be
 *          declared before it but does not need to be assigned.
 *          If num is present, the behavior is the same as 2).
 *
 *      5): The len field of 3) and 4) exists only inside a struct. There,
 *          packing computes the length for the wire and leaves the len
 *          field untouched; unpacking stores the length read from the wire
 *          into it (in host byte order).
 *          At the top level of the format there is no len field: "-128s"
 *          takes a char *, and "-[...]" takes a pointer to the struct body
 *          itself, which must NOT begin with a len field (every field would
 *          be shifted by it, without any error). The length read from the
 *          wire is not returned to the caller in that case.
 *          example: struct body { int32_t x; };
 *                   struct outer { uint8_t len; struct body b; };
 *                   unpackf(buf, n, "-[d]", &body);     length not returned
 *                   unpackf(buf, n, "[-[d]]", &outer);  outer.len is set
 *
 * 2. When neither - nor = is present:
 *      1): For types acwdD[fF, when num is present it denotes an array;
 *          the argument should be an array or pointer.
 *
 *      2): For s, when num is present it specifies the field width; if the
 *          actual string is shorter than num, the remaining bytes are filled
 *          with '\0'. If num is absent, the length is strlen + 1.
 *          S differs from s: when num is present and the string is shorter
 *          than num, S does NOT pad with '\0'.
 *
 *      3): WARNING: when unpacking, a string without num ("s", "-s", "=s")
 *          is copied with no limit on the destination size; the number of
 *          bytes written is decided by the input data alone (up to 65536
 *          for "=s"), like sscanf "%s". When the input is not trusted,
 *          always give num ("-32s", "32s", "32S") so that an oversized
 *          string is rejected with PACKF_BE_CUT_OFF.
 *          The same holds for an LV array of cwdDfF without num ("-d",
 *          "=d"): the input decides the element count, up to 255 for - and
 *          65535 for =. Give num ("-64d") to bound it.
 *          An LV struct without num ("-[...]") is not affected: its body
 *          is bounded by the format.
 *
 * 3. [] denotes a struct; the argument should be a pointer to the struct.
 *      1): The struct must be defined between # pragma pack(1) and
 *          # pragma pack()!
 *
 *      2): Structs support nesting, i.e. a struct may contain other structs.
 */

/*
 * Error codes (negative return values from packf / unpackf / vpack* / …)
 *
 *   PACKF_OUT_OF_BUF    (-1)  Destination or source buffer too small;
 *                             or a bare string field has no '\0' within
 *                             the remaining input.
 *   PACKF_NOT_FORMAT    (-2)  Unknown type character in the format string.
 *   PACKF_NOT_MATCH     (-3)  Unmatched '[' / ']' in the format string.
 *   PACKF_EXPECT_FORMAT (-4)  Format string ended where a type was expected.
 *   PACKF_BE_CUT_OFF    (-5)  Data would be truncated (e.g. LV length or
 *                             string longer than the declared maximum).
 *   PACKF_NULL_POINTER  (-6)  A required pointer argument was NULL.
 *   PACKF_INVALID_ARG   (-7)  Invalid size or length: negative LV length,
 *                             size / remaining length > INT_MAX or < 0,
 *                             format count overflow, array size overflow.
 *
 * Callers should check the return value against these codes. There is no
 * global error state — the return code alone is sufficient and thread-safe.
 */
enum
{
    PACKF_OUT_OF_BUF    = -1,
    PACKF_NOT_FORMAT    = -2,
    PACKF_NOT_MATCH     = -3,
    PACKF_EXPECT_FORMAT = -4,
    PACKF_BE_CUT_OFF    = -5,
    PACKF_NULL_POINTER  = -6,
    PACKF_INVALID_ARG   = -7,
};

/*
 * Function: packf : pack format
 * Description: Packs the variadic arguments into network byte order binary
 *              data according to the layout described by format.
 * Parameters:
 *       dest:   destination buffer address
 *       max:    length of the buffer pointed to by dest
 *       format: format string corresponding to the variadic arguments
 *       ...:    variadic arguments corresponding to format
 * Return value:
 *      >= 0 : success, returns the total number of bytes packed
 *      < 0  : failure
 * Failure semantics:
 *      On failure, dest may have been partially written; do not rely on
 *      its contents. Treat the operation as failed and discard/retry.
 */
extern int packf(void *dest, size_t max, char const *format, ...);

/*
 * Function: unpackf : unpack format
 * Description: Unpacks network byte order binary data into host byte order
 *              according to the layout described by format.
 * Parameters:
 *      src:    start address of the network byte order binary data
 *      max:    length of the binary data pointed to by src
 *      format: format string describing the binary data layout
 *      ...:    variadic arguments; each should be a pointer
 * Return value:
 *      >= 0 : success, returns the total number of bytes unpacked
 *      < 0  : failure
 * Failure semantics:
 *      On failure, output arguments may have been partially written; do
 *      not rely on their contents.
 */
extern int unpackf(void *src, size_t max, char const *format, ...);

/*
 * Function: vpackf : variable pack format
 * Description: Like packf, but accepts a pointer to the current buffer
 *              position and a pointer to the remaining length, and updates
 *              them on success.
 * Parameters:
 *      current: pointer to the current position in the buffer
 *      left:    pointer to the remaining length of the buffer
 *      format:  format string corresponding to the variadic arguments
 *      ...:     variadic arguments corresponding to format
 * Return value:
 *      >= 0 : success, returns the total number of bytes packed
 *             *current: updated to the next available address in the buffer
 *             *left:    updated remaining buffer length
 *      < 0  : failure — *current and *left are left unchanged
 * Failure semantics:
 *      On failure the cursor is not advanced, but the buffer at *current
 *      may have been partially written; do not rely on those bytes.
 */
extern int vpackf(void **current, int *left, char const *format, ...);

/*
 * Function: vunpackf : variable unpack format
 * Description: Like unpackf, but accepts a pointer to the current network
 *              data position and a pointer to the remaining length, and
 *              updates them on success.
 * Parameters:
 *      current: pointer to the current position in the network data
 *      left:    pointer to the remaining length of the network data
 *      format:  format string describing the network data layout
 *      ...:     variadic arguments; each should be a pointer
 * Return value:
 *      >= 0 : success, returns the total number of bytes unpacked
 *             *current: updated to the next address in the network data
 *             *left:    updated remaining network data length
 *      < 0  : failure — *current and *left are left unchanged
 * Failure semantics:
 *      On failure the cursor is not advanced, but output arguments may
 *      have been partially written; do not rely on their contents.
 */
extern int vunpackf(void **current, int *left, char const *format, ...);

/*
 * Function: vpackn : variable pack buffer of n length
 * Description: Packs a fixed-length buffer into the output.
 * Parameters:
 *      current: pointer to the current position in the buffer
 *      left:    pointer to the remaining length of the buffer
 *      buf:     address of the source buffer
 *      n:       length of the source buffer
 * Return value:
 *      >= 0 : success, returns the total number of bytes packed
 *             *current: updated to the next available address in the buffer
 *             *left:    updated remaining buffer length
 *      < 0  : failure — *current and *left are left unchanged
 */
extern int vpackn(void **current, int *left, void const *buf, size_t n);

/*
 * Function: vunpackn : variable unpack buffer of n length
 * Description: Unpacks a fixed-length buffer from the network data.
 * Parameters:
 *      current: pointer to the current position in the network data
 *      left:    pointer to the remaining length of the network data
 *      buf:     address of the destination buffer
 *      n:       length of the buffer
 * Return value:
 *      >= 0 : success, returns the total number of bytes unpacked
 *             *current: updated to the next address in the network data
 *             *left:    updated remaining network data length
 *      < 0  : failure — *current and *left are left unchanged
 */
extern int vunpackn(void **current, int *left, void *buf, size_t n);

extern int vpacka(void **current, int *left, char const *format, va_list arg);
extern int vunpacka(void **current, int *left, char const *format, va_list arg);

/*
 * Stringify a macro-defined number.
 * example:
 * # define MAX_LEN 1024
 * PACKF_STR(MAX_LEN) expands to "1024"
 */
# define PACKF_STR_(s) #s
# define PACKF_STR(s) PACKF_STR_(s)

/* Advance pointer by size bytes and decrease remaining length accordingly */
# define PACKF_PASS(ptr, left_len, size) do {       \
    (ptr) = (char*)(ptr) + (size);                  \
    (left_len) = (int)(left_len) - (size);          \
} while (0)

# ifdef  __cplusplus
}
# endif

# endif
