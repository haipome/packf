/*
 * Description: a lib for network programming
 *     History: damonyang@tencent.com, 2012/09/04, create
 */

# include <stdio.h>
# include <string.h>
# include <stdarg.h>
# include <netinet/in.h>

# include "pack.h"

# define FROM_ARG 1
# define FROM_PTR 2

# define ERROR_BAD_PARAM        -221
# define ERROR_NO_BUF           -222
# define ERROR_CUT_OFF          -223
# define ERROR_NULL_POINTER     -224
# define ERROR_NOT_FORMAT       -225
# define ERROR_EXPECT_FORMAT    -226
# define ERROR_NOT_MATCH        -227
# define ERROR_PKG_FORMAT       -228
# define ERROR_OIDB_RESULT      -229

static char *err_msg[] =
{
    "bad parameter",
    "out of buffer",
    "be cut off",
    "null pointer",
    "not format",
    "format error",
    "struct not match",
    "pkg format error",
    "oidb result error",
};

# define swap_ddword(x)  \
   ((((x) & 0xff00000000000000llu) >> 56) | \
    (((x) & 0x00ff000000000000llu) >> 40) | \
    (((x) & 0x0000ff0000000000llu) >> 24) | \
    (((x) & 0x000000ff00000000llu) >> 8)  | \
    (((x) & 0x00000000ff000000llu) << 8)  | \
    (((x) & 0x0000000000ff0000llu) << 24) | \
    (((x) & 0x000000000000ff00llu) << 40) | \
    (((x) & 0x00000000000000ffllu) << 56) )

/* endian.h do not support standard c */
static union { char c[4]; uint32_t l; } int_endian_t = {{ 'l', '?', '?', 'b' }};

# define IS_INT_BIG_ENDIAN ((char)(int_endian_t.l) == 'b')

uint64_t ddword_endian_switch(uint64_t a)
{
    if (IS_INT_BIG_ENDIAN)
        return a;

    return swap_ddword(a);
}

static union { float f; uint8_t c[4]; } float_endian_t = { 1.0 };

# define IS_FLOAT_BIG_ENDIAN (float_endian_t.c[0] == 0x3f)

float float_endian_switch(float a)
{
    if (IS_FLOAT_BIG_ENDIAN)
        return a;

    union { float f; uint32_t i; } u_float = { a };
    u_float.i = htonl(u_float.i);

    return u_float.f;
}

double double_endian_switch(double a)
{
    if (IS_FLOAT_BIG_ENDIAN)
        return a;

    union { double d; uint64_t i; } u_double = { a };
    u_double.i = htonll(u_double.i);

    return u_double.d;
}

# define __ISDIGIT(c) ((c) >= '0' && (c) <= '9')

static inline int __atoi(char *s, size_t n)
{
    int r = 0, i;

    for (i = 0; i < n; ++i)
        r = (r * 10) + (s[i] - '0');

    return r;
}

char *pack_error_format = NULL;
int pack_print_error = 0;

# define ERR_RET_FMT(ret) do {                                          \
    pack_error_format = __f;                                            \
    return ret;                                                         \
} while (0)

# define PRINT_ERR_FMT(ret) do {                                        \
    if ((ret) < 0 && pack_print_error)                                  \
    {                                                                   \
        fprintf(stderr, "%s: %s", __func__, err_msg[-(ret) - 221]);     \
        if (pack_print_error)                                           \
            fprintf(stderr, ": %s\n", pack_error_format);               \
        else                                                            \
            putc('\n', stderr);                                         \
    }                                                                   \
} while (0)

# define ERR_RET_PRINT(ret) do {                                        \
    if (pack_print_error)                                               \
        fprintf(stderr, "%s: %s\n", __func__, err_msg[-(ret) - 221]);   \
    return ret;                                                         \
} while (0)

# define IF_LESS(x, n) do {                                             \
    if ((x) < 0 || (x) < (n)) ERR_RET_FMT(ERROR_NO_BUF);                \
    (x) -= (n);                                                         \
} while(0)

