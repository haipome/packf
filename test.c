# include <stdio.h>
# include <stdint.h>
# include <string.h>
# include <assert.h>
# include <limits.h>

# include "packf.h"

void bin_dump(void *pkg, int len)
{
    int i, j = 0;

    printf("len = %d\n", len);
    if (len > 0)
    {
        for (i = 0; i < len; ++i)
        {
            printf("%02X  ", ((uint8_t *)pkg)[i]);
            ++j;
            if (!(j % 20))
                putchar('\n');
        }
    }
    if (j % 20)
        putchar('\n');
}

static void test_roundtrip(void)
{
    char buf[8096];

# pragma pack(1)
    struct user
    {
        uint32_t        uid;
        uint8_t         name_len;
        char            name[100];
        uint64_t        key;
        char            passwd[30];
    };

    struct users
    {
        uint32_t        type;
        uint16_t        user_num;
        struct user     user[10];
        uint16_t        n;
    } users;
# pragma pack()

    users.type          = 0;
    users.user_num      = 1;
    users.user[0].uid   = 506401056;
    users.user[0].key   = 0x0102030405060708llu;
    strcpy(users.user[0].name,   "haipoyang");
    users.user[0].name_len = (uint8_t)strlen(users.user[0].name);
    strcpy(users.user[0].passwd, "bazinga");
    users.n             = 10;

    int len = packf(buf, sizeof(buf), "cwdDfF[d =10[d -100s D 30S] w]16a",
         0xa, 1, 2, 237417076350464llu, 3.4, 5.6, &users);
    assert(len == 81);

    bin_dump(buf, len);

    struct users ue;
    memset(&ue, 0, sizeof(ue));
    int r = unpackf(buf, (size_t)len, "27a[d =10[d -100s D 30S] w]16a", &ue);
    assert(r == len && r > 0);
    assert(ue.type == users.type);
    assert(ue.user_num == users.user_num);
    assert(ue.user[0].uid == users.user[0].uid);
    assert(ue.user[0].name_len == users.user[0].name_len);
    assert(memcmp(ue.user[0].name, users.user[0].name, ue.user[0].name_len) == 0);
    assert(ue.user[0].key == users.user[0].key);
    assert(strcmp(ue.user[0].passwd, users.user[0].passwd) == 0);
    assert(ue.n == users.n);

    printf("roundtrip ok, n=%u\n", ue.n);
}

/* Issue 1: bare 's' must not strlen past the source buffer. */
static void test_unterminated_s(void)
{
    char wire[8];
    char out[64];

    memset(wire, 'A', sizeof(wire));
    memset(out, 0xCC, sizeof(out));

    int r = unpackf(wire, sizeof(wire), "s", out);
    assert(r == PACKF_OUT_OF_BUF);
    printf("unterminated s: ok (ret=%d)\n", r);
}

/* Issue 2: multi-byte fields at odd offsets must not trap under UBSan. */
static void test_unaligned_fields(void)
{
    char buf[64];
    uint16_t w_in = 0x1234, w_out = 0;
    uint32_t d_in = 0xDEADBEEFu, d_out = 0;
    uint64_t D_in = 0x0102030405060708ull, D_out = 0;

    /* Leading pad byte forces subsequent fields onto odd addresses. */
    int len = packf(buf, sizeof(buf), "a w d D", w_in, d_in, D_in);
    assert(len == 1 + 2 + 4 + 8);

    int r = unpackf(buf, (size_t)len, "a w d D", &w_out, &d_out, &D_out);
    assert(r == len);
    assert(w_out == w_in);
    assert(d_out == d_in);
    assert(D_out == D_in);
    printf("unaligned fields: ok\n");
}

/* Issue 3: size_t max larger than INT_MAX must be rejected. */
static void test_size_too_large(void)
{
    char buf[16];
    size_t huge = (size_t)INT_MAX + 1u;
    int r;

    r = packf(buf, huge, "c", 1);
    assert(r == PACKF_INVALID_ARG);

    r = unpackf(buf, huge, "c", buf);
    assert(r == PACKF_INVALID_ARG);

    {
        void *cur = buf;
        int left = -1;
        r = vpackf(&cur, &left, "c", 1);
        assert(r == PACKF_INVALID_ARG);
    }

    printf("size/left checks: ok\n");
}

