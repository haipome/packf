/*
 *    Filename: pack.c
 * Description: 支持数组和结构体的网络数据打包和解包封装函数
 *      Coding: GBK
 *     History: damonyang@tencent, 2012/09/04, create
 */

# include <string.h>
# include <stdarg.h>
# include <ctype.h>
# include <stdlib.h>
# include <netinet/in.h>
# include <endian.h>

# include "pack.h"

# define BUF_NUM_LEN 64

enum {
    FROM_ARG,
    FROM_PTR,
};

enum {
    DO_PACK,
    DO_UNPACK,
};

enum {
    ERROR_BAD_PARAM = -221,       /* 传入参数错误，指针为 NULL or max_len < 0 */
    ERROR_NO_BUF = -222,          /* ptr 指向的存储区长度不够 */
    ERROR_NULL_POINTER = -223,    /* 变参中指针有 NULL */
    ERROR_NEED_NUM = -224,        /* as 类型必须前缀数字 */
    ERROR_NOT_FORMAT = -225,      /* 未知 format 类型 */
    ERROR_EXPECT_FORMAT = -226,   /* format 缺少参数 */
    ERROR_NOT_MATCH = -227,       /* [] 不匹配 */
    ERROR_NO_DDWORD_ARG = -228,   /* 32 位机不支持变参中传递 8 字节长整数 */
};

uint64_t endian_switch(uint64_t a)
{
    if (BYTE_ORDER == BIG_ENDIAN)
        return a;
    else
        return swap_ddword(a);
}

# define IF_LESS(x, n)  do                                                  \
                        {                                                   \
                            if ((x) < (n))                                  \
                                return ERROR_NO_BUF;                        \
                            (x) -= (n);                                     \
                        } while(0)                                          \

# define NO_SWITCH(x) (x)

# define GET_AND_SET(val, type1, type2, switch1, switch2) do                \
                {                                                           \
                    IF_LESS(*left_len, sizeof(type2));                      \
                    if (job == DO_PACK)                                     \
                    {                                                       \
                        if (n == -1)                                        \
                        {                                                   \
                            if (from == FROM_ARG)                           \
                                val = va_arg(*va, type1);                   \
                            else                                            \
                            {                                               \
                                val = (type1)(*((type2 *)(*locale)));       \
                                *locale += sizeof(type2);                   \
                            }                                               \
                        }                                                   \
                        else                                                \
                        {                                                   \
                            val = (type1)(((type2 *)s)[i]);                 \
                            if ((job == DO_PACK) && (n != -1) &&            \
                                    (from == FROM_PTR))                     \
                                *locale += sizeof(type2);                   \
                        }                                                   \
                        *((type2 *)(*net)) = switch1((type2)val);           \
                        *net += sizeof(type2);                              \
                    }                                                       \
                    else                                                    \
                    {                                                       \
                        ((type2 *)d)[i] = switch2(((type2 *)s)[i]);         \
                        if ((job == DO_UNPACK) && (from == FROM_PTR))       \
                            *locale += sizeof(type2);                       \
                        *net += sizeof(type2);                              \
                    }                                                       \
                } while (0)

static int process_data(char **net, int *left_len, int type, int n, \
                        int from, va_list *va, char **locale, int job)
{
    int i, array_num, slen;
    char *s, *d;

    int itmp;
    uint64_t itmp8;
    double ftmp;

    if ((!net || !(*net)))
        return ERROR_BAD_PARAM;

    if (job == DO_PACK)
        d = *net;
    else
        s = *net;

    if (type == 's')
    {
        if (job == DO_PACK)
        {
            if (from == FROM_ARG)
                s = va_arg(*va, char *);
            else
                s = *locale;
        }
        else
        {
            if (from == FROM_ARG)
                d = va_arg(*va, char *);
            else
                d = *locale;
        }

        if (n == -1)
        {
            slen = strlen(s) + 1;
        }
        else
        {
            slen = n;
        }

        IF_LESS(*left_len, slen);
        
        for (i = 0; i < (slen - 1); ++i)
        {
            d[i] = s[i];
            if (!d[i])
                break;
        }

        if(n == -1) 
        {
            d[i] = '\0';
        }
        else if (n > 0)
        {
            for (; i < slen; ++i)
                d[i] = '\0';
        }

        *net += slen;

        if (from == FROM_PTR)
            *locale += slen;
    }
    else if (type == 'a')
    {
        if (n == -1)
            n = 1;

        IF_LESS(*left_len, n);

        if (job == DO_PACK)
            memset(*net, '\0', n);

        *net += n;
    }
    else
    {
        if ((job == DO_PACK) && n != -1)
        {
            if (from == FROM_ARG)
                s = va_arg(*va, char *);
            else
                s = *locale;
        }
        if (job == DO_UNPACK)
        {
            if (from == FROM_ARG)
                d = va_arg(*va, char *);
            else
                d = *locale;
        }

        if (n == -1)
            array_num = 1;
        else
            array_num = n;

        for (i = 0; i < array_num; ++i)
        {
            switch (type)
            {
                case 'c':
                    GET_AND_SET(itmp, int, int8_t, NO_SWITCH, NO_SWITCH);
                    break;
                case 'w':
                    GET_AND_SET(itmp, int, int16_t, htons, ntohs);
                    break;
                case 'd':
                    GET_AND_SET(itmp, int, int32_t, htonl, ntohl);
                    break;
                case 'D':
                    /* there is no int64_t in ... in 32 system */
                    if ((job == DO_PACK) && (n == -1) && (from == FROM_ARG))
                        if (sizeof(char *) == sizeof(uint32_t))
                            return ERROR_NO_DDWORD_ARG;

                    GET_AND_SET(itmp8, int64_t, int64_t, htonll, ntohll);
                    break;
                case 'f':
                    GET_AND_SET(ftmp, double, float, NO_SWITCH, NO_SWITCH);
                    break;
                case 'F':
                    GET_AND_SET(ftmp, double, double, NO_SWITCH, NO_SWITCH);
                    break;
                default:
                    return ERROR_NOT_FORMAT;
            }
        }
    }

    return 0;
}