# define NEG_RET(x) do {                                                \
    int __ret = (x);                                                    \
    if (__ret < 0) return __ret;                                        \
} while (0)

# define NO_SWITCH(x) (x)

# define SET_LV_LEN(des) do {                                           \
    IF_LESS(*left_len, lv_type);                                        \
    if (lv_type == 1) *((uint8_t *)(des)) = (uint8_t)lv_len;            \
    else *((uint16_t *)(des)) = htons((uint16_t)lv_len);                \
    (des) = (char *)(des) + lv_type;                                    \
} while (0)

# define SET_LV(des, src) do {                                          \
    if (lv_type)                                                        \
    {                                                                   \
        if (from == FROM_ARG) lv_len = va_arg(va, int);                 \
        else                                                            \
        {                                                               \
            if (lv_type == 1) lv_len = *((uint8_t *)(src));             \
            else lv_len = *((uint16_t *)(src));                         \
            (src) = (char *)(src) + lv_type;                            \
        }                                                               \
        if (num != -1 && lv_len > num) ERR_RET_FMT(ERROR_CUT_OFF);      \
        SET_LV_LEN(des);                                                \
    }                                                                   \
} while (0)

# define DO_PACKF(type1, type2, swap, swap_flag) do {                   \
    SET_LV(*net, *locale);                                              \
    if (num == -1 && !lv_type)                                          \
    {                                                                   \
        IF_LESS(*left_len, sizeof(type1));                              \
        if (from == FROM_ARG)                                           \
            *((type1 *)(*net)) = swap((type1)(va_arg(va, type2)));      \
        else                                                            \
        {                                                               \
            *((type1 *)(*net)) = swap(*((type1 *)(*locale)));           \
            *locale = (char *)*locale + sizeof(type1);                  \
        }                                                               \
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
        offset = sizeof(type1) * array_size;                            \
        if (offset)                                                     \
        {                                                               \
            IF_LESS(*left_len, offset);                                 \
            if (!swap_flag)                                             \
                memcpy(des, src, offset);                               \
            else                                                        \
                for (i = 0; i < array_size; i++)                        \
                    ((type1 *)des)[i] = swap(((type1 *)src)[i]);        \
            *net = (char *)*net + offset;                               \
        }                                                               \
        if (from == FROM_PTR)                                           \
        {                                                               \
            if (lv_type && num)                                         \
                *locale = (char *)*locale + num * sizeof(type1);        \
            else                                                        \
                *locale = (char *)*locale + offset;                     \
        }                                                               \
    }                                                                   \
} while (0)

