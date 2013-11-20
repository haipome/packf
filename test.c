# include <stdio.h>
# include <stdint.h>
# include <string.h>

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
    strcpy(users.user[0].passwd, "bazinga");
    users.n             = 10;

    packf_print_error = 1;

    int len = packf(buf, sizeof(buf), "cwdDfF[d =10[d -100s D 30S] w]16a", \
         0xa, 1, 2, 237417076350464llu, 3.4, 5.6, &users);

    bin_dump(buf, len);

    struct users ue;
    memset(&ue, 0, sizeof(ue));
    unpackf(buf, len, "27a[d =10[d -100s D 30S] w]16a", &ue);

    printf("%u\n", ue.n);

    return 0;
}