/* Issue 4: negative LV length on pack must be rejected. */
static void test_negative_lv(void)
{
    char buf[64];
    int32_t arr[4] = {1, 2, 3, 4};
    int r = packf(buf, sizeof(buf), "-4d", -1, arr);
    assert(r == PACKF_INVALID_ARG);
    printf("negative lv: ok (ret=%d)\n", r);
}

/* Format count larger than INT_MAX must be rejected. */
static void test_format_num_overflow(void)
{
    char buf[16];
    int r;

    /* INT_MAX + 1 as a decimal count in the format string */
    r = packf(buf, sizeof(buf), "2147483648c");
    assert(r == PACKF_INVALID_ARG);

    /* INT_MAX itself must still parse (then fail on buffer size). */
    r = packf(buf, sizeof(buf), "2147483647a");
    assert(r == PACKF_OUT_OF_BUF);

    printf("format num overflow: ok\n");
}

/* vpack* must not advance the cursor when packing fails mid-way. */
static void test_vpack_cursor_on_failure(void)
{
    char buf[8];
    void *cur = buf;
    int left = (int)sizeof(buf);
    void *cur0 = cur;
    int left0 = left;
    int r;

    /* First byte fits; 8-byte D does not — overall call fails. */
    r = vpackf(&cur, &left, "c D", 1, (int64_t)0x0102030405060708LL);
    assert(r < 0);
    assert(cur == cur0);
    assert(left == left0);

    printf("vpack cursor on failure: ok\n");
}

/* unpack "-[...]": the declared struct length must be bounded by the input
 * and consumed from it, so later fields cannot read past the buffer. */
static void test_lv_struct_unpack_bounds(void)
{
    uint8_t ok_wire[5]  = {4, 0x00, 0x00, 0x00, 0x2A};
    uint8_t big_wire[5] = {200, 0x00, 0x00, 0x00, 0x2A};
    uint32_t body = 0, next = 0;
    int r;

    /* Return value must count the struct body, not just the prefix. */
    r = unpackf(ok_wire, sizeof(ok_wire), "-[d]", &body);
    assert(r == 5);
    assert(body == 0x2A);

    /* Nothing left after the struct: the trailing d must fail. */
    r = unpackf(ok_wire, sizeof(ok_wire), "-[d] d", &body, &next);
    assert(r == PACKF_OUT_OF_BUF);

    /* Declared length larger than the input. */
    r = unpackf(big_wire, sizeof(big_wire), "-[d]", &body);
    assert(r == PACKF_OUT_OF_BUF);

    printf("lv struct unpack bounds: ok\n");
}

/* pack "-[...]": the length prefix must be reserved before the body is
 * written, so the body cannot overflow the destination. */
static void test_lv_struct_pack_bounds(void)
{
    uint8_t buf[8];
    uint32_t body = 0x11223344u;
    int r;

    memset(buf, 0xAA, sizeof(buf));
    r = packf(buf, 4, "-[d]", &body);
    assert(r == PACKF_OUT_OF_BUF);
    assert(buf[4] == 0xAA);

    r = packf(buf, 5, "-[d]", &body);
    assert(r == 5);
    assert(buf[0] == 4);

    printf("lv struct pack bounds: ok\n");
}

/* An LV length that does not fit its 1/2-byte prefix must be rejected,
 * not silently truncated. */
static void test_lv_len_prefix_overflow(void)
{
    static uint8_t buf[1024];
    static uint8_t bytes[300];
    static char str[301];
    int r;

    memset(bytes, 1, sizeof(bytes));
    memset(str, 'x', 300);
    str[300] = '\0';

    r = packf(buf, sizeof(buf), "-300c", 300, bytes);
    assert(r == PACKF_BE_CUT_OFF);
    r = packf(buf, sizeof(buf), "=300c", 300, bytes);
    assert(r == 2 + 300);

    r = packf(buf, sizeof(buf), "-s", str);
    assert(r == PACKF_BE_CUT_OFF);
    r = packf(buf, sizeof(buf), "=s", str);
    assert(r == 2 + 300);

    r = packf(buf, sizeof(buf), "-[300c]", bytes);
    assert(r == PACKF_BE_CUT_OFF);
    r = packf(buf, sizeof(buf), "=[300c]", bytes);
    assert(r == 2 + 300);

    printf("lv length prefix overflow: ok\n");
}

