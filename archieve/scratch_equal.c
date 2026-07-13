#include <stdio.h>

int main()
{
    int x = 0x1;
    int y = 0x1;
    int z = x ^ y;
    printf("%#x\n", !z);
    return 0;
}