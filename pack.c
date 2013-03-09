/*
 * Description: a lib for network programming
 *     History: damonyang@tencent.com, 2012/09/04, create
 */

# include <stdio.h>
# include <string.h>
# include <stdarg.h>
# include <ctype.h>
# include <stdlib.h>
# include <netinet/in.h>

# include "oi_pack.h"

# define FROM_ARG 1
# define FROM_PTR 2

# define ERROR_BAD_PARAM        -221
# define ERROR_NO_BUF           -222
# define ERROR_NULL_POINTER     -223
# define ERROR_NOT_FORMAT       -224
# define ERROR_EXPECT_FORMAT    -225
# define ERROR_NOT_MATCH        -226
# define ERROR_PKG_FORMAT       -227
# define ERROR_OIDB_RESULT      -228

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

# define IF_LESS(x, n) do {                             \
    if ((x) < 0 || (x) < (n)) return ERROR_NO_BUF;      \
    (x) -= (n);                                         \
} while(0)

# define NEG_RET(x) do {                                \
    int __ret = (x);                                    \
    if (__ret < 0) return __ret;                        \
} while (0)

# define NO_SWITCH(x) (x)

# define SET_LV_LEN(des) do {                                       \
    IF_LESS(*left_len, lv_type);                                    \
    if (lv_type == 1) *((uint8_t *)(des)) = (uint8_t)lv_len;        \
    else *((uint16_t *)(des)) = htons((uint16_t)lv_len);            \
    (des) = (char *)(des) + lv_type;                                \
} while (0)

# define SET_LV(des, src) do {                                      \
    if (lv_type)                                                    \
    {                                                               \
        if (from == FROM_ARG) lv_len = va_arg(va, int);             \
        else                                                        \
        {                                                           \
            if (lv_type == 1) lv_len = *((uint8_t *)(src));         \
            else lv_len = *((uint16_t *)(src));                     \
            (src) = (char *)(src) + lv_type;                        \
        }                                                           \
        if (num != -1 && lv_len > num) return ERROR_NO_BUF;         \
        SET_LV_LEN(des);                                            \
    }                                                               \
} while (0)

# define DO_PACKF(type1, type2, swap, swap_flag) do {               \
    SET_LV(*net, *locale);                                          \
    if (num == -1 && !lv_type)                                      \
    {                                                               \
        IF_LESS(*left_len, sizeof(type1));                          \
        if (from == FROM_ARG)                                       \
            *((type1 *)(*net)) = swap((type1)(va_arg(va, type2)));  \
        else                                                        \
        {                                                           \
            *((type1 *)(*net)) = swap(*((type1 *)(*locale)));       \
            *locale = (char *)*locale + sizeof(type1);              \
        }                                                           \
        *net = (char *)*net + sizeof(type1);                        \
    }                                                               \
    else                                                            \
    {                                                               \
        if (from == FROM_ARG)                                       \
            src = va_arg(va, char *);                               \
        else                                                        \
            src = *locale;                                          \
        des = *net;                                                 \
        if (lv_type && num != -1 && lv_len > num)                   \
            return ERROR_NO_BUF;                                    \
        array_size = lv_type ? lv_len : num;                        \
        offset = sizeof(type1) * array_size;                        \
        if (offset) {                                               \
            IF_LESS(*left_len, offset);                             \
            if (!swap_flag)                                         \
                memcpy(des, src, offset);                           \
            else                                                    \
                for (i = 0; i < array_size; i++)                    \
                    ((type1 *)des)[i] = swap(((type1 *)src)[i]);    \
            *net = (char *)*net + offset;                           \
        }                                                           \
        if (from == FROM_PTR)                                       \
        {                                                           \
            if (lv_type && num)                                     \
                *locale = (char *)*locale + num * sizeof(type1);    \
            else                                                    \
                *locale = (char *)*locale + offset;                 \
        }                                                           \
    }                                                               \
} while (0)

