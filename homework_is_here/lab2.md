## 高级操作系统
### lab2：支持建立页表

姓名：朱里<br/>
学号：2015210959<br/>
专业：计算机技术<br/>
导师：高性能计算所 舒继武 老师<br/>

###实验过程
1.找线索：<br/>
本次实验要建立页表。所以我先把em.c中有“page”关键字的源码部分读了一遍，大概有setpage、rlook、wlook几个函数。<br/>
涉及到的中断有FMEM，涉及到指令有pdir、spag、lvad。<br/>
所以本次实验主要围绕这些概念展开。<br/>

2.学习ucore的lab2内容<br/>
我又花了两天看ucore的lab2的文档和源码。文档好歹看完了，把概念算是复习了一遍，代码实在是看不过来了。<br/>
姑且到这儿吧，日后有时间再看。<br/>

3.os2.c<br/>
我把os文件夹下的源码扫了一遍，发现os2.c的源码中包含了页表的相关操作。<br/>

4.理解代码<br/>
通过运行我发现这程序好像本身就完整了-_-||<br/>
于是现在的目标就成了理解os2的代码。<br/>
这其中主要需要理解的代码，有两部分。<br/>
一个是主函数main()，其中的流程比较清楚，对各种各样的中断都进行了测试，逐行理解即可。<br/>
另一个就是建立页表的部分setup_paging()。<br/>

5.页表有多大？<br/>
char pg_mem[6 * 4096]; // page dir + 4 entries + alignment<br/>
注意上面这句定义，按照每页4KB的话，一共定义了6页。<br/>
首先要理解为什么是6页。注释里写了，一个page dir，4个entry，对齐。<br/>
page dir：就是一级页表，即“页表的页表”。<br/>
entry：就是二级页表，指向物理页面。<br/>
对齐：内存地址要按照4KB对齐，所以难免要多分配一整页。这个是C语言常用技巧，就不多说了。<br/>
所以，一级页表有一页，二级页表有四页。总共能映射16M的内存地址空间。<br/>

6.setup_paging()<br/>
关于页表的建立，干脆我逐行讲解好了。<br/>
```
  // 一个循环变量
  int i;
  
  // address alignment, tricky to read
  // 内存地址按页对齐，位操作可读性较低，但效率很高。搞底层编程常用。
  pg_dir = (int *)((((int)&pg_mem) + 4095) & -4096);
  // 二级页表1的表头
  pg0 = pg_dir + 1024;
  // 二级页表1的表头
  pg1 = pg0 + 1024;
  // 二级页表1的表头
  pg2 = pg1 + 1024;
  // 二级页表1的表头
  pg3 = pg2 + 1024;
  
  // 一级页表只有4项，对应4个二级页表的表头地址。
  pg_dir[0] = (int)pg0 | PTE_P | PTE_W | PTE_U;  // identity map 16M
  pg_dir[1] = (int)pg1 | PTE_P | PTE_W | PTE_U;
  pg_dir[2] = (int)pg2 | PTE_P | PTE_W | PTE_U;
  pg_dir[3] = (int)pg3 | PTE_P | PTE_W | PTE_U;
  
  // 剩下的一级页表项全部置为0，表示“无”
  for (i=4;i<1024;i++) pg_dir[i] = 0;
  
  // 因为pg0～pg3的地址是连续的，所以这么干一口气把pg0到pg3的所有表项和连续的物理页做了个线性映射。
  // 注意，是线性映射。这么写就是一种演示，实际是不大可能的。
  for (i=0;i<4096;i++) pg0[i] = (i<<12) | PTE_P | PTE_W | PTE_U;  // trick to write all 4 contiguous pages
  
  // 将一级页表的表头位置设置为pg_dir。
  pdir(pg_dir);
  // 启动虚拟内存机制。
  spage(1);
```

7.小修改<br/>
在程序结尾，我又加了个无限循环，而且把trap处理的代码逻辑改了一下。让每次按键都能输出所按字符。并且大概每秒显示一个信息。<br/>

