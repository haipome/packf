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
 * --------------------------------------------------------------
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
 * 3. [] denotes a struct; the argument should be a pointer to the struct.
 *      1): The struct must be defined between # pragma pack(1) and
 *          # pragma pack()!
 *
 *      2): Structs support nesting, i.e. a struct may contain other structs.
 */

enum
{
    PACKF_OUT_OF_BUF    = -1,
    PACKF_NOT_FORMAT    = -2,
    PACKF_NOT_MATCH     = -3,
    PACKF_EXPECT_FORMAT = -4,
    PACKF_BE_CUT_OFF    = -5,
    PACKF_NULL_POINTER  = -6,
};

/*
 * On error, if packf_error_format is not NULL, it points to the position
 * in the format string where the error occurred, which can be printed to
 * help locate the error.
 */
extern char *packf_error_format;

/*
 * If packf_print_error is non-zero, error messages are printed to stderr
 * when an error occurs.
 */
extern int packf_print_error;

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
 *      < 0  : failure
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
 *      < 0  : failure
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
 *      < 0  : failure
 */
extern int vpackn(void **current, int *left, void *buf, size_t n);

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
 *      < 0  : failure
 */
extern int vunpackn(void **current, int *left, void *buf, size_t n);

extern int vpacka(void **current, int *left, char const *format, va_list arg);
extern int vunpacka(void **current, int *left, char const *format, va_list arg);

/* Return the negated line number if the result is negative */
# ifndef NEG_RET_LN
# define NEG_RET_LN(x) do { if ((x) < 0) return -__LINE__; } while (0)
# endif

/*
 * Stringify a macro-defined number.
 * example:
 * # define MAX_LEN 1024
 * STR_OF(MAX_LEN) expands to "1024"
 */
# ifndef STR_OF
# define __STR_OF__(s) #s
# define STR_OF(s) __STR_OF__(s)
# endif

/* Advance pointer by size bytes and decrease remaining length accordingly */
# define PACK_PASS(ptr, left_len, size) do {        \
    (ptr) = (char*)(ptr) + (size);                  \
    (left_len) = (int)(left_len) - (size);          \
} while (0)

# ifdef  __cplusplus
}
# endif

# endif