static int __packf(void **net, int *left_len, char *format, \
        int from, va_list va, void **locale)
{
    int buf_len = *left_len;
    int num, i, array_size, offset, bracket_stack, lv_len = 0;
    char *f = format, *__f, *str_num, *src, *des;
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

        __f = f;

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
        if (str_num == f)
            num = -1;
        else
            num = __atoi(str_num, f - str_num);

        type = *(f++);
        if (!type)
            ERR_RET_FMT(ERROR_EXPECT_FORMAT);

        switch (type)
        {
            case '[':
                if (from == FROM_ARG)
                    *locale = va_arg(va, char *);

                if (lv_type && num == -1)
                {
                    p_struct_len = *net;
                    struct_start_net = *net = (char *)*net + lv_type;
                    if (from == FROM_PTR)
                        *locale = (char *)*locale + lv_type;
                    NEG_RET(__packf(net, left_len, f, FROM_PTR, NULL, locale));
                    lv_len = (char *)*net - (char *)struct_start_net;
                    SET_LV_LEN(p_struct_len);
                }
                else
                {
                    SET_LV(*net, *locale);
                    array_size = lv_type ? lv_len : (num == -1 ? 1 : num);
                    for (i = 0; i < array_size; i++)
                    {
                        struct_start_locale = *locale;
                        NEG_RET(__packf(net, left_len, f, FROM_PTR, NULL, locale));
                        struct_len_locale = (char *)*locale - (char *)struct_start_locale;
                    }
                    if (lv_type && from == FROM_PTR)
                        *locale = (char *)*locale + struct_len_locale * (num - lv_len);
                }

                bracket_stack = 1;
                while (bracket_stack)
                {
                    if (!(*f))
                        ERR_RET_FMT(ERROR_NOT_MATCH);
                    else if (*f == '[')
                        ++bracket_stack;
                    else if (*f == ']')
                        --bracket_stack;

                    ++f;
                }

                break;
            case ']':
                return 0;
            case 's':
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
                            ERR_RET_FMT(ERROR_CUT_OFF);
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
                    else
                    {
                        offset = num;
                        IF_LESS(*left_len, offset);

                        for (i = 0; i < offset; ++i)
                            if (!(des[i] = src[i]))
                                break;

                        if (offset && i == offset)
                        {
                            des[i - 1] = '\0';
                            ERR_RET_FMT(ERROR_CUT_OFF);
                        }

                        for (++i; i < offset; ++i)
                            des[i] = '\0';
                    }

                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                        *locale = (char *)*locale + offset;
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
                    if (lv_type && num)
                        *locale = (char *)*locale + num;
                    else
                        *locale = (char *)*locale + offset;
                }

                break;
            case 'c':
                DO_PACKF(int8_t, int, NO_SWITCH, 0);

                break;
            case 'w':
                DO_PACKF(int16_t, int, htons, 1);

                break;
            case 'd':
                DO_PACKF(int32_t, int, htonl, 1);

                break;
            case 'D':
                DO_PACKF(int64_t, int64_t, htonll, 1);

                break;
            case 'f':
                DO_PACKF(float, double, htonf, 1);

                break;
            case 'F':
                DO_PACKF(double, double, htond, 1);

                break;
            default:
                ERR_RET_FMT(ERROR_NOT_FORMAT);
        }
    }

    return buf_len - *left_len;
}

# define SET_LEN(des, len) do {                                             \
    if (lv_type == 1) *((uint8_t *)(des)) = (uint8_t)len;                   \
    else *((uint16_t *)(des)) = (uint16_t)lv_len;                           \
} while (0)

# define GET_LV_LEN(src) do {                                               \
    IF_LESS(*left_len, lv_type);                                            \
    if (lv_type == 1) lv_len = *((uint8_t *)(src));                         \
    else lv_len = ntohs(*((uint16_t *)(src)));                              \
    (src) = (char *)(src) + lv_type;                                        \
} while (0)

# define GET_LV(des, src) do {                                              \
    if (lv_type)                                                            \
    {                                                                       \
        GET_LV_LEN(src);                                                    \
        if (num != -1 && lv_len > num) ERR_RET_FMT(ERROR_CUT_OFF);          \
        if (from == FROM_ARG) SET_LEN(va_arg(va, char *), lv_len);          \
        else { SET_LEN(des, lv_len); (des) = (char *)des + lv_type; }       \
    }                                                                       \
} while (0)

# define DO_UNPACKF(type, swap, swap_flag) do {                             \
    GET_LV(*locale, *net);                                                  \
    if (num == -1 && !lv_type)                                              \
    {                                                                       \
        IF_LESS(*left_len, sizeof(type));                                   \
        if (from == FROM_ARG)                                               \
            *((type *)(va_arg(va, char *))) = swap(*((type *)(*net)));      \
        else                                                                \
        {                                                                   \
            *((type *)(*locale)) = swap(*((type *)(*net)));                 \
            *locale = (char *)*locale + sizeof(type);                       \
        }                                                                   \
        *net = (char *)*net + sizeof(type);                                 \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        if (from == FROM_ARG)                                               \
            des = va_arg(va, char *);                                       \
        else                                                                \
            des = *locale;                                                  \
        src = *net;                                                         \
        array_size = lv_type ? lv_len : num;                                \
        offset = sizeof(type) * array_size;                                 \
        if (offset)                                                         \
        {                                                                   \
            IF_LESS(*left_len, offset);                                     \
            if (!swap_flag)                                                 \
                memcpy(des, src, offset);                                   \
            else                                                            \
                for (i = 0; i < array_size; i++)                            \
                    ((type *)des)[i] = swap(((type *)src)[i]);              \
            *net = (char *)*net + offset;                                   \
        }                                                                   \
        if (from == FROM_PTR)                                               \
        {                                                                   \
            if (lv_type && num)                                             \
                *locale = (char *)*locale + num * sizeof(type);             \
            else                                                            \
                *locale = (char *)*locale + offset;                         \
        }                                                                   \
    }                                                                       \
} while (0)