###最后附上代码：
```
// os2.c -- test various features

#include <u.h>

enum { // page table entry flags
  PTE_P   = 0x001,       // Present
  PTE_W   = 0x002,       // Writeable
  PTE_U   = 0x004,       // User
//PTE_PWT = 0x008,       // Write-Through
//PTE_PCD = 0x010,       // Cache-Disable
  PTE_A   = 0x020,       // Accessed
  PTE_D   = 0x040,       // Dirty
//PTE_PS  = 0x080,       // Page Size
//PTE_MBZ = 0x180,       // Bits must be zero
};

enum { // processor fault codes
  FMEM,   // bad physical address
  FTIMER, // timer interrupt
  FKEYBD, // keyboard interrupt
  FPRIV,  // privileged instruction
  FINST,  // illegal instruction
  FSYS,   // software trap
  FARITH, // arithmetic trap
  FIPAGE, // page fault on opcode fetch
  FWPAGE, // page fault on write
  FRPAGE, // page fault on read
  USER=16 // user mode exception 
};

// first-level and second-level page table
char pg_mem[6 * 4096]; // page dir + 4 entries + alignment

int *pg_dir, *pg0, *pg1, *pg2, *pg3;

int current;

// read a byte from input
int in(port)    { asm(LL,8); asm(BIN); }
// print a byte to output
out(port, val)  { asm(LL,8); asm(LBL,16); asm(BOUT); }
// set the interrupt vector address
ivec(void *isr) { asm(LL,8); asm(IVEC); }
// load the bad virtual address to a
lvadr()         { asm(LVAD); }
// set the timeout value
stmr(int val)   { asm(LL,8); asm(TIME); }
// set the page directory starting address by the value in a
pdir(value)     { asm(LL,8); asm(PDIR); }
// enable/disable virtual memory feature by the value in a
spage(value)    { asm(LL,8); asm(SPAG); }
// exit the program
halt(value)     { asm(LL,8); asm(HALT); }

void *memcpy() { asm(LL,8); asm(LBL, 16); asm(LCL,24); asm(MCPY); asm(LL,8); }
void *memset() { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MSET); asm(LL,8); }
void *memchr() { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MCHR); }

write(fd, char *p, n) { while (n--) out(fd, *p++); }

int strlen(char *s) { return memchr(s, 0, -1) - (void *)s; }

enum { BUFSIZ = 32 };
int vsprintf(char *s, char *f, va_list v)
{
  char *e = s, *p, c, fill, b[BUFSIZ];
  int i, left, fmax, fmin, sign;

  while (c = *f++) {
    if (c != '%') { *e++ = c; continue; }
    if (*f == '%') { *e++ = *f++; continue; }
    if (left = (*f == '-')) f++;
    fill = (*f == '0') ? *f++ : ' ';
    fmin = sign = 0; fmax = BUFSIZ;
    if (*f == '*') { fmin = va_arg(v,int); f++; } else while ('0' <= *f && *f <= '9') fmin = fmin * 10 + *f++ - '0';
    if (*f == '.') { if (*++f == '*') { fmax = va_arg(v,int); f++; } else for (fmax = 0; '0' <= *f && *f <= '9'; fmax = fmax * 10 + *f++ - '0'); }
    if (*f == 'l') f++;
    switch (c = *f++) {
    case 0: *e++ = '%'; *e = 0; return e - s;
    case 'c': fill = ' '; i = (*(p = b) = va_arg(v,int)) ? 1 : 0; break;
    case 's': fill = ' '; if (!(p = va_arg(v,char *))) p = "(null)"; if ((i = strlen(p)) > fmax) i = fmax; break;
    case 'u': i = va_arg(v,int); goto c1;
    case 'd': if ((i = va_arg(v,int)) < 0) { sign = 1; i = -i; } c1: p = b + BUFSIZ-1; do { *--p = ((uint)i % 10) + '0'; } while (i = (uint)i / 10); i = (b + BUFSIZ-1) - p; break;
    case 'o': i = va_arg(v,int); p = b + BUFSIZ-1; do { *--p = (i & 7) + '0'; } while (i = (uint)i >> 3); i = (b + BUFSIZ-1) - p; break;
    case 'p': fill = '0'; fmin = 8; c = 'x';
    case 'x': case 'X': c -= 33; i = va_arg(v,int); p = b + BUFSIZ-1; do { *--p = (i & 15) + ((i & 15) > 9 ? c : '0'); } while (i = (uint)i >> 4); i = (b + BUFSIZ-1) - p; break;
    default: *e++ = c; continue;
    }
    fmin -= i + sign;
    if (sign && fill == '0') *e++ = '-';
    if (!left && fmin > 0) { memset(e, fill, fmin); e += fmin; }
    if (sign && fill == ' ') *e++ = '-';
    memcpy(e, p, i); e += i;
    if (left && fmin > 0) { memset(e, fill, fmin); e += fmin; }
  }
  *e = 0;
  return e - s;
}

int printf(char *f) { static char buf[4096]; return write(1, buf, vsprintf(buf, f, &f)); } // XXX remove static from buf

// trap dispatcher, which deals with specific behaviors of each trap.
trap(int c, int b, int a, int fc, int pc)
{
  printf("TRAP: ");
  // fault code, or trap number, if you will
  switch (fc) {
  case FINST:  printf("FINST\n"); break; // normally this will be a fatal error.
  case FRPAGE: printf("FRPAGE [0x%08x]\n",lvadr()); break;
  case FWPAGE: printf("FWPAGE [0x%08x]\n",lvadr()); break;
  case FIPAGE: printf("FIPAGE [0x%08x]\n",lvadr()); break;
  case FSYS:   printf("FSYS\n"); break;
  case FARITH: printf("FARITH\n"); break;
  case FMEM:   printf("FMEM [0x%08x]\n",lvadr()); break;
  case FTIMER: printf("FTIMER: %d\n", current); ++current; /*stmr(0);*/ break;
  case FKEYBD: printf("FKEYBD [%c]\n", in(0)); break;
  default:     printf("other [%d]\n",fc); break;
  }
}

// trap parameters are set here.
alltraps()
{
  asm(PSHA);
  asm(PSHB);
  asm(PSHC);
  trap();
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

setup_paging()
{
  int i;
  
  // address alignment, tricky to read
  pg_dir = (int *)((((int)&pg_mem) + 4095) & -4096);
  pg0 = pg_dir + 1024;
  pg1 = pg0 + 1024;
  pg2 = pg1 + 1024;
  pg3 = pg2 + 1024;
  
  pg_dir[0] = (int)pg0 | PTE_P | PTE_W | PTE_U;  // identity map 16M
  pg_dir[1] = (int)pg1 | PTE_P | PTE_W | PTE_U;
  pg_dir[2] = (int)pg2 | PTE_P | PTE_W | PTE_U;
  pg_dir[3] = (int)pg3 | PTE_P | PTE_W | PTE_U;
  for (i=4;i<1024;i++) pg_dir[i] = 0;
  
  for (i=0;i<4096;i++) pg0[i] = (i<<12) | PTE_P | PTE_W | PTE_U;  // trick to write all 4 contiguous pages
  
  pdir(pg_dir);
  spage(1);
}

main()
{
  int t, d; 
  
  current = 0;
  ivec(alltraps);
  
  asm(STI);
  
  printf("test timer...");
  t = 0;
  stmr(10000);
  while (!current) t++;
  printf("(t=%d)...ok\n",t);
  
  printf("test invalid instruction...");
  //asm(_dd); // XXX find a better way
  asm(-1);
  printf("...ok\n");
  
  printf("test bad physical address...");
  t = *(int *)0x20000000;
  printf("...ok\n");

  printf("test divide by zero...");
  t = 10; d = 0; t /= d;
  printf("...ok\n");
  
  printf("test paging...");
  // reposition stack within first 16M
  asm(LI, 4*1024*1024); // a = 4M
  asm(SSP); // sp = a
  setup_paging();
  printf("identity map...ok\n");

  printf("test page fault read...");
  pg0[50] = 0;
  pdir(pg_dir);
  t = *(int *)(50<<12);
  printf("...ok\n");

  printf("test page fault write...");
  *(int *)(50<<12) = 5;
  printf("...ok\n");

  // add a timer
  stmr(2500000);

  while (1) {
  }

  halt(0);
}
```