static int __packf(void **net, int *left_len, char *format, \
        int from, va_list va, void **locale)
{
    int buf_len = *left_len;
    int num, i, array_size, offset, bracket_stack, lv_len = 0;
    char *f = format, *str_num, *src, *des;
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
        while (isdigit(*f))
            ++f;
        if (str_num == f)
            num = -1;
        else
            num = atoi(str_num);

        type = *(f++);
        if (!type)
            return ERROR_EXPECT_FORMAT;

        /* printf("num = %d, type = %c\n", num,  type); */

        switch (type)
        {
            case '[':
                if (from == FROM_ARG)
                    *locale = va_arg(va, char *);

                if (lv_type && num == -1)
                {
                    struct_start_net = *net = (char *)*net + lv_type;
                    NEG_RET(__packf(net, left_len, f, FROM_PTR, NULL, locale));
                    lv_len = (char *)*net - (char *)struct_start_net;
                    p_struct_len = (char *)struct_start_net - lv_type;
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
                        return ERROR_NOT_MATCH;
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
                    if (num == -1)
                        lv_len = strlen(src);
                    else
                        lv_len = strnlen(src, num - 1);
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
                            *locale = (char *)*locale + offset;
                        else
                            *locale = (char *)*locale + lv_type + num;
                    }
                }
                else
                {
                    if (num == -1)
                        offset = strlen(src) + 1;
                    else
                        offset = num;

                    IF_LESS(*left_len, offset);

                    for (i = 0; i < offset; ++i)
                        if (!(des[i] = src[i]))
                            break;
                    for (; i < offset; ++i)
                        des[i] = '\0';
                    des[offset - 1] = '\0';

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
                return ERROR_NOT_FORMAT;
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
        if (num != -1 && lv_len > num) return ERROR_NO_BUF;                 \
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
        if (offset) {                                                       \
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
    char *f = format, *str_num, *src, *des;
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
        while (isdigit(*f))
            ++f;
        if (str_num == f)
            num = -1;
        else
            num = atoi(str_num);
        type = *(f++);
        if (!type)
            return ERROR_EXPECT_FORMAT;

        /* printf("num = %d, type = %c\n", num,  type); */

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
                        return ERROR_NOT_MATCH;
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
                    if (num != -1 && lv_len > num - 1)
                        return ERROR_NO_BUF;

                    if (from == FROM_PTR)
                    {
                        SET_LEN(des, lv_len);
                        des += lv_type;
                    }
                    if (lv_len)
                    {
                        IF_LESS(*left_len, lv_len);
                        memcpy(des, src, lv_len);
                        *(des + lv_len) = '\0';
                    }

                    offset = lv_type + lv_len;
                    *net = (char *)*net + offset;
                    if (from == FROM_PTR)
                    {
                        if (num == -1)
                            *locale = (char *)*locale + offset;
                        else
                            *locale = (char *)*locale + lv_type + num;
                    }
                }
                else
                {
                    if (num == -1)
                        offset = strlen(src) + 1;
                    else
                        offset = num;

                    IF_LESS(*left_len, offset);

                    for (i = 0; i < offset; ++i)
                        if (!(des[i] = src[i]))
                            break;
                    for (; i < offset; ++i)
                        des[i] = '\0';
                    des[offset - 1] = '\0';

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
                return ERROR_NOT_FORMAT;
        }
    }

    return buf_len - *left_len;
}

int packf(void *const ptr, size_t max_len, char *format, ...)
{
    va_list va;
    int ret;
    int left_len = max_len;
    void *net = ptr, *locale = NULL;

    if (!ptr)
        return ERROR_NULL_POINTER;
    if (!format)
        return 0;

    va_start(va, format);
    ret = __packf(&net, &left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    return ret;
}

int unpackf(void *const ptr, size_t max_len, char *format, ...)
{
    va_list va;
    int ret;
    int left_len = max_len;
    void *net = ptr, *locale = NULL;

    if (!ptr)
        return ERROR_NULL_POINTER;
    if (!format)
        return 0;

    va_start(va, format);
    ret = __unpackf(&net, &left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    return ret;
}

int vpackf(void **ptr, int *left_len, char *format, ...)
{
    va_list va;
    int ret;
    void *locale = NULL;

    if (!ptr || !*ptr || !left_len)
        return ERROR_NULL_POINTER;
    if (!format)
        return 0;

    va_start(va, format);
    ret = __packf(ptr, left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    return ret;
}

int vunpackf(void **ptr, int *left_len, char *format, ...)
{
    va_list va;
    int ret;
    void *locale = NULL;

    if (!ptr || !*ptr || !left_len)
        return ERROR_NULL_POINTER;
    if (!format)
        return 0;

    va_start(va, format);
    ret = __unpackf(ptr, left_len, format, FROM_ARG, va, &locale);
    va_end(va);

    return ret;
}

int vpacka(void **ptr, int *left_len, char *format, va_list arg)
{
    void *locale = NULL;
    int ret;

    if (!ptr || !*ptr || !left_len)
        return ERROR_NULL_POINTER;
    if (!format)
        return 0;

    ret = __packf(ptr, left_len, format, FROM_ARG, arg, &locale);

    return ret;
}

int vunpacka(void **ptr, int *left_len, char *format, va_list arg)
{
    void *locale = NULL;
    int ret;

    if (!ptr || !*ptr || !left_len)
        return ERROR_NULL_POINTER;
    if (!format)
        return 0;

    ret = __unpackf(ptr, left_len, format, FROM_ARG, arg, &locale);

    return ret;
}

int vpackn(void **ptr, int *left_len, void *buf, size_t len)
{
    if (!ptr || !*ptr || !left_len || !buf)
        return ERROR_NULL_POINTER;

    IF_LESS(*left_len, len);

    memcpy(*ptr, buf, len);

    *ptr = (char *)*ptr + len;

    return len;
}

int vunpackn(void **ptr, int *left_len, void *buf, size_t len)
{
    if (!ptr || !*ptr || !left_len || !buf)
        return ERROR_NULL_POINTER;

    IF_LESS(*left_len, len);

    memcpy(buf, *ptr, len);

    *ptr = (char *)*ptr + len;

    return len;
}