static int get_num(char **f)
{
    char buf[BUF_NUM_LEN];
    char *p = *f;
    char *b = buf;

    if (f == NULL || p == NULL)
        return ERROR_BAD_PARAM;

    while (isdigit(*p))
    {
        *b++ = *p++;
    }
    *b = '\0';
    *f = p;

    if (!isdigit(*buf))
        return -1;

    return atoi(buf);
}

static int bracket_match(char *s)
{
    char *p = s;
    int stack = 0;

    while (*p)
    {
        if (*p == '[')
            ++stack;
        else if (*p == ']')
            --stack;
        else
            ;

        ++p;
    }

    if (stack)
        return ERROR_NOT_MATCH;
    else
        return 0;
}

static int bracket_pass(char **s)
{
    char *p = *s;
    int stack = 1;

    while (*p)
    {
        if (*p == '[')
            ++stack;
        else if (*p == ']')
        {
            --stack;
            if (stack == 0)
            {
                *s = p + 1;
                return 0;
            }
        }
        else
            ;

        ++p;
    }

    return ERROR_NOT_MATCH;
}

static int handle_format(char **net, int *left_len, char *fmt, \
                          int from, va_list *va, char **locale, int job)
{
    int i, n, ret, buf_len = *left_len;
    char type;
    char *f = fmt;

    while (*f)
    {
        n = get_num(&f);
        type = *(f++);
        if (!type)
            return ERROR_EXPECT_FORMAT;

        /* printf("n = %d, type = %c\n", n,  type); */

        switch (type)
        {
            case '[':
                if (from == FROM_ARG)
                    *locale = va_arg(*va, char *);

                if (n == -1)
                    n = 1;

                for (i = 0; i < n; ++i)
                {
                    ret = handle_format(net, left_len, f, \
                                        FROM_PTR, NULL, locale, job);

                    if (ret < 0)
                        return ret;
                }

                ret = bracket_pass(&f);
                if (ret < 0)
                    return ret;

                break;
            case ']':
                return 0;
            default:
                ret = process_data(net, left_len, type, n, \
                                   from, va, locale, job);
                if (ret < 0)
                    return ret;

                break;
        }
    }

    return buf_len - *left_len;
}

static int __pack(char *const ptr, size_t max_len, char *format, \
                  va_list va, int job)
{
    int ret, left_len = max_len;
    char *p = ptr;
    char *f = format;
    char *locale = NULL;
    va_list _va;

    if (ptr == NULL || max_len < 0 || format == NULL)
        return -1;

    ret = bracket_match(f);
    if (ret < 0)
        return ret;

    va_copy(_va, va);

    ret = handle_format(&p, &left_len, f, FROM_ARG, &_va, &locale, job);

    return ret;
}

int pack(char *const ptr, size_t max_len, char *format, ...)
{
    va_list va;
    int ret;

    va_start(va, format);
    ret = __pack(ptr, max_len, format, va, DO_PACK);
    va_end(va);

    return ret;
}

int vpack(char **ptr, int *left_len, char *format, ...)
{
    va_list va;
    int ret;

    va_start(va, format);
    ret = __pack(*ptr, *left_len, format, va, DO_PACK);
    va_end(va);

    if (ret >= 0)
    {
        *ptr += ret;
        *left_len -= ret;
    }

    return ret;
}

int unpack(char *const ptr, size_t max_len, char *format, ...)
{
    va_list va;
    int ret;

    va_start(va, format);
    ret = __pack(ptr, max_len, format, va, DO_UNPACK);
    va_end(va);

    return ret;
}

int vunpack(char **ptr, int *left_len, char *format, ...)
{
    va_list va;
    int ret;

    va_start(va, format);
    ret = __pack(*ptr, *left_len, format, va, DO_UNPACK);
    va_end(va);

    if (ret >= 0)
    {
        *ptr += ret;
        *left_len -= ret;
    }

    return ret;
}