/* An empty LV struct array whose element contains S must still compute the
 * element size, and skip the right number of local bytes. */
static void test_empty_struct_array_with_S(void)
{
    uint8_t buf[64];

# pragma pack(1)
    struct elem
    {
        uint32_t    a;
        char        s[8];
    };
    struct msg
    {
        uint16_t    n;
        struct elem e[2];
        uint8_t     tail;
    } in, out;
# pragma pack()

    memset(&in, 0, sizeof(in));
    in.n    = 0;
    in.tail = 0x5A;

    int len = packf(buf, sizeof(buf), "[=2[d 8S] c]", &in);
    assert(len == 2 + 1);
    assert(buf[2] == 0x5A);

    memset(&out, 0, sizeof(out));
    int r = unpackf(buf, (size_t)len, "[=2[d 8S] c]", &out);
    assert(r == len);
    assert(out.n == 0);
    assert(out.tail == 0x5A);

    printf("empty struct array with S: ok\n");
}

/* An LV array without a max inside a struct is variable-length locally:
 * the local cursor must move forward past the values, not backwards. */
static void test_lv_no_max_in_struct(void)
{
    uint8_t local[1 + 2 * 2 + 1];
    uint8_t buf[16];
    uint16_t v[2] = {0x0102, 0x0304};
    int len;

    local[0] = 2;
    memcpy(local + 1, v, sizeof(v));
    local[5] = 0x5A;

    len = packf(buf, sizeof(buf), "[-w c]", local);
    assert(len == 1 + 4 + 1);
    assert(buf[0] == 2);
    assert(buf[1] == 0x01 && buf[2] == 0x02);
    assert(buf[3] == 0x03 && buf[4] == 0x04);
    assert(buf[5] == 0x5A);

    uint8_t back[sizeof(local)];
    memset(back, 0, sizeof(back));
    int r = unpackf(buf, (size_t)len, "[-w c]", back);
    assert(r == len);
    assert(memcmp(back, local, sizeof(local)) == 0);

    printf("lv without max in struct: ok\n");
}

/* A ']' with no matching '[' must be reported, not treated as a
 * successful zero-byte result that silently drops the rest of the format. */
static void test_stray_close_bracket(void)
{
    uint8_t buf[16];
    void *cur = buf;
    int left = (int)sizeof(buf);
    uint32_t d = 0;
    uint16_t w = 0;
    int r;

    r = packf(buf, sizeof(buf), "d]w", 1, 2);
    assert(r == PACKF_NOT_MATCH);
    r = packf(buf, sizeof(buf), "]");
    assert(r == PACKF_NOT_MATCH);

    r = vpackf(&cur, &left, "d]", 1);
    assert(r == PACKF_NOT_MATCH);
    assert(cur == (void *)buf && left == (int)sizeof(buf));

    memset(buf, 0, sizeof(buf));
    r = unpackf(buf, sizeof(buf), "d]w", &d, &w);
    assert(r == PACKF_NOT_MATCH);

    /* Balanced brackets, including nested ones, still work. */
    r = packf(buf, sizeof(buf), "[d[w]]", buf);
    assert(r == 6);

    printf("stray close bracket: ok\n");
}

/* On unpack, the LV count is stored with the width of the length prefix:
 * uint8_t for '-', uint16_t for '='. */
static void test_lv_count_width(void)
{
    uint8_t buf[32];
    int32_t arr[3] = {1, 2, 3}, out[4];
    uint8_t c8[2] = {0, 0xEE};
    uint16_t c16[2] = {0, 0xEEEE};
    int len, r;

    len = packf(buf, sizeof(buf), "-4d", 3, arr);
    assert(len == 1 + 12);
    r = unpackf(buf, (size_t)len, "-4d", &c8[0], out);
    assert(r == len && c8[0] == 3 && c8[1] == 0xEE);

    len = packf(buf, sizeof(buf), "=4d", 3, arr);
    assert(len == 2 + 12);
    r = unpackf(buf, (size_t)len, "=4d", &c16[0], out);
    assert(r == len && c16[0] == 3 && c16[1] == 0xEEEE);

    printf("lv count width: ok\n");
}

