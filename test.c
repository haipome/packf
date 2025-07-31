# include <stdio.h>
# include <stdint.h>
# include <string.h>
# include <assert.h>

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


int main()
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
    users.user[0].name_len = strlen(users.user[0].name);
    strcpy(users.user[0].passwd, "bazinga");
    users.n             = 10;

    packf_print_error = 1;

    int len = packf(buf, sizeof(buf), "cwdDfF[d =10[d -100s D 30S] w]16a", \
         0xa, 1, 2, 237417076350464llu, 3.4, 5.6, &users);
    assert(len == 81);

    bin_dump(buf, len);

    struct users ue;
    memset(&ue, 0, sizeof(ue));
    int r = unpackf(buf, len, "27a[d =10[d -100s D 30S] w]16a", &ue);
    assert(r == len && r > 0);
    assert(ue.type == users.type);
    assert(ue.user_num == users.user_num);
    assert(ue.user[0].uid == users.user[0].uid);
    assert(ue.user[0].name_len == users.user[0].name_len);
    assert(memcmp(ue.user[0].name, users.user[0].name, ue.user[0].name_len) == 0);
    assert(ue.user[0].key == users.user[0].key);
    assert(strcmp(ue.user[0].passwd, users.user[0].passwd) == 0);
    assert(ue.n == users.n);

    printf("%u\n", ue.n);

    return 0;
}

