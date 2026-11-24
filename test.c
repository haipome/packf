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

int main(void)
{
    test_roundtrip();
    test_unterminated_s();
    test_unaligned_fields();
    test_size_too_large();
    test_negative_lv();
    test_format_num_overflow();
    test_vpack_cursor_on_failure();

    printf("all tests passed\n");
    return 0;
}