static int __unpackf(void **net, int *left_len, char *format, \
        int from, va_list va, void **locale)
{
    int buf_len = *left_len;
    int num, i, array_size, offset, bracket_stack, lv_len = 0;
    char *f = format, *__f, *str_num, *src, *des;
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

        __f = f;

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
        if (str_num == f)
            num = -1;
        else
            num = __atoi(str_num, f - str_num);

        type = *(f++);
        if (!type)
            ERR_RET_FMT(ERROR_EXPECT_FORMAT);

        switch (type)
        {
            case '[':
                if (from == FROM_ARG)
                    *locale = va_arg(va, char *);

                if (lv_type && num == -1)
                {
                    GET_LV_LEN(*net);
                    struct_len_net = lv_len;
                    struct_start_net = *net;
                    if (from == FROM_PTR)
                    {
                        SET_LEN(*locale, lv_len);
                        *locale = (char *)*locale + lv_type;
                    }
                    NEG_RET(__unpackf(net, &struct_len_net, f, FROM_PTR, NULL, locale));
                    *net = (char *)struct_start_net + lv_len;
                }
                else
                {
                    GET_LV(*locale, *net);
                    array_size = lv_type ? lv_len : (num == -1 ? 1 : num);
                    for (i = 0; i < array_size; i++)
                    {
                        struct_start_locale = *locale;
                        NEG_RET(__unpackf(net, left_len, f, FROM_PTR, NULL, locale));
                        struct_len_locale = (char *)*locale - (char *)struct_start_locale;
                    }
                    if (lv_type && from == FROM_PTR)
                        *locale = (char *)*locale + struct_len_locale * (num - lv_len);
                }

                bracket_stack = 1;
                while (bracket_stack)
                {
                    if (!(*f))
                        ERR_RET_FMT(ERROR_NOT_MATCH);
                    else if (*f == '[')
                        ++bracket_stack;
                    else if (*f == ']')
                        --bracket_stack;

                    ++f;
                }

                break;
            case ']':
                return 0;
            case 's':
                src = *net;
                if (from == FROM_ARG)
                    des = va_arg(va, char *);
                else
                    des = (char *)*locale;

                if (lv_type)
                {
                    GET_LV_LEN(src);

                    if ((num == 0 && lv_len) || (num > 0 && lv_len > num - 1))
                        ERR_RET_FMT(ERROR_CUT_OFF);

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
                        offset = strlen(src) + 1;
                        IF_LESS(*left_len, offset);
                        strcpy(des, src);
                    }
                    else
                    {
                        offset = num;
                        IF_LESS(*left_len, offset);

                        for (i = 0; i < offset; ++i)
                            if (!(des[i] = src[i]))
                                break;

                        if (offset && i == offset)
                        {
                            des[i - 1] = 0;
                            ERR_RET_FMT(ERROR_CUT_OFF);
                        }
                    }

                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                        *locale = (char *)*locale + offset;
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
                    if (lv_type && num)
                        *locale = (char *)*locale + num;
                    else
                        *locale = (char *)*locale + offset;
                }

                break;
            case 'c':
                DO_UNPACKF(int8_t, NO_SWITCH, 0);

                break;
            case 'w':
                DO_UNPACKF(int16_t, ntohs, 1);

                break;
            case 'd':
                DO_UNPACKF(int32_t, ntohl, 1);

                break;
            case 'D':
                DO_UNPACKF(int64_t, ntohll, 1);

                break;
            case 'f':
                DO_UNPACKF(float, ntohf, 1);

                break;
            case 'F':
                DO_UNPACKF(double, ntohd, 1);

                break;
            default:
                ERR_RET_FMT(ERROR_NOT_FORMAT);
        }
    }

    return buf_len - *left_len;
}

