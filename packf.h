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
 * packf, vpackf 和其对应的 unpackf, vunpackf 函数提供一种类似于 sprintf
 * 和 ssancf 函数的格式化的序列化与反序列化函数，只需要传入描述数据的格式
 * 字符串，即可方便的将各种数据类型（包括结构体和数组）转换为本地序或网络
 * 序，用于网络传输。
 *
 * format: [-=][num]type     [] 表示可选
 *
 * ---------------------------------------------------------------
 *  type   |    means
 * ---------------------------------------------------------------
 *    s    |    字符串 ('\0' end)
 *    a    |    空字符 ('\0')
 *    c    |    char   (int8_t  | uint8_t)
 *    w    |    word   (int16_t | uint16_t)
 *    d    |    dword  (int32_t | uint32_t)
 *    D    |    ddword (int64_t | uint64_t) (c99)
 *    f    |    float  (4 bytes)
 *    F    |    double (8 bytes)
 *    [    |    结构体开始
 * --------------------------------------------------------------
 *    ]    |    结构体结束
 *   空格  |    使用空格让格式串更美观，不能用在 -/= num 和 type 之间
 * --------------------------------------------------------------
 *
 * note：
 * 1、当 - 或 = 存在时，表示 LV (len + value) 字段，num 存在代表 value 的最大数量
 *      1): - 表示长度字段为 1 字节，= 表示长度字段为 2 字节
 *
 *      2): 对于 acwdDfF 类型，在 value 前需要指定 len
 *          example: char buf[1024]; int array[64];
 *          packf(buf, sizeof(buf), "-64d", 5, array);
 *          其中 5 表示 array 中数据的长度，64 表示数组的最大长度。
 *          这将会在 buf 中打包 21 各字节（1 字节长度加 20 字节数据），返回值为 21.
 *
 *      3): 对于 s 类型，不需要指定 len 的具体长度，而以 strlen 来确定
 *          example: packf(buf, sizeof(buf), "-128s", "damonyang");
 *          其中 128 代表字符串的最大长度（包含结尾字符 '\0'）。
 *          这将会在 buf 中打包 10 各字节，返回值为 10（1 字节长度加 9 字节字符，
 *          不包含结尾字符 '\0'）
 *          **但当 s 出现在结构体中时，前面需要定义有 len 值，但不需要赋值（为了使
 *          结构体定义与协议格式保持一致）。
 *
 *      4): 对于 [ 结构体类型，如果 num 不存在，不需要指定 len, 长度以实际打包的
 *          结构体长度确定。和 s 相同，当这种结构出现于结构体中时，前面需要定义有
 *          len 值，但不需要赋值。
 *          如果 num 存在，和 2）相同。
 *
 * 2、当 - 或 = 不存在时
 *      1): 对于 acwdD[fF, 当 num 存在时表示数组，参数为数组或指针。
 *
 *      2): 对于 s, 当 num 存在时代表字符串的长度，如果字符串实际长度小于 num，则
 *          在剩余的部分补 '\0'. 如果 num 不存在，长度按照 strlen + 1 计算。
 *
 * 3、[] 表示结构体，参数应为结构体指针
 *      1): 结构体必须在 # pragma pack(1) 与 # pragma pack() 之间定义！
 *
 *      2): 结构体支持嵌套，即结构体中包含结构体。
 */

enum
{
    PACKF_OUT_OF_BUF   = -1,
    PACKF_NOT_FORMAT   = -2,
    PACKF_FORMAT_ERROR = -3,
    PACKF_BE_CUT_OFF   = -4,
    PACKF_NULL_POINTER = -5,
};

/*
 * 当发生错误时，如果 packf_error_format 不为 NULL，其指向发生错误的 format.
 * 此时可以通过打印字符串 packf_error_format 帮助定位错误位置。
 */
extern char *packf_error_format;

/*
 * 如果 packf_print_error 为非 0 值，则在发生错误时，在标准出错打印错误信息
 */
extern int packf_print_error;

/*
 * 函数：packf : pack format
 * 功能: 打包，按照 format 定义的格式将变参中数据转换为网络字节序的二进制数据
 * 参数:
 *       dest:   目标缓冲区地址
 *       max:    dest 指向的缓冲区长度
 *       format: 格式字符串，和后面的变参对应
 *       ...:    变参，和 format 对应
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *      < 0  : 失败
 */