# pragma pack(1)
struct le_pair
{
    uint16_t a;
    uint16_t b;
};

struct le_lv
{
    uint16_t len;
    uint16_t v;
};
# pragma pack()

/* '<' switches the wire byte order to little endian, '>' back to big
 * endian. Wire bytes are asserted directly so the checks hold on any host. */
static void test_little_endian(void)
{
    uint8_t buf[64];
    int len, r;

    /* scalars */
    {
        static const uint8_t expect[] = {
            0x02, 0x01,
            0x06, 0x05, 0x04, 0x03,
            0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07,
        };
        uint16_t w = 0;
        uint32_t d = 0;
        uint64_t D = 0;

        len = packf(buf, sizeof(buf), "<wdD", 0x0102, 0x03040506,
                (int64_t)0x0708090A0B0C0D0ELL);
        assert(len == (int)sizeof(expect));
        assert(memcmp(buf, expect, sizeof(expect)) == 0);

        r = unpackf(buf, (size_t)len, "<wdD", &w, &d, &D);
        assert(r == len);
        assert(w == 0x0102 && d == 0x03040506);
        assert(D == 0x0708090A0B0C0D0EULL);
    }

    /* switching back and forth inside one format */
    {
        static const uint8_t expect[] = {
            0x01, 0x02, 0x02, 0x01, 0x01, 0x02,
        };
        uint16_t x = 0, y = 0, z = 0;

        len = packf(buf, sizeof(buf), "w <w >w", 0x0102, 0x0102, 0x0102);
        assert(len == (int)sizeof(expect));
        assert(memcmp(buf, expect, sizeof(expect)) == 0);

        r = unpackf(buf, (size_t)len, "w <w >w", &x, &y, &z);
        assert(r == len && x == 0x0102 && y == 0x0102 && z == 0x0102);
    }

    /* float / double */
    {
        static const uint8_t expect[] = {
            0x00, 0x00, 0x80, 0x3F,
            0x3F, 0x80, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,
        };
        float f1 = 0, f2 = 0;
        double d1 = 0;

        len = packf(buf, sizeof(buf), "<f >f <F", 1.0, 1.0, 1.0);
        assert(len == (int)sizeof(expect));
        assert(memcmp(buf, expect, sizeof(expect)) == 0);

        r = unpackf(buf, (size_t)len, "<f >f <F", &f1, &f2, &d1);
        assert(r == len && f1 == 1.0f && f2 == 1.0f && d1 == 1.0);
    }

    /* arrays, and the 2-byte LV length prefix follows the byte order */
    {
        static const uint8_t expect_le[] = {
            0x02, 0x00, 0x02, 0x01, 0x04, 0x03,
        };
        static const uint8_t expect_be[] = {
            0x00, 0x02, 0x01, 0x02, 0x03, 0x04,
        };
        uint16_t arr[2] = {0x0102, 0x0304}, out[4];
        uint16_t n = 0;

        len = packf(buf, sizeof(buf), "<=4w", 2, arr);
        assert(len == (int)sizeof(expect_le));
        assert(memcmp(buf, expect_le, sizeof(expect_le)) == 0);
        r = unpackf(buf, (size_t)len, "<=4w", &n, out);
        assert(r == len && n == 2);
        assert(out[0] == 0x0102 && out[1] == 0x0304);

        len = packf(buf, sizeof(buf), "=4w", 2, arr);
        assert(len == (int)sizeof(expect_be));
        assert(memcmp(buf, expect_be, sizeof(expect_be)) == 0);

        /* 1-byte prefix and strings are not affected */
        len = packf(buf, sizeof(buf), "<-8s", "ab");
        assert(len == 3);
        assert(buf[0] == 2 && buf[1] == 'a' && buf[2] == 'b');
    }

    /* a struct inherits the outer byte order; a switch inside [] does not
     * leak out, and every array element starts from the same order */
    {
        static const uint8_t expect[] = {
            0x01, 0x02, 0x04, 0x03,     /* element 0: a BE, b LE */
            0x05, 0x06, 0x08, 0x07,     /* element 1: a BE again, b LE */
            0x09, 0x0A,                 /* after ]: BE again */
            0x0C, 0x0B,                 /* <[w]: inherited LE */
        };
        struct le_pair in[2] = {{0x0102, 0x0304}, {0x0506, 0x0708}};
        struct le_pair out[2];
        uint16_t one = 0x0B0C, tail = 0, one_out = 0;

        len = packf(buf, sizeof(buf), "2[w <w] w <[w]", in, 0x090A, &one);
        assert(len == (int)sizeof(expect));
        assert(memcmp(buf, expect, sizeof(expect)) == 0);

        memset(out, 0, sizeof(out));
        r = unpackf(buf, (size_t)len, "2[w <w] w <[w]", out, &tail, &one_out);
        assert(r == len);
        assert(memcmp(out, in, sizeof(in)) == 0);
        assert(tail == 0x090A && one_out == 0x0B0C);
    }

    /* LV struct: 2-byte length prefix in little endian */
    {
        static const uint8_t expect[] = {0x02, 0x00, 0x02, 0x01};
        uint16_t in = 0x0102, out = 0;
        struct le_lv s_in = {0, 0x0102}, s_out = {0, 0};

        len = packf(buf, sizeof(buf), "<=[w]", &in);
        assert(len == (int)sizeof(expect));
        assert(memcmp(buf, expect, sizeof(expect)) == 0);

        r = unpackf(buf, (size_t)len, "<=[w]", &out);
        assert(r == len && out == 0x0102);

        /* inside a struct the local length field precedes the body */
        len = packf(buf, sizeof(buf), "<[=[w]]", &s_in);
        assert(len == (int)sizeof(expect));
        assert(memcmp(buf, expect, sizeof(expect)) == 0);

        r = unpackf(buf, (size_t)len, "<[=[w]]", &s_out);
        assert(r == len && s_out.len == 2 && s_out.v == 0x0102);
    }

    /* the switch is a token of its own, not part of a field */
    {
        uint32_t d = 0;
        uint8_t n = 0;

        assert(packf(buf, sizeof(buf), "-<d", 1, &d) == PACKF_NOT_FORMAT);
        assert(packf(buf, sizeof(buf), "2<d", &d) == PACKF_NOT_FORMAT);
        memset(buf, 0, sizeof(buf));
        assert(unpackf(buf, sizeof(buf), "-<d", &n, &d) == PACKF_NOT_FORMAT);
        /* a trailing switch is harmless */
        assert(packf(buf, sizeof(buf), "d<", 1) == 4);
    }

    printf("little endian: ok\n");
}

