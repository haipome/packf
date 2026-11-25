/*
 * Binary serialization library for c/c++
 *
 * By Haipo Yang <yang@haipo.me>, 2012, 2013.
 *
 * This code is in the public domain. 
 * You may use this code any way you wish, private, educational,
 * or commercial. It's free.
 */

# ifndef _GNU_SOURCE
# define _GNU_SOURCE /* for strnlen */
# endif

# include <string.h>
# include <stdarg.h>
# include <limits.h>

# include "packf.h"

# define FROM_ARG 1
# define FROM_PTR 2

/* Consume `need` bytes from remaining length `x`, rejecting sizes that
 * do not fit in a non-negative int (API returns int byte counts). */
# define IF_LESS(x, n) do {                                             \
    size_t __need = (size_t)(n);                                        \
    if (__need > (size_t)INT_MAX || (x) < 0 || (size_t)(x) < __need)    \
        return PACKF_OUT_OF_BUF;                                  \
    (x) -= (int)__need;                                                 \
} while (0)

# define CHECK_ARRAY_OFFSET(type1, array_size, offset_var) do {         \
    if ((array_size) < 0)                                               \
        return PACKF_INVALID_ARG;                                 \
    if ((array_size) > 0 &&                                             \
        (size_t)(array_size) > (size_t)INT_MAX / sizeof(type1))         \
        return PACKF_INVALID_ARG;                                 \
    (offset_var) = (int)(sizeof(type1) * (size_t)(array_size));         \
} while (0)

# define NEG_RET(x) do {                                                \
    int __ret = (x);                                                    \
    if (__ret < 0) return __ret;                                        \
} while (0)

# include <endian.h>

/* Byte order of the data on the wire. Big endian (network byte order) is
 * the default; '<' and '>' in the format string switch it. */
# define ORDER_BE 0
# define ORDER_LE 1

# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define HOST_ORDER ORDER_LE
# else
#  define HOST_ORDER ORDER_BE
# endif

# ifndef __FLOAT_WORD_ORDER
# define __FLOAT_WORD_ORDER __BYTE_ORDER
# endif

# if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
#  define HOST_FLOAT_ORDER ORDER_LE
# else
#  define HOST_FLOAT_ORDER ORDER_BE
# endif

/* Copy n (<= 8) bytes with the byte order reversed. Works on raw bytes, so
 * a swapped float/double never exists as a floating-point value, and dst
 * may be unaligned or equal to src. */
static inline void __copy_rev(void *dst, void const *src, size_t n)
{
    unsigned char tmp[8];
    unsigned char const *s = src;
    size_t i;

    for (i = 0; i < n; ++i)
        tmp[i] = s[n - 1 - i];
    memcpy(dst, tmp, n);
}

/* Unaligned-safe load/store. Wire buffers (and #pragma pack(1) structs)
 * are not guaranteed to satisfy natural alignment for multi-byte types. */
static inline void __store_u8(void *dst, uint8_t v)
{
    memcpy(dst, &v, sizeof(v));
}

static inline uint8_t __load_u8(void const *src)
{
    uint8_t v;
    memcpy(&v, src, sizeof(v));
    return v;
}

static inline void __store_u16(void *dst, uint16_t v)
{
    memcpy(dst, &v, sizeof(v));
}

static inline uint16_t __load_u16(void const *src)
{
    uint16_t v;
    memcpy(&v, src, sizeof(v));
    return v;
}

/* 2-byte LV length prefix on the wire, in the given byte order. */
static inline void __store_u16_wire(void *dst, uint16_t v, int order)
{
    unsigned char *p = dst;

    if (order == ORDER_LE)
    {
        p[0] = (unsigned char)(v & 0xFF);
        p[1] = (unsigned char)(v >> 8);
    }
    else
    {
        p[0] = (unsigned char)(v >> 8);
        p[1] = (unsigned char)(v & 0xFF);
    }
}

static inline uint16_t __load_u16_wire(void const *src, int order)
{
    unsigned char const *p = src;

    if (order == ORDER_LE)
        return (uint16_t)(p[0] | (p[1] << 8));
    return (uint16_t)((p[0] << 8) | p[1]);
}

# define __ISDIGIT(c) ((c) >= '0' && (c) <= '9')