extern int packf(void *dest, size_t max, char *format, ...);

/*
 * 函数：unpackf : unpack format
 * 功能: 解包，将网络序的二进制数据按照 format 定义的格式解析，转换为本地序
 * 参数：
 *      src:    网络序二进制数据起始地址
 *      max:    src 指向的网络序二进制数据长度
 *      format: 描述网络序二进制数据的格式字符串
 *      ...:    变参，参数应为指针
 * 返回值：
 *      >= 0 : 成功，返回解包的数据总长度
 *      < 0  : 失败
 */
extern int unpackf(void *src, size_t max, char *format, ...);

/*
 * 函数：vpackf : variable pack format
 * 功能：类似 packf, 但传入的参数为指向当前缓冲区指针的指针，和当前剩余长度的
 *       指针，并在成功执行后更新它们。
 * 参数：
 *      current: 缓冲区当前位置
 *      left:    缓冲区当前剩余长度
 *      format:  格式字符串，和后面的变参对应
 *      ...:     变参，和 format 对应
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *             *current: 指向缓冲区接下来可用地址
 *             *left:    缓冲区剩余长度
 *      < 0  : 失败
 */
extern int vpackf(void **current, int *left, char *format, ...);

/*
 * 函数：vunpackf : variable unpack format
 * 功能：类似 unpack, 但传入的参数为指向当前网络数据指针的指针，和当前网络数据
 *       的剩余长度，并在成功执行后更新它们。
 * 参数：
 *      current: 网络序二进制数据当前位置
 *      left:    网络序二进制数据当前剩余长度
 *      format:  描述网络序二进制数据的格式字符串
 *      ...:     变参，参数应为指针
 * 返回值：
 *      >= 0 : 成功，返回解包的数据总长度
 *             *current: 指向网络序二进制数据接下来的地址
 *             *left:    网络序二进制数据剩余长度
 *      < 0  : 失败
 */
extern int vunpackf(void **current, int *left, char *format, ...);

/*
 * 函数：vpackn : variable pack buffer of n length
 * 功能：将一段固定长度的 buffer 打包
 * 参数：
 *      current: 缓冲区当前位置
 *      left:    缓冲区当前剩余长度
 *      buf:     buffer 的地址
 *      n:       buffer 的长度
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *             *current: 指向缓冲区接下来可用地址
 *             *left:    缓冲区剩余长度
 *      < 0  : 失败
 */
extern int vpackn(void **current, int *left, void *buf, size_t n);

/*
 * 函数：vunpackn : variable unpack buffer of n length
 * 功能：解包一段固定长度的 buffer
 * 参数：
 *      current: 网络序二进制数据当前位置
 *      left:    网络序二进制数据当前剩余长度
 *      buf:     buffer 的地址
 *      n:       buffer 的长度
 * 返回值：
 *      >= 0 : 成功，返回解包的数据总长度
 *             *current: 指向网络序二进制数据接下来的地址
 *             *left:    网络序二进制数据剩余长度
 *      < 0  : 失败
 */
extern int vunpackn(void **current, int *left, void *buf, size_t n);

extern int vpacka(void **current, int *left, char *format, va_list arg);
extern int vunpacka(void **current, int *left, char *format, va_list arg);

/* 如果结果为负值则返回负的行号 */
# ifndef NEG_RET_LN
# define NEG_RET_LN(x) do { if ((x) < 0) return -__LINE__; } while (0)
# endif

/* 
 * 将一个宏定义的数字转换为字符串
 * example: 
 * # define MAX_LEN 1024
 * STR_OF(MAX_LEN) 等价于 "1024"
 */
# ifndef STR_OF
# define __STR_OF__(s) #s
# define STR_OF(s) __STR_OF__(s)
# endif

/* 跳过指定长度 */
# define PACK_PASS(ptr, left_len, size) do {        \
    (ptr) = (char*)(ptr) + (size);                  \
    (left_len) = (int)(left_len) - (size);          \
} while (0)

# ifdef  __cplusplus
}
# endif

# endif

