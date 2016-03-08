// os0.c -- simple timer isr test

#include <u.h>
#include <my.h>

int current;

// out(port, val)    { asm(LL,8); asm(LBL,16); asm(BOUT); }
ivec(void *isr) { asm(LL,8); asm(IVEC); }
stmr(int val) { asm(LL,8); asm(TIME); }
halt(val) { asm(LL,8); asm(HALT); }

alltraps()
{
    // This function captures all kinds of trapm, just like the trap_dispatch() in ucore.
    // I'm supposed to write a switch-case routine, 
    // but I haven't fully understood the stack structure.
    asm(PSHA);
    asm(PSHB);

    puts("a trap is captured.\n");
    printinteger(current);
    putchar('\n');
    current++;

    asm(POPB);
    asm(POPA);
    asm(RTI);
}

main()
{
    current = 0;

    stmr(2500000);
    ivec(alltraps);
    
    asm(STI);
    
    while (1) {
        // if (current & 1) out(1, '1'); else out(1, '0');
    }

    halt(0);
}