/* Parse n decimal digits into *out. Returns 0, or -1 on overflow. */
static inline int __parse_uint(char const *s, size_t n, int *out)
{
    int r = 0;
    size_t i;

    for (i = 0; i < n; ++i)
    {
        int digit = s[i] - '0';

        if (r > (INT_MAX - digit) / 10)
            return -1;
        r = r * 10 + digit;
    }

    *out = r;
    return 0;
}

# define PARSE_NUM(str_num, end, num_var, empty_val) do {               \
    if ((str_num) == (end))                                             \
        (num_var) = (empty_val);                                        \
    else if (__parse_uint((str_num),                                    \
                          (size_t)((end) - (str_num)), &(num_var)) < 0) \
        return PACKF_INVALID_ARG;                                 \
} while (0)

static int __struct_len_locale(char const *format)
{
    int len = 0, u_len, num, bracket_stack;
    char const *f = format, *str_num;
    char type;

    while (*f)
    {
        if (*f == ' ' || *f == '<' || *f == '>')
        {
            ++f;
            continue;
        }

        if (*f == '-')
        {
            len += 1;
            ++f;
        }
        else if (*f == '=')
        {
            len += 2;
            ++f;
        }

        str_num = f;
        while (__ISDIGIT(*f))
            ++f;
        PARSE_NUM(str_num, f, num, 1);

        type = *(f++);
        if (!type)
            return PACKF_EXPECT_FORMAT;

        switch (type)
        {
            case 'a':
            case 'c':
            case 's':
            case 'S':
                u_len = 1;
                break;
            case 'w':
                u_len = 2;
                break;
            case 'd':
            case 'f':
                u_len = 4;
                break;
            case 'D':
            case 'F':
                u_len = 8;
                break;
            case '[':
                NEG_RET(u_len = __struct_len_locale(f));
                bracket_stack = 1;
                while (bracket_stack)
                {
                    if (!(*f))
                        return PACKF_NOT_MATCH;
                    else if (*f == '[')
                        ++bracket_stack;
                    else if (*f == ']')
                        --bracket_stack;

                    ++f;
                }

                break;
            case ']':
                return len;
            default:
                return PACKF_NOT_FORMAT;
        }

        len += num * u_len;
    }

    return  len;
}

/* Largest length an LV prefix of lv_type bytes can encode. */
# define LV_MAX(lv_type) ((lv_type) == 1 ? 0xFF : 0xFFFF)

/* Write the LV length prefix at des (space must already be reserved) and
 * advance des. Fails if lv_len does not fit in the prefix. */
# define PUT_LV_LEN(des) do {                                           \
    if (lv_len > LV_MAX(lv_type)) return PACKF_BE_CUT_OFF;              \
    if (lv_type == 1) __store_u8(des, (uint8_t)lv_len);                 \
    else __store_u16_wire(des, (uint16_t)lv_len, order);                        \
    (des) = (char *)(des) + lv_type;                                    \
} while (0)

/* Reserve space for the LV length prefix, then write it. */
# define SET_LV_LEN(des) do {                                           \
    IF_LESS(*left_len, lv_type);                                        \
    PUT_LV_LEN(des);                                                    \
} while (0)

# define SET_LV(des, src) do {                                       \
    if (lv_type)                                                        \
    {                                                                   \
        if (from == FROM_ARG) lv_len = va_arg(va, int);                 \
        else                                                            \
        {                                                               \
            if (lv_type == 1) lv_len = (int)__load_u8(src);             \
            else lv_len = (int)__load_u16(src);                         \
            (src) = (char *)(src) + lv_type;                            \
        }                                                               \
        if (lv_len < 0) return PACKF_INVALID_ARG;                 \
        if (num != -1 && lv_len > num) return PACKF_BE_CUT_OFF;   \
        SET_LV_LEN(des);                                                \
    }                                                                   \
} while (0)

