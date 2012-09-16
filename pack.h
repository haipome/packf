/*
 *    Filename: pack.c
 * Description: 支持数组和结构体的网络数据打包和解包封装函数
 *      Coding: GBK
 *     History: damonyang@tencent, 2012/09/04, create
 */

# ifndef _PACK_H
# define _PACK_H

# define swap_ddword(x)  \
   ((((x) & 0xff00000000000000llu) >> 56) | \
    (((x) & 0x00ff000000000000llu) >> 40) | \
    (((x) & 0x0000ff0000000000llu) >> 24) | \
    (((x) & 0x0000ff0000000000llu) >> 24) | \
    (((x) & 0x000000ff00000000llu) >> 8)  | \
    (((x) & 0x00000000ff000000llu) << 8)  | \
    (((x) & 0x0000000000ff0000llu) << 24) | \
    (((x) & 0x000000000000ff00llu) << 40) | \
    (((x) & 0x00000000000000ffllu) << 56) )

extern uint64_t endian_switch(uint64_t a);

# define htonll(x) endian_switch((x))
# define ntohll(x) endian_switch((x))

/*
 * 提供一种类似与 sprintf 和 ssancf 函数的格式化网络数据打包和解包函数，方
 * 便的将各种数据类型（包括结构体和数组）转换为本地序或网络序。
 * 兼容 32 位机和 64 位机。
 */

/*
 * format 格式：
 * ----------------------------------------------------------------------
 *           |   s    |    字符串
 *           |   a    |    空字符 ('\0')
 *     n     |   c    |    char   (uint8_t)
 *  (choice) |   w    |    word   (uint16_t)
 *  (n >= 0) |   d    |    dword  (uint32_t)
 *           |   D    |    ddword (uint64_t) 32 位机不支持 8 字节整数的
 *           |        |    参数传递方式，如需请使用数组或结构体方式
 *           |   f    |    float  (4 bytes)
 *           |   F    |    double (8 bytes)
 *           |   [    |    结构体开始
 * ----------------------------------------------------------------------
 *           |   ]    |    结构体结束
 * ----------------------------------------------------------------------
 */

/*
 * note：
 * 1、对于 acwdD[fF, 当 n 存在时表示数组，参数为数组或指针
 * 2、对于 s, 当 n 存在时代表字符串的长度，否则长度按照 strlen + 1 计算
 * 3、对于 [] 结构体，参数应为结构体指针
 * 4、结构体必须在 # pragma pack(1) 与 # pragma pack(0) 之间定义！
 * 5、结构体支持嵌套，即结构体中包含结构体
 */

/*
 * pack: 按照 format 定义的格式将变参中数据转换为网络字节序的二进制数据，放入
 *       ptr 指向的地址，
 * max_len: ptr 指向的区域最大可用地址
 *
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *      < 0  : 失败
 */

extern int pack(char *const ptr, size_t max_len, char *format, ...);

/*
 * vpack: 和 pack 功能相同，不同之处为函数若执行成功，*ptr += pack(...)
 */

extern int vpack(char **ptr, int *left_len, char *format, ...);

/*
 * unpack: 解包，将网络序的二进制数据按照 format 定义的格式解析，转换为本地序
 *         后将值放入变参中的指针指向的存储地址。
 * note: na 代表跳过网络序二进制数据 n 个字节
 *
 * 返回值：
 *      >= 0 : 成功，返回打包的数据总长度
 *      < 0  : 失败
 */

extern int unpack(char *const ptr, size_t max_len, char *format, ...);

/*
 * 同 vpack
 */

extern int vunpack(char **ptr, int *left_len, char *format, ...);

/*
 * 错误码：
 * -221: 传入参数错误，指针为 NULL or max_len < 0
 * -222: ptr 指向的存储区长度不够
 * -223: 变参中指针有 NULL
 * -224: as 类型必须前缀数字
 * -225: 未知 format 类型
 * -226: 缺少参数
 * -227:  ] 不匹配
 * -228: 32 位机不支持变参中传递 8 字节长整数
 */


/*
 * example:
 *     char pkg[8096];
 *     int len;
 *     uint64_t ddword[2] = {0, 1};
 *     len = pack(pkg, 8096, "cwd2DfF10s16a", 'b', 2, 3, \
 *                4.0, 5.1, ddword, "hello pack!");
 *     if (len < 0)
 *         error...
 *
 *     char a;
 *     short b;
 *     uint32_t c;
 *     float d;
 *     double e;
 *     char s[20];
 *     len = unpack(pkg, 8096, "cwd2DfF10s16a", &a, &b, &c, &d, &e, ddword, s);
 *     if (len < 0)
 *         error...
 *
 *    -------------------------
 *    # pragma pack(1)
 *    struct st1 {
 *        int a;
 *    }
 *    struct st2 {
 *        char *p;
 *        struct st1;
 *    } test[2];
 *    # pragma pack(0)
 *
 *    len = pack(pkg, 8096, "2[d[d]]", test);
 *     if (len < 0)
 *         error...
 */

# endif