/* '#' is an LV field with a 4-byte length prefix: it carries what does
 * not fit in the 2 bytes of '=', follows the byte order switches, and its
 * count / length member is a uint32_t. */
static void test_lv_4byte_prefix(void)
{
    enum { BIG = 70000 };
    static uint8_t buf[BIG + 64];
    static uint8_t bytes[BIG], bytes_out[BIG];
    static char str[BIG + 1], str_out[BIG + 1];
    int32_t arr[3] = {1, 2, 3}, out[4];
    uint32_t c32[2] = {0, 0xEEEEEEEE};
    int len, r, i;

    for (i = 0; i < BIG; ++i)
        bytes[i] = (uint8_t)(i * 7);
    memset(str, 'x', BIG);
    str[BIG] = '\0';

    /* count array: wire layout and count width */
    len = packf(buf, sizeof(buf), "#4d", 3, arr);
    assert(len == 4 + 12);
    assert(buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 3);
    assert(buf[4] == 0 && buf[7] == 1);
    r = unpackf(buf, (size_t)len, "#4d", &c32[0], out);
    assert(r == len && c32[0] == 3 && c32[1] == 0xEEEEEEEE);
    assert(out[0] == 1 && out[1] == 2 && out[2] == 3);

    /* the prefix follows the byte order switch */
    len = packf(buf, sizeof(buf), "<#4d", 3, arr);
    assert(len == 4 + 12);
    assert(buf[0] == 3 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
    assert(buf[4] == 1 && buf[7] == 0);
    r = unpackf(buf, (size_t)len, "<#4d", &c32[0], out);
    assert(r == len && c32[0] == 3 && out[2] == 3);

    /* over the max is still an error */
    assert(packf(buf, sizeof(buf), "#2d", 3, arr) == PACKF_BE_CUT_OFF);

    /* more than '=' can carry: bytes, string, struct body */
    assert(packf(buf, sizeof(buf), "=c", BIG, bytes) == PACKF_BE_CUT_OFF);
    len = packf(buf, sizeof(buf), "#c", BIG, bytes);
    assert(len == 4 + BIG);
    assert(buf[0] == 0x00 && buf[1] == 0x01 && buf[2] == 0x11 && buf[3] == 0x70);
    r = unpackf(buf, (size_t)len, "#c", &c32[0], bytes_out);
    assert(r == len && c32[0] == BIG);
    assert(memcmp(bytes, bytes_out, BIG) == 0);

    assert(packf(buf, sizeof(buf), "=s", str) == PACKF_BE_CUT_OFF);
    len = packf(buf, sizeof(buf), "#s", str);
    assert(len == 4 + BIG);
    r = unpackf(buf, (size_t)len, "#70001s", str_out);
    assert(r == len && strcmp(str, str_out) == 0);
    assert(unpackf(buf, (size_t)len, "#70000s", str_out) == PACKF_BE_CUT_OFF);

    assert(packf(buf, sizeof(buf), "=[70000c]", bytes) == PACKF_BE_CUT_OFF);
    len = packf(buf, sizeof(buf), "#[70000c] w", bytes, 0xABCD);
    assert(len == 4 + BIG + 2);
    assert(buf[0] == 0x00 && buf[1] == 0x01 && buf[2] == 0x11 && buf[3] == 0x70);
    {
        /* an old reader that knows a shorter body skips the rest */
        uint8_t head[4];
        uint16_t next = 0;

        r = unpackf(buf, (size_t)len, "#[4c] w", head, &next);
        assert(r == len && memcmp(head, bytes, 4) == 0 && next == 0xABCD);
    }

    /* inside a struct the length members are uint32_t, in host order */
    {
# pragma pack(1)
        struct msg
        {
            uint32_t    n;
            uint16_t    v[4];
            uint32_t    name_len;
            char        name[8];
            uint32_t    body_len;
            struct { uint16_t x; } body;
            uint8_t     tail;
        } in, back;
# pragma pack()
        char const *fmt = "[#4w #8s #[w] c]";

        memset(&in, 0, sizeof(in));
        in.n = 2;
        in.v[0] = 0x0102;
        in.v[1] = 0x0304;
        strcpy(in.name, "abc");
        in.body.x = 0x0506;
        in.tail = 0x5A;

        len = packf(buf, sizeof(buf), fmt, &in);
        assert(len == (4 + 4) + (4 + 3) + (4 + 2) + 1);
        assert(buf[3] == 2 && buf[4] == 0x01 && buf[7] == 0x04);
        assert(buf[11] == 3 && buf[12] == 'a');
        assert(buf[18] == 2 && buf[19] == 0x05 && buf[20] == 0x06);
        assert(buf[21] == 0x5A);

        memset(&back, 0xFF, sizeof(back));
        r = unpackf(buf, (size_t)len, fmt, &back);
        assert(r == len);
        assert(back.n == 2 && back.v[0] == 0x0102 && back.v[1] == 0x0304);
        assert(back.name_len == 3 && strcmp(back.name, "abc") == 0);
        assert(back.body_len == 2 && back.body.x == 0x0506);
        assert(back.tail == 0x5A);

        /* a length member that does not fit an int is rejected */
        in.n = 0x80000000u;
        assert(packf(buf, sizeof(buf), fmt, &in) == PACKF_INVALID_ARG);
    }

    /* an empty struct array still steps over the 4-byte length member */
    {
# pragma pack(1)
        struct msg
        {
            uint32_t    n;
            struct { uint32_t a; char s[8]; } e[2];
            uint8_t     tail;
        } in, back;
# pragma pack()

        memset(&in, 0, sizeof(in));
        in.tail = 0x5A;
        len = packf(buf, sizeof(buf), "[#2[d 8S] c]", &in);
        assert(len == 4 + 1 && buf[4] == 0x5A);
        memset(&back, 0xFF, sizeof(back));
        r = unpackf(buf, (size_t)len, "[#2[d 8S] c]", &back);
        assert(r == len && back.n == 0 && back.tail == 0x5A);
    }

    /* untrusted input: a declared length above INT_MAX, or just larger
     * than the input, must fail cleanly */
    {
        uint8_t evil[8] = {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4};
        uint8_t dst[8];

        assert(unpackf(evil, sizeof(evil), "#c", &c32[0], dst) == PACKF_OUT_OF_BUF);
        assert(unpackf(evil, sizeof(evil), "#s", dst) == PACKF_OUT_OF_BUF);
        assert(unpackf(evil, sizeof(evil), "#[4c]", dst) == PACKF_OUT_OF_BUF);
        assert(unpackf(evil, sizeof(evil), "#a", &c32[0]) == PACKF_OUT_OF_BUF);
        evil[0] = 0x7F;
        assert(unpackf(evil, sizeof(evil), "#c", &c32[0], dst) == PACKF_OUT_OF_BUF);
        assert(unpackf(evil, sizeof(evil), "#D", &c32[0], dst) == PACKF_INVALID_ARG);
        assert(unpackf(evil, sizeof(evil), "#[4c]", dst) == PACKF_OUT_OF_BUF);
        /* a truncated prefix */
        assert(unpackf(evil, 3, "#c", &c32[0], dst) == PACKF_OUT_OF_BUF);
    }

    printf("lv 4-byte prefix: ok\n");
}

/* ']' takes no LV prefix or count: it must be rejected, not silently
 * treated as a plain ']'. */
static void test_prefix_before_close_bracket(void)
{
    uint8_t local[64], buf[64];

    memset(local, 0, sizeof(local));

    assert(packf(buf, sizeof(buf), "[3a #]", local) == PACKF_NOT_FORMAT);
    assert(packf(buf, sizeof(buf), "[c 5]", local) == PACKF_NOT_FORMAT);
    assert(packf(buf, sizeof(buf), "[c -]", local) == PACKF_NOT_FORMAT);
    assert(packf(buf, sizeof(buf), "[c =2]", local) == PACKF_NOT_FORMAT);
    assert(unpackf(buf, sizeof(buf), "[3a #]", local) == PACKF_NOT_FORMAT);
    assert(unpackf(buf, sizeof(buf), "[c 5]", local) == PACKF_NOT_FORMAT);

    /* at the top level too */
    assert(packf(buf, sizeof(buf), "c 5]", 1) == PACKF_NOT_FORMAT);

    /* and in the body of an empty struct array, which is only measured */
    assert(packf(buf, sizeof(buf), "[-1[c 2]]", local) == PACKF_NOT_FORMAT);

    /* the plain forms still work */
    assert(packf(buf, sizeof(buf), "[3a]", local) == 3);
    assert(packf(buf, sizeof(buf), "[-1[c]]", local) == 1);

    printf("prefix before ']': ok\n");
}

/* Measuring an empty struct array must not overflow an int when the format
 * declares huge counts (UBSan would flag a signed overflow). */
static void test_local_struct_len_overflow(void)
{
    uint8_t local[64], buf[64];

    memset(local, 0, sizeof(local));   /* LV count 0 */

    assert(packf(buf, sizeof(buf), "[-1[2147483647d]]", local)
            == PACKF_INVALID_ARG);
    assert(packf(buf, sizeof(buf), "[-1[1073741824c 1073741824c]]", local)
            == PACKF_INVALID_ARG);
    buf[0] = 0;                        /* wire count 0 */
    assert(unpackf(buf, 1, "[-1[2147483647d]]", local) == PACKF_INVALID_ARG);

    /* a large but representable size is fine */
    assert(packf(buf, sizeof(buf), "[-1[1000c]]", local) == 1);

    printf("local struct length overflow: ok\n");
}

int main(void)
{
    test_roundtrip();
    test_unterminated_s();
    test_unaligned_fields();
    test_size_too_large();
    test_negative_lv();
    test_format_num_overflow();
    test_vpack_cursor_on_failure();
    test_lv_struct_unpack_bounds();
    test_lv_struct_pack_bounds();
    test_lv_len_prefix_overflow();
    test_empty_struct_array_with_S();
    test_lv_no_max_in_struct();
    test_stray_close_bracket();
    test_lv_count_width();
    test_little_endian();
    test_lv_4byte_prefix();
    test_prefix_before_close_bracket();
    test_local_struct_len_overflow();

    printf("all tests passed\n");
    return 0;
}