# define DO_PACKF(type1, type2, host_order) do {                        \
    type1 __tmp;                                                        \
    int __swap = (sizeof(type1) > 1 && order != (host_order));          \
    SET_LV(*net, *locale);                                              \
    if (num == -1 && !lv_type)                                          \
    {                                                                   \
        IF_LESS(*left_len, sizeof(type1));                              \
        if (from == FROM_ARG)                                           \
        {                                                               \
            __tmp = (type1)(va_arg(va, type2));                         \
            src = (char *)&__tmp;                                       \
        }                                                               \
        else                                                            \
        {                                                               \
            src = *locale;                                              \
            *locale = (char *)*locale + sizeof(type1);                  \
        }                                                               \
        if (__swap)                                                     \
            __copy_rev(*net, src, sizeof(type1));                       \
        else                                                            \
            memcpy(*net, src, sizeof(type1));                           \
        *net = (char *)*net + sizeof(type1);                            \
    }                                                                   \
    else                                                                \
    {                                                                   \
        if (from == FROM_ARG)                                           \
            src = va_arg(va, char *);                                   \
        else                                                            \
            src = *locale;                                              \
        des = *net;                                                     \
        array_size = lv_type ? lv_len : num;                            \
        CHECK_ARRAY_OFFSET(type1, array_size, offset);                  \
        if (offset)                                                     \
        {                                                               \
            IF_LESS(*left_len, offset);                                 \
            if (__swap)                                                 \
            {                                                           \
                for (i = 0; i < array_size; i++)                        \
                    __copy_rev(des + i * sizeof(type1),                 \
                               src + i * sizeof(type1), sizeof(type1)); \
            }                                                           \
            else                                                        \
                memcpy(des, src, (size_t)offset);                       \
            *net = (char *)*net + offset;                               \
        }                                                               \
        if (from == FROM_PTR)                                           \
        {                                                               \
            if (lv_type && num != -1)                                   \
                *locale = (char *)*locale + num * sizeof(type1);        \
            else                                                        \
                *locale = (char *)*locale + offset;                     \
        }                                                               \
    }                                                                   \
} while (0)

