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

    printf("all tests passed\n");
    return 0;
}
