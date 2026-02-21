# packf：用格式字符串驱动的 C 二进制序列化库

网络编程中有一件事情永远绕不开：把内存里的数据结构转成字节流，再从字节流还原回来。这件事听起来简单，做起来却有无数细节——字节序、变长字段、结构体对齐、缓冲区边界……每个项目都在重复解决同一组问题。

2012 年，我刚大学毕业，在时间压力下写了 [packf](https://github.com/haipome/packf)，一个用格式字符串驱动的 C 二进制序列化库。整个库只有一个 `.c` 和一个 `.h`，不依赖任何外部库。

---

## 为什么要造这个轮子

最直接的答案是：没有找到合适的。

当时的选择大概是这些：

- **手写**：每个协议字段手动调用 `htons`/`htonl`，代码和协议文档同步维护，改一处漏一处
- **protobuf**：引入了 protoc 编译器、proto 文件、运行时库，对于一个纯 C 的嵌入式/服务端项目来说太重
- **XDR/ASN.1**：工具链复杂，学习成本高，不适合快速迭代

我需要的是一个零依赖、能直接集成进项目的小工具，同时要足够通用，能处理真实协议里常见的模式：变长字符串、变长数组、嵌套结构体。

---

## 核心思想：向 printf 借鉴

C 语言里最成功的"小型 DSL"是什么？是 `printf` 的格式字符串。

```c
printf("%d %s %.2f", count, name, price);
```

一行描述了数据的类型和布局，可读性很好，表达能力也够用。packf 的 API 照搬了这个思路：

```c
// 序列化
int len = packf(buf, sizeof(buf), "w d D", 0x1234, 0xDEADBEEF, 0x123456789ABCull);

// 反序列化
uint16_t a; uint32_t b; uint64_t c;
unpackf(buf, len, "w d D", &a, &b, &c);
```

格式字符串描述协议布局，函数自动处理字节序转换、边界检查、指针推进。

---

## 格式字符串语法

完整的格式 token 是：

```
[-=][num]type
```

### 基本类型

| 字符 | 线上字节数 | 对应 C 类型 |
|------|-----------|-------------|
| `c`  | 1 | `int8_t` / `uint8_t` |
| `w`  | 2 | `int16_t` / `uint16_t` |
| `d`  | 4 | `int32_t` / `uint32_t` |
| `D`  | 8 | `int64_t` / `uint64_t` |
| `f`  | 4 | `float` |
| `F`  | 8 | `double` |
| `s`  | 变长 | null 结尾字符串 |
| `a`  | 变长 | 填零/对齐字节 |
| `[…]` | 变长 | 结构体 |

所有多字节类型自动按大端（网络字节序）转换，不需要手动调 `htons`/`htonl`。

### 数组

在类型前加数字表示数组：

```c
int32_t arr[5] = {10, 20, 30, 40, 50};
packf(buf, sizeof(buf), "5d", arr);  // 序列化 5 个 int32_t，共 20 字节
```

### LV 字段（变长字段的核心）

真实协议里最常见的模式是"先传长度，再传内容"。`-` 和 `=` 前缀处理这种模式：

- `-`：1 字节长度前缀
- `=`：2 字节长度前缀

```c
// 序列化：先给出实际元素个数，再给出数组指针；num=64 是上限
packf(buf, sizeof(buf), "-64d", 5, arr);
// 线上格式：[0x05][10][20][30][40][50]（1 + 20 = 21 字节）

// 反序列化：先拿到长度，再拿到数据
uint8_t count; int32_t out[64];
unpackf(buf, 21, "-64d", &count, out);
// count = 5, out = {10, 20, 30, 40, 50}
```

字符串也一样：

```c
packf(buf, sizeof(buf), "-128s", "hello");
// 线上：[0x05][h][e][l][l][o]（不含 \0，共 6 字节）
```

---

## 结构体支持：`[…]` 语法

协议通常不是一堆散字段，而是有嵌套层次的结构体。packf 支持直接传结构体指针：

```c
#pragma pack(1)
struct Point { int32_t x; int32_t y; };
struct Packet {
    uint16_t version;
    struct Point origin;
    float value;
};
#pragma pack()

struct Packet pkt = {1, {100, 200}, 3.14f};
int len = packf(buf, sizeof(buf), "[w [dd] f]", &pkt);
```

`[…]` 里的格式字符串对应结构体内存布局，packf 会按顺序从结构体指针读取（或写入）每个字段，并做字节序转换。

`#pragma pack(1)` 是必要条件——它消除编译器的对齐填充，确保内存布局和协议格式完全对应。

---

## 最难设计的部分：`s` 与 `S` 的区别

这是整个库里最微妙的地方，也是真实工程中会遇到的典型问题。

考虑一个常见的协议结构体：

```c
#pragma pack(1)
struct User {
    uint32_t uid;
    uint8_t  name_len;
    char     name[100];
    char     passwd[30];
};
#pragma pack()
```

`name` 字段在内存里占 100 字节，但实际字符串可能只有 9 个字符。`passwd` 同理。

这里有两种需求：

1. **固定宽度**（`Ns`）：线上永远发 N 字节，不够的补 `\0`。接收方知道固定长度，解析简单，但浪费带宽。
2. **紧凑传输**（`NS`）：线上只发实际字符串（含 `\0`），节省带宽，但接收方需要依赖上层帧边界。

`S` 专门解决这个问题：线上字节数等于 `strlen + 1`（紧凑），但本地结构体指针推进 N 字节（匹配 `char passwd[30]` 的实际占用）。

```
内存中的 passwd:  [b][a][z][i][n][g][a][\0][\0]..[\0]   ← 30 字节
30s 线上格式:     [b][a][z][i][n][g][a][\0][\0]..[\0]   ← 始终 30 字节
30S 线上格式:     [b][a][z][i][n][g][a][\0]             ← 只有 8 字节
```

这个细节在任何关于序列化的教程里都不会提，但做过实际协议开发的人一定碰到过这个问题。

---

## 游标变体：vpackf / vunpackf

实际组包时往往需要多步完成，比如先写消息头，再写消息体，最后回填长度。packf 提供了游标变体：

```c
char buf[256];
void *pos = buf;
int  left = sizeof(buf);

vpackf(&pos, &left, "w", (int)0x0001);    // 消息类型
vpackf(&pos, &left, "d", (int)0);          // 长度占位
vpackf(&pos, &left, "-32s", "hello");      // 实际载荷

int total = sizeof(buf) - left;
```

每次调用后 `pos` 自动推进，`left` 自动减少，不需要手动维护偏移量。

---

## 实现细节：几个值得关注的地方

### 浮点字节序

整数的字节序转换很直接，但浮点数不能直接用整数的位操作——类型别名（type aliasing）在 C 里是未定义行为。正确做法是用 union：

```c
static float __bswap_f(float x) {
    union { float f; uint32_t i; } u = { x };
    u.i = bswap_32(u.i);
    return u.f;
}
```

这是 C99 标准允许的方式，比 `*(uint32_t *)&x` 的 reinterpret_cast 写法安全。

### 嵌套格式字符串的解析

`[…]` 支持任意嵌套，实现上用了一个简单的括号栈计数。处理完 `[` 对应的内容后，扫描格式字符串跳到对应的 `]`：

```c
bracket_stack = 1;
while (bracket_stack) {
    if (*f == '[') ++bracket_stack;
    else if (*f == ']') --bracket_stack;
    ++f;
}
```

递归调用 `__packf` 处理结构体内部，然后跳过对应的 `]` 继续外层。

### 错误定位

解析格式字符串出错时，`packf_error_format` 指向出错位置：

```c
extern char *packf_error_format;
```

结合 `packf_print_error = 1` 可以直接把出错的格式字符串片段打印到 stderr，调试时很方便。

---

## 一个完整的例子

下面是 `test.c` 里的场景——一个包含嵌套结构体和各种字段类型的消息：

```c
#pragma pack(1)
struct user {
    uint32_t  uid;
    uint8_t   name_len;
    char      name[100];
    uint64_t  key;
    char      passwd[30];
};

struct users {
    uint32_t    type;
    uint16_t    user_num;
    struct user user[10];
    uint16_t    n;
} users;
#pragma pack()

// 填充数据 ...

// 序列化：type(d) + user数组(=10[...]) + 16字节填零(16a)
int len = packf(buf, sizeof(buf),
    "[d  =10[d -100s D 30S]  w]  16a",
    &users);

// 反序列化
struct users out;
unpackf(buf, len,
    "[d  =10[d -100s D 30S]  w]  16a",
    &out);
```

格式字符串里：
- `=10[...]`：2 字节元素个数 + 最多 10 个 user 结构体
- `-100s`：1 字节长度前缀 + name 内容（最大 100 字节含 `\0`）
- `30S`：紧凑 passwd（线上 strlen+1 字节，结构体指针推进 30 字节）
- `16a`：16 字节填零（对齐/版本保留字段）

---

## 集成方式

只需把两个文件复制进项目：

```sh
gcc -o test test.c packf.c
```

没有 CMake，没有 configure，没有依赖。

> 注意：packf 依赖 `<endian.h>` 和 `<byteswap.h>`，目前仅支持 Linux。macOS 和 Windows 需要自行提供等价的字节序接口。

---

## 局限性

坦白说，这个库有几个明显的局限：

1. **Linux 限定**：`<endian.h>` / `<byteswap.h>` 是 Linux 专属，跨平台需要适配层
2. **非线程安全**：`packf_error_format` 是全局变量，多线程并发会有竞态
3. **格式字符串在运行时解析**：没有编译期类型检查，参数和格式不匹配只能靠测试发现

这些在写的时候都清楚，在当时的使用场景（Linux 服务端，单线程或进程隔离）下都不是问题。

---

## 最后

packf 到现在还运行在某些生产环境里。它不是最通用的解决方案，但在它的定位上——零依赖、格式字符串驱动、处理真实网络协议——它做了它该做的事。

代码在 [GitHub](https://github.com/haipome/packf)，公有领域授权，随意取用。