static int __packf(void **net, int *left_len, char const *format, \
        int from, va_list va, void **locale, int depth, int order)
{
    int buf_len = *left_len;
    int num, i, array_size, offset, bracket_stack, lv_len = 0;
    char const *f = format, *str_num;
    char *src, *des;
    char type, lv_type;
    int struct_len_locale = 0;
    void *struct_start_locale, *struct_start_net, *p_struct_len;

    while (*f)
    {
        if (*f == ' ')
        {
            ++f;
            continue;
        }

        /* Byte order switch: applies to the following fields, and to
         * nested structs, but does not leak out of the enclosing []. */
        if (*f == '<' || *f == '>')
        {
            order = (*f == '<') ? ORDER_LE : ORDER_BE;
            ++f;
            continue;
        }

        if (*f == '-')
        {
            lv_type = 1;
            ++f;
        }
        else if (*f == '=')
        {
            lv_type = 2;
            ++f;
        }
        else
        {
            lv_type = 0;
        }

        str_num = f;
        while (__ISDIGIT(*f))
            ++f;
        PARSE_NUM(str_num, f, num, -1);

        type = *(f++);
        if (!type)
            return PACKF_EXPECT_FORMAT;

        switch (type)
        {
            case '[':
                if (lv_type && num == -1)
                {
                    if (from == FROM_ARG)
                        *locale = va_arg(va, char *);

                    /* Reserve the length prefix before packing the body,
                     * so the body cannot be written past the buffer end. */
                    IF_LESS(*left_len, lv_type);
                    p_struct_len = *net;
                    struct_start_net = *net = (char *)*net + lv_type;
                    if (from == FROM_PTR)
                        *locale = (char *)*locale + lv_type;
                    NEG_RET(__packf(net, left_len, f, FROM_PTR,
                                NULL, locale, depth + 1, order));
                    lv_len = (char *)*net - (char *)struct_start_net;
                    PUT_LV_LEN(p_struct_len);
                }
                else
                {
                    SET_LV(*net, *locale);
                    if (from == FROM_ARG)
                        *locale = va_arg(va, char *);

                    array_size = lv_type ? lv_len : (num == -1 ? 1 : num);
                    for (i = 0; i < array_size; i++)
                    {
                        struct_start_locale = *locale;
                        NEG_RET(__packf(net, left_len, f, FROM_PTR,
                                    NULL, locale, depth + 1, order));
                        struct_len_locale = (char *)*locale -
                            (char *)struct_start_locale;
                    }
                    if (lv_type && from == FROM_PTR)
                    {
                        if (array_size == 0)
                            NEG_RET(struct_len_locale = __struct_len_locale(f));
                        *locale = (char *)*locale + struct_len_locale *
                            (num - lv_len);
                    }
                }

                bracket_stack = 1;
                while (bracket_stack)
                {
                    if (!(*f))
                        return PACKF_NOT_MATCH;
                    else if (*f == '[')
                        ++bracket_stack;
                    else if (*f == ']')
                        --bracket_stack;

                    ++f;
                }

                break;
            case ']':
                /* A top-level ']' has no matching '['. */
                if (depth == 0)
                    return PACKF_NOT_MATCH;
                return 0;
            case 's':
            case 'S':
                des = *net;
                if (from == FROM_ARG)
                    src = va_arg(va, char *);
                else
                    src = (char *)*locale + lv_type;

                if (lv_type)
                {
                    lv_len = 0;
                    if (num == -1)
                        lv_len = strlen(src);
                    else if (num)
                    {
                        lv_len = strnlen(src, num - 1);
                        if (src[lv_len])
                            return PACKF_BE_CUT_OFF;
                    }

                    SET_LV_LEN(des);

                    if (lv_len)
                    {
                        IF_LESS(*left_len, lv_len);
                        memcpy(des, src, lv_len);
                    }

                    offset = lv_type + lv_len;
                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                    {
                        if (num == -1)
                            *locale = (char *)*locale + offset + 1;
                        else
                            *locale = (char *)*locale + lv_type + num;
                    }
                }
                else
                {
                    if (num == -1)
                    {
                        offset = strlen(src) + 1;
                        IF_LESS(*left_len, offset);
                        strcpy(des, src);
                    }
                    else if (type == 's')
                    {
                        offset = num;
                        IF_LESS(*left_len, offset);

                        for (i = 0; i < offset; ++i)
                            if (!(des[i] = src[i]))
                                break;

                        if (offset && i == offset)
                        {
                            des[i - 1] = '\0';
                            return PACKF_BE_CUT_OFF;
                        }

                        for (++i; i < offset; ++i)
                            des[i] = '\0';
                    }
                    else
                    {
                        offset = strnlen(src, num);
                        if (num && offset == num)
                            return PACKF_BE_CUT_OFF;
                        offset += num ? 1 : 0;
                        IF_LESS(*left_len, offset);

                        for (i = 0; i < offset; ++i)
                            if (!(des[i] = src[i]))
                                break;
                    }

                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                    {
                        if (num >= 0 && type == 'S')
                            *locale = (char *)*locale + num;
                        else
                            *locale = (char *)*locale + offset;
                    }
                }

                break;
            case 'a':
                SET_LV(*net, *locale);

                offset = lv_type ? lv_len : (num == -1 ? 1 : num);
                IF_LESS(*left_len, offset);
                memset(*net, 0, offset);
                *net = (char *)*net + offset;
                if (from == FROM_PTR)
                {
                    if (lv_type && num != -1)
                        *locale = (char *)*locale + num;
                    else
                        *locale = (char *)*locale + offset;
                }

                break;
            case 'c':
                DO_PACKF(int8_t, int, HOST_ORDER);

                break;
            case 'w':
                DO_PACKF(int16_t, int, HOST_ORDER);

                break;
            case 'd':
                DO_PACKF(int32_t, int, HOST_ORDER);

                break;
            case 'D':
                DO_PACKF(int64_t, int64_t, HOST_ORDER);

                break;
            case 'f':
                DO_PACKF(float, double, HOST_FLOAT_ORDER);

                break;
            case 'F':
                DO_PACKF(double, double, HOST_FLOAT_ORDER);

                break;
            default:
                return PACKF_NOT_FORMAT;
        }
    }

    return buf_len - *left_len;
}

# define SET_LEN(des, len) do {                                         \
    if (lv_type == 1) __store_u8(des, (uint8_t)(len));                  \
    else __store_u16(des, (uint16_t)(len));                             \
} while (0)

# define GET_LV_LEN(src) do {                                           \
    IF_LESS(*left_len, lv_type);                                        \
    if (lv_type == 1) lv_len = (int)__load_u8(src);                     \
    else lv_len = (int)__load_u16_wire(src, order);                         \
    (src) = (char *)(src) + lv_type;                                    \
} while (0)

# define GET_LV(des, src) do {                                          \
    if (lv_type)                                                        \
    {                                                                   \
        GET_LV_LEN(src);                                                \
        if (num != -1 && lv_len > num) return PACKF_BE_CUT_OFF;   \
        if (from == FROM_ARG) SET_LEN(va_arg(va, char *), lv_len);      \
        else { SET_LEN(des, lv_len); (des) = (char *)des + lv_type; }   \
    }                                                                   \
} while (0)

