/*
 * Description: 支持数组和结构体的网络数据打包和解包函数
 *              CS 和 OIDB 协议打包和解包的封装函数
 *     History: damonyang@tencent.com, 2012/09/04, create
 *              damonyang@tencent.com, 2012/11/06, revise for commlib
 *              damonyang@tencent.com, 2012/12/11, add support to LV
 *              damonyang@tencent.com, 2013/01/30, add v(un)packa
 */

# ifndef _OI_PACK_H
# define _OI_PACK_H

# include <stdint.h>
# include <stdarg.h>
# include <netinet/in.h>

# ifdef  __cplusplus
extern "C"
{
# endif

/*
 * packf, vpackf 和其对应的 unpackf, vunpackf 函数提供一种类似与 sprintf
 * 和 ssancf 函数的格式化网络数据打包和解包函数，只需要传入描述数据的格式
 * 字符串，即可方便的将各种数据类型（包括结构体和数组）转换为本地序或网络
 * 序，用于网络传输。
 * 兼容 32 位机和 64 位机。
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
 *          packf(buf, sizeof(buf), "-64d", 5, array)
 *          其中 5 表示 array 中数据的长度，64 表示数组的最大长度。
 *          这将会在 buf 中打包 21 各字节（1 字节长度加 20 字节数据），返回值为 21
 *
 *      3): 对于 s 类型，不需要指定 len 的具体长度，而以 strlen 来确定
 *          example: packf(buf, sizeof(buf), "-128s", "damonyang");
 *          其中 128 代表字符串的最大长度（包含结尾字符 0）。
 *          这将会在 buf 中打包 10 各字节，返回值为 10（1 字节长度加 9 字节字符）
 *          **但当 s 出现在结构体中时，前面需要有 len 值，但不需要赋值（为了使
 *          结构体定义与协议格式保持一致）
 *
 *      4): 对于 [ 结构体类型，如果 num 不存在，不需要指定 len, 长度以实际打包的
 *          结构体长度确定，不同于 s ，即使在结构体中也不需要有 len 值。
 *          如果 num 存在，和 2）相同。
 *
 *
 * 2、当 - 或 = 不存在时
 *      1): 对于 acwdD[fF, 当 num 存在时表示数组，参数为数组或指针
 *
 *      2): 对于 s, 当 num 存在时代表字符串的长度，否则长度按照 strlen + 1 计算
 *
 * 3、[] 表示结构体，参数应为结构体指针
 *      1): 结构体必须在 # pragma pack(1) 与 # pragma pack(0) 之间定义！
 *
 *      2): 结构体支持嵌套，即结构体中包含结构体
 *
 * example:
 *      char buf[256];
 *
 * # pragma pack(1)
 *      struct user {
 *          uint32_t uin;
 *          uint8_t name_len;
 *          char name[100];
 *          uint64_t key;
 *          char passwd[30];
 *      };
 *
 *      struct users {
 *          uint32_t t;
 *          uint16_t user_num;
 *          struct user user[10];
 *      } users;
 * # pragma pack(0)
 *
 *      users.t = 0;
 *      users.user_num = 1;
 *      users.user[0].uin = 506401056;
 *      strcpy(users.user[0].name, "damonyang");
 *      users.user[0].key = 1;
 *      strcpy(users.user[0].passwd, "123456789");
 *
 *      packf(buf, sizeof(buf), "cwdDfF [ d =10[d -100s D 30s] ] 16a", \
 *          0xa, 1, 2, 155734042726471llu, 3.4, 5.6, &users);
 */

/*
 * 函数：packf : pack format
 * 功能: 打包，按照 format 定义的格式将变参中数据转换为网络字节序的二进制数据
 * 参数:
 *       ptr: 缓冲区地址
 *       max_len: ptr 指向的缓冲区域最大长度
 *       format: 格式字符串，和后面的变参对应
 *       ...: 变参，和 format 对应
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *      < 0  : 失败
 */

extern int packf(void *const ptr, size_t max_len, char *format, ...);

/*
 * 函数：unpackf : unpack format
 * 功能: 解包，将网络序的二进制数据按照 format 定义的格式解析，转换为本地序
 * 参数：
 *      ptr: 网络序二进制数据起始地址
 *      max_len: 网络序二进制数据长度
 *      format: 描述二进制数据的格式字符串
 *      ...: 变参，参数应为指针
 * 返回值：
 *      >= 0 : 成功，返回解包的数据总长度
 *      < 0  : 失败
 */

extern int unpackf(void *const ptr, size_t max_len, char *format, ...);

/*
 * 函数：vpackf : variable pack format
 * 功能：同 packf
 * 参数：
 *      ptr: 指向指向缓冲区地址指针变量的指针
 *      letf_len: 指向缓冲区剩余长度变量的指针
 *      format: 格式字符串，和后面的变参对应
 *      ...: 变参，和 format 对应
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *          ptr: *ptr 指向缓冲区接下来可用地址
 *          left_len: *left_len 为缓冲区剩余长度
 *      < 0  : 失败
 */

extern int vpackf(void **ptr, int *left_len, char *format, ...);

/*
 * 函数：vunpackf : variable unpack format
 * 功能：同 unpack
 * 参数：
 *      ptr: 指向指向网络序二进制数据地址指针变量的指针
 *      left_len: 指向网络序二进制数据剩余长度变量的指针
 *      format: 描述二进制数据的格式字符串
 *      ...: 变参，参数应为指针
 * 返回值：
 *      >= 0 : 成功，返回解包的数据总长度
 *          ptr: *ptr 指向网络序二进制数据接下来的地址
 *          left_len: *left_len 为网络序二进制数据剩余长度
 *      < 0  : 失败
 */

extern int vunpackf(void **ptr, int *left_len, char *format, ...);

/*
 * 函数：vpackn : variable pack buffer of certain length
 * 功能：将一段固定长度的 buffer 打包
 * 参数：
 *      ptr: 指向指向缓冲区地址指针变量的指针
 *      letf_len: 指向缓冲区剩余长度变量的指针
 *      buf: buffer 的地址
 *      len: buffer 的长度
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *          ptr: *ptr 指向缓冲区接下来可用地址
 *          left_len: *left_len 为缓冲区剩余长度
 *      < 0  : 失败
 */

extern int vpackn(void **ptr, int *left_len, void *buf, size_t len);

/*
 * 函数：vunpackn : variable unpack buffer of certain length
 * 功能：解包一段固定长度的 buffer
 * 参数：
 *      ptr: 指向指向网络序二进制数据地址指针变量的指针
 *      left_len: 指向网络序二进制数据剩余长度变量的指针
 *      buf: buffer 的地址
 *      len: buffer 的长度
 * 返回值：
 *      >= 0 : 成功，返回解包的数据总长度
 *          ptr: *ptr 指向网络序二进制数据接下来的地址
 *          left_len: *left_len 为网络序二进制数据剩余长度
 *      < 0  : 失败
 */

extern int vunpackn(void **ptr, int *left_len, void *buf, size_t len);

extern int vpacka(void **ptr, int *left_len, char *format, va_list arg);
extern int vunpacka(void **ptr, int *left_len, char *format, va_list arg);

/* 64 位整数字节序转换函数 */
extern uint64_t ddword_endian_switch(uint64_t a);

# ifndef htonll
# define htonll(x) ddword_endian_switch(x)
# endif
# ifndef ntohll
# define ntohll(x) ddword_endian_switch(x)
# endif

extern float float_endian_switch(float a);

# ifndef htonf
# define htonf(x) float_endian_switch(x)
# endif
# ifndef ntohf
# define ntohf(x) float_endian_switch(x)
# endif

extern double double_endian_switch(double a);

# ifndef htond
# define htond(x) double_endian_switch(x)
# endif
# ifndef ntohd
# define ntohd(x) double_endian_switch(x)
# endif

# ifndef NEG_RET_LN
# define NEG_RET_LN(x) do { if ((x) < 0) return -__LINE__; } while (0)
# endif

# ifndef STR_OF
# define __STR_OF__(s) #s
# define STR_OF(s) __STR_OF__(s)
# endif

/* 跳过指定长度 */
# define PACK_PASS(ptr, left_len, size) do {        \
    (ptr) = (char*)(ptr) + (size);                  \
    (left_len) = (int)(left_len) - (size);          \
} while (0)

/*
 * 错误码：
 *
 * -221: 传入参数错误
 * -222: 缓冲区不足
 * -223: NULL 指针
 * -224: format 未知类型
 * -225: format 格式错误参数
 * -226: format ] 不匹配
 */

# ifdef  __cplusplus
}
# endif

# endif