int packf(void *dest, size_t max, char *format, ...)
{
    va_list va;
    int ret, left_len = (int)max;
    void *net = dest, *locale = NULL;

    if (!dest)
        ERR_RET_PRINT(ERROR_NULL_POINTER);
    if (!format)
        return 0;

    va_start(va, format);
    ret = __packf(&net, &left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    PRINT_ERR_FMT(ret);

    return ret;
}

int unpackf(void *src, size_t max, char *format, ...)
{
    va_list va;
    int ret, left_len = (int)max;
    void *net = src, *locale = NULL;

    if (!src)
        ERR_RET_PRINT(ERROR_NULL_POINTER);
    if (!format)
        return 0;

    va_start(va, format);
    ret = __unpackf(&net, &left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    PRINT_ERR_FMT(ret);

    return ret;
}

int vpackf(void **current, int *left, char *format, ...)
{
    va_list va;
    int ret, left_len = *left;
    void *net = *current, *locale = NULL;

    if (!current || !*current || !left)
        ERR_RET_PRINT(ERROR_NULL_POINTER);
    if (!format)
        return 0;

    va_start(va, format);
    ret = __packf(&net, &left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    PRINT_ERR_FMT(ret);

    return ret;
}

int vunpackf(void **current, int *left, char *format, ...)
{
    va_list va;
    int ret, left_len = *left;
    void *net = *current, *locale = NULL;

    if (!current || !*current || !left)
        ERR_RET_PRINT(ERROR_NULL_POINTER);
    if (!format)
        return 0;

    va_start(va, format);
    ret = __unpackf(&net, &left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    PRINT_ERR_FMT(ret);

    return ret;
}

int vpacka(void **current, int *left, char *format, va_list arg)
{
    int ret, left_len = *left;
    void *net = *current, *locale = NULL;

    if (!current || !*current || !left)
        ERR_RET_PRINT(ERROR_NULL_POINTER);
    if (!format)
        return 0;

    ret = __packf(&net, &left_len, format, FROM_ARG, arg, &locale);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    PRINT_ERR_FMT(ret);

    return ret;
}

int vunpacka(void **current, int *left, char *format, va_list arg)
{
    int ret, left_len = *left;
    void *net = *current, *locale = NULL;

    if (!current || !*current || !left)
        ERR_RET_PRINT(ERROR_NULL_POINTER);
    if (!format)
        return 0;

    ret = __unpackf(&net, &left_len, format, FROM_ARG, arg, &locale);

    if (ret > 0)
    {
        *current = net;
        *left    = left_len;
    }

    PRINT_ERR_FMT(ret);

    return ret;
}

int vpackn(void **current, int *left, void *buf, size_t n)
{
    if (!current || !*current || !left || !buf)
        ERR_RET_PRINT(ERROR_NULL_POINTER);

    char *__f = NULL;
    IF_LESS(*left, n);
    memcpy(*current, buf, n);
    *current = (char *)*current + n;

    return (int)n;
}

int vunpackn(void **current, int *left, void *buf, size_t n)
{
    if (!current || !*current || !left || !buf)
        ERR_RET_PRINT(ERROR_NULL_POINTER);

    char *__f = NULL;
    IF_LESS(*left, n);
    memcpy(buf, *current, n);
    *current = (char *)*current + n;

    return (int)n;
}