# define DO_UNPACKF(type, host_order) do {                              \
    int __swap = (sizeof(type) > 1 && order != (host_order));           \
    GET_LV(*locale, *net);                                              \
    if (num == -1 && !lv_type)                                          \
    {                                                                   \
        IF_LESS(*left_len, sizeof(type));                               \
        if (from == FROM_ARG)                                           \
            des = va_arg(va, char *);                                   \
        else                                                            \
        {                                                               \
            des = *locale;                                              \
            *locale = (char *)*locale + sizeof(type);                   \
        }                                                               \
        if (__swap)                                                     \
            __copy_rev(des, *net, sizeof(type));                        \
        else                                                            \
            memcpy(des, *net, sizeof(type));                            \
        *net = (char *)*net + sizeof(type);                             \
    }                                                                   \
    else                                                                \
    {                                                                   \
        if (from == FROM_ARG)                                           \
            des = va_arg(va, char *);                                   \
        else                                                            \
            des = *locale;                                              \
        src = *net;                                                     \
        array_size = lv_type ? lv_len : num;                            \
        CHECK_ARRAY_OFFSET(type, array_size, offset);                   \
        if (offset)                                                     \
        {                                                               \
            IF_LESS(*left_len, offset);                                 \
            if (__swap)                                                 \
            {                                                           \
                for (i = 0; i < array_size; i++)                        \
                    __copy_rev(des + i * sizeof(type),                  \
                               src + i * sizeof(type), sizeof(type));   \
            }                                                           \
            else                                                        \
                memcpy(des, src, (size_t)offset);                       \
            *net = (char *)*net + offset;                               \
        }                                                               \
        if (from == FROM_PTR)                                           \
        {                                                               \
            if (lv_type && num != -1)                                   \
                *locale = (char *)*locale + num * sizeof(type);         \
            else                                                        \
                *locale = (char *)*locale + offset;                     \
        }                                                               \
    }                                                                   \
} while (0)

