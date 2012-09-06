# include <stdio.h>
# include <stdlib.h>
# include <stdint.h>
# include "pack.h"

# define BUF_PKG_LEN 8096

void bin_dump(char *pkg, int len)
{
    int i, j = 0;

    printf("len = %d\n", len);
    if (len > 0)
    {
        for (i = 0; i < len; ++i)
        {
            printf("%02X  ", (uint8_t)(pkg[i]));
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
    char pkg[BUF_PKG_LEN] = {0};
    int len;
    char *p = pkg;
    
# pragma pack(1)
    
    struct a{
        int aa;
    };
    struct b{
        struct a bb[2];
    };
    
# pragma pack(0)
    
    double F = 4.0;
    char a;
    short b;
    int c;
    float d;
    double e;
    char h[20];

    struct b ast, ast2;

    ast.bb[0].aa = 1;
    ast.bb[1].aa = 2;

    len = pack(pkg, BUF_PKG_LEN, "[2[d]]", &ast);

    bin_dump(pkg, len);
    if (len < 0)
    {
        exit(-1);
    }
    len = unpack(pkg, BUF_PKG_LEN, "[2[d]]", &ast2);

    bin_dump(&ast2, len);

    uint64_t ddword[2] = {0, 1};
    len = pack(pkg, 8096, "cwdfF2D10s16a", 'b', 2, 3, 4.0, -5.1, ddword, "hello pack!");
    bin_dump(pkg, len);

    char s[20];
    len = unpack(pkg, 8096, "cwdfF2D10s24a", &a, &b, &c, &d, &e,  ddword, s);
    printf("len = %d, %c %d %d %f %f\n", len, a, b, c, d, e);
    bin_dump(ddword, 16);
    puts(s);

    c = 7;
    len = pack(pkg, 8096, "[[[[[[[[[[d]]]]]]]]]]", &c);
    bin_dump(pkg, len);

    len = pack(pkg, 0, "[[[]]]");
    bin_dump(pkg, len);

    return 0;
}

