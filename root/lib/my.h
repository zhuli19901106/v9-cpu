#ifndef _MY_H_
#define _MY_H_

void out(port, val)
{
    asm(LL,8);
    asm(LBL,16);
    asm(BOUT);
}

void putchar(char ch)
{
    out(1, ch);
}

void puts(char *s)
{
    int i = 0;
    while (s[i] != 0) {
        putchar(s[i++]);
    }
}

void printinteger(int n)
{
    // All variables must be defined up front, or there'll be error.
    int bs = 1000000000;
    if (n == 0) {
       putchar('0');
       return; 
    }

    while (n / bs == 0) {
        bs /= 10;
    }
    while (bs > 0) {
        putchar('0' + n / bs);
        n %= bs;
        bs /= 10;
    }
}

#endif