static int __unpackf(void **net, int *left_len, char const *format, \
        int from, va_list va, void **locale, int depth, int order)
{
    int buf_len = *left_len;
    int num, i, array_size, offset, bracket_stack, lv_len = 0;
    char const *f = format, *str_num;
    char *src, *des;
    char type, lv_type;
    int struct_len_locale = 0, struct_len_net = 0;
    void *struct_start_locale, *struct_start_net;

    while (*f)
    {
        if (*f == ' ')
        {
            ++f;
            continue;
        }

        /* Byte order switch: applies to the following fields, and to
         * nested structs, but does not leak out of the enclosing []. */
        if (*f == '<' || *f == '>')
        {
            order = (*f == '<') ? ORDER_LE : ORDER_BE;
            ++f;
            continue;
        }

        if (*f == '-')
        {
            lv_type = 1;
            ++f;
        }
        else if (*f == '=')
        {
            lv_type = 2;
            ++f;
        }
        else
        {
            lv_type = 0;
        }

        str_num = f;
        while (__ISDIGIT(*f))
            ++f;
        PARSE_NUM(str_num, f, num, -1);

        type = *(f++);
        if (!type)
            return PACKF_EXPECT_FORMAT;

        switch (type)
        {
            case '[':
                if (lv_type && num == -1)
                {
                    if (from == FROM_ARG)
                        *locale = va_arg(va, char *);

                    GET_LV_LEN(*net);
                    /* The declared struct length must fit in the input,
                     * and is consumed from it as a whole. */
                    IF_LESS(*left_len, lv_len);
                    struct_len_net = lv_len;
                    struct_start_net = *net;
                    if (from == FROM_PTR)
                    {
                        SET_LEN(*locale, lv_len);
                        *locale = (char *)*locale + lv_type;
                    }
                    NEG_RET(__unpackf(net, &struct_len_net, f, FROM_PTR,
                                NULL, locale, depth + 1, order));
                    *net = (char *)struct_start_net + lv_len;
                }
                else
                {
                    GET_LV(*locale, *net);
                    if (from == FROM_ARG)
                        *locale = va_arg(va, char *);

                    array_size = lv_type ? lv_len : (num == -1 ? 1 : num);
                    for (i = 0; i < array_size; i++)
                    {
                        struct_start_locale = *locale;
                        NEG_RET(__unpackf(net, left_len, f, FROM_PTR,
                                    NULL, locale, depth + 1, order));
                        struct_len_locale = (char *)*locale -
                            (char *)struct_start_locale;
                    }
                    if (lv_type && from == FROM_PTR)
                    {
                        if (array_size == 0)
                            NEG_RET(struct_len_locale = __struct_len_locale(f));
                        *locale = (char *)*locale + struct_len_locale *
                            (num - lv_len);
                    }
                }

                bracket_stack = 1;
                while (bracket_stack)
                {
                    if (!(*f))
                        return PACKF_NOT_MATCH;
                    else if (*f == '[')
                        ++bracket_stack;
                    else if (*f == ']')
                        --bracket_stack;

                    ++f;
                }

                break;
            case ']':
                /* A top-level ']' has no matching '['. */
                if (depth == 0)
                    return PACKF_NOT_MATCH;
                return 0;
            case 's':
            case 'S':
                src = *net;
                if (from == FROM_ARG)
                    des = va_arg(va, char *);
                else
                    des = (char *)*locale;

                if (lv_type)
                {
                    GET_LV_LEN(src);

                    if ((num == 0 && lv_len) || (num > 0 && lv_len > num - 1))
                        return PACKF_BE_CUT_OFF;

                    if (from == FROM_PTR)
                    {
                        SET_LEN(des, lv_len);
                        des += lv_type;
                    }

                    if (lv_len)
                    {
                        IF_LESS(*left_len, lv_len);
                        memcpy(des, src, lv_len);
                    }

                    if (num != 0)
                        *(des + lv_len) = '\0';

                    offset = lv_type + lv_len;
                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                    {
                        if (num == -1)
                            *locale = (char *)*locale + offset + 1;
                        else
                            *locale = (char *)*locale + lv_type + num;
                    }
                }
                else
                {
                    if (num == -1)
                    {
                        size_t slen;

                        if (*left_len < 0)
                            return PACKF_OUT_OF_BUF;
                        slen = strnlen(src, (size_t)(*left_len));
                        if (slen >= (size_t)(*left_len))
                            return PACKF_OUT_OF_BUF;
                        offset = (int)slen + 1;
                        IF_LESS(*left_len, offset);
                        memcpy(des, src, (size_t)offset);
                    }
                    else if (type == 's')
                    {
                        offset = num;
                        IF_LESS(*left_len, offset);

                        for (i = 0; i < offset; ++i)
                            if (!(des[i] = src[i]))
                                break;

                        if (offset && i == offset)
                        {
                            des[i - 1] = 0;
                            return PACKF_BE_CUT_OFF;
                        }
                    }
                    else
                    {
                        size_t max_scan;

                        if (*left_len < 0)
                            return PACKF_OUT_OF_BUF;
                        max_scan = (size_t)num;
                        if (max_scan > (size_t)(*left_len))
                            max_scan = (size_t)(*left_len);
                        offset = (int)strnlen(src, max_scan);
                        if (num && offset == num)
                            return PACKF_BE_CUT_OFF;
                        offset += num ? 1 : 0;
                        IF_LESS(*left_len, offset);
                        memcpy(des, src, (size_t)offset);
                    }

                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                    {
                        if (num >= 0 && type == 'S')
                            *locale = (char *)*locale + num;
                        else
                            *locale = (char *)*locale + offset;
                    }
                }

                break;
            case 'a':
                GET_LV(*locale, *net);

                offset = lv_type ? lv_len : (num == -1 ? 1 : num);
                IF_LESS(*left_len, offset);
                *net = (char *)*net + offset;
                if (from == FROM_PTR)
                {
                    memset(*locale, 0, offset);
                    if (lv_type && num != -1)
                        *locale = (char *)*locale + num;
                    else
                        *locale = (char *)*locale + offset;
                }

                break;
            case 'c':
                DO_UNPACKF(int8_t, HOST_ORDER);

                break;
            case 'w':
                DO_UNPACKF(int16_t, HOST_ORDER);

                break;
            case 'd':
                DO_UNPACKF(int32_t, HOST_ORDER);

                break;
            case 'D':
                DO_UNPACKF(int64_t, HOST_ORDER);

                break;
            case 'f':
                DO_UNPACKF(float, HOST_FLOAT_ORDER);

                break;
            case 'F':
                DO_UNPACKF(double, HOST_FLOAT_ORDER);

                break;
            default:
                return PACKF_NOT_FORMAT;
        }
    }

    return buf_len - *left_len;
}

static int __check_buf_size(size_t max)
{
    if (max > (size_t)INT_MAX)
        return PACKF_INVALID_ARG;
    return 0;
}

static int __check_left(int *left)
{
    if (!left || *left < 0)
        return PACKF_INVALID_ARG;
    return 0;
}

int packf(void *dest, size_t max, char const *format, ...)
{
    va_list va;
    int ret, left_len;
    void *net = dest, *locale = NULL;

    if (!dest)
        return PACKF_NULL_POINTER;
    if ((ret = __check_buf_size(max)) < 0)
        return ret;
    if (!format)
        return 0;

    left_len = (int)max;

    va_start(va, format);
    ret = __packf(&net, &left_len, format, FROM_ARG, va, &locale, 0, ORDER_BE);
    va_end(va);

    return ret;
}

int unpackf(void *src, size_t max, char const *format, ...)
{
    va_list va;
    int ret, left_len;
    void *net = src, *locale = NULL;

    if (!src)
        return PACKF_NULL_POINTER;
    if ((ret = __check_buf_size(max)) < 0)
        return ret;
    if (!format)
        return 0;

    left_len = (int)max;

    va_start(va, format);
    ret = __unpackf(&net, &left_len, format, FROM_ARG, va, &locale, 0, ORDER_BE);
    va_end(va);

    return ret;
}

int vpackf(void **current, int *left, char const *format, ...)
{
    va_list va;
    int ret, left_len;
    void *net, *locale = NULL;

    if (!current || !*current || !left)
        return PACKF_NULL_POINTER;
    if ((ret = __check_left(left)) < 0)
        return ret;
    if (!format)
        return 0;

    net = *current;
    left_len = *left;

    va_start(va, format);
    ret = __packf(&net, &left_len, format, FROM_ARG, va, &locale, 0, ORDER_BE);
    va_end(va);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    return ret;
}

int vunpackf(void **current, int *left, char const *format, ...)
{
    va_list va;
    int ret, left_len;
    void *net, *locale = NULL;

    if (!current || !*current || !left)
        return PACKF_NULL_POINTER;
    if ((ret = __check_left(left)) < 0)
        return ret;
    if (!format)
        return 0;

    net = *current;
    left_len = *left;

    va_start(va, format);
    ret = __unpackf(&net, &left_len, format, FROM_ARG, va, &locale, 0, ORDER_BE);
    va_end(va);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    return ret;
}

int vpacka(void **current, int *left, char const *format, va_list arg)
{
    int ret, left_len;
    void *net, *locale = NULL;

    if (!current || !*current || !left)
        return PACKF_NULL_POINTER;
    if ((ret = __check_left(left)) < 0)
        return ret;
    if (!format)
        return 0;

    net = *current;
    left_len = *left;
    ret = __packf(&net, &left_len, format, FROM_ARG, arg, &locale, 0, ORDER_BE);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    return ret;
}

int vunpacka(void **current, int *left, char const *format, va_list arg)
{
    int ret, left_len;
    void *net, *locale = NULL;

    if (!current || !*current || !left)
        return PACKF_NULL_POINTER;
    if ((ret = __check_left(left)) < 0)
        return ret;
    if (!format)
        return 0;

    net = *current;
    left_len = *left;
    ret = __unpackf(&net, &left_len, format, FROM_ARG, arg, &locale, 0, ORDER_BE);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    return ret;
}

int vpackn(void **current, int *left, void const *buf, size_t n)
{
    int ret;

    if (!current || !*current || !left || !buf)
        return PACKF_NULL_POINTER;
    if ((ret = __check_left(left)) < 0)
        return ret;
    if (n > (size_t)INT_MAX)
        return PACKF_INVALID_ARG;

    IF_LESS(*left, n);
    memcpy(*current, buf, n);
    *current = (char *)*current + n;

    return (int)n;
}

int vunpackn(void **current, int *left, void *buf, size_t n)
{
    int ret;

    if (!current || !*current || !left || !buf)
        return PACKF_NULL_POINTER;
    if ((ret = __check_left(left)) < 0)
        return ret;
    if (n > (size_t)INT_MAX)
        return PACKF_INVALID_ARG;

    IF_LESS(*left, n);
    memcpy(buf, *current, n);
    *current = (char *)*current + n;

    return (int)n;
}

