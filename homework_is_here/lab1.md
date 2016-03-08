## 高级操作系统
### lab1：编程支持时钟，中断，输出

姓名：朱里<br/>
学号：2015210959<br/>
专业：计算机技术<br/>
导师：高性能计算所 舒继武 老师<br/>

###实验过程
1.先做个改造：<br/>
由于是第一次作业，我还没太搞明白是完全手写，还是在os0.c的基础上修改。<br/>
所以，我首先把模拟器的代码大致读了一遍，然后把反汇编器的代码扫了一遍，大致了解v9模拟器的用法。<br/>
在试了试xem的调试模式后，我发现debug界面下虽然可以打印出每条指令，但打出来的是二进制码，看不懂。<br/>
因为反汇编器能够翻译二进制码，所以我把dis.c里的一部分翻译代码拷了出来，经过适当修改嵌到em.c中，使得调试模式下能够以汇编指令的形式查看当前正在执行的指令。<br/>

2.了解语法规则:<br/>
因为c.c是一个简易的c编译器,所以我们能写什么程序,要取决于c.c的处理能力。<br/>
通过对c.c的大致阅读，我发现基本上标准c语言的语法都能支持，变量不能定义在函数中间。<br/>

3.了解库函数：<br/>
root/lib下有三个库：u.h，libc.h，libm.h。<br/>
其中u.h是模拟器中很对重要结构和字段的定义来源。<br/>
libc.h实现了输入输出、内存操作和一些系统调用。由于系统调用还没有实现，所以现在调用会导致程序崩溃。（比如printf还不能用）<br/>
libm.h直接包含了math.h，按下不表。<br/>

4.学习ucore的lab1：<br/>
其实不必学地太细，不过我花了好几天学习ucore内核的代码，加上doc里的文档。<br/>
虽然不学也能完成作业，但是读了ucore代码并做了lab1之后收获确实不小。<br/>

5.读懂os0.c的代码，修改并运行：<br/>
具体怎么改我就不细说了，大致的效果就是，差不多一秒输出一行字。同时会捕捉按键并输出对应的字符。<br/>

6.对模拟器的修改：<br/>
模拟器em.c是整个cpu的中心，所以在调试过程中经常会对em.c进行修改。<br/>
大多是printf之类的输出语句。<br/>

7.自己写个函数库<br/>
我在root/lib下写了个my.h，偶尔有什么实用的函数可以写进去，供以后调用。<br/>

8.写点脚本<br/>
为了提高编译、运行之类效率，我写了两个脚本comp.sh和allcomp.sh。<br/>
目的也就是每次编译和运行不用敲长命令。<br/>

###备注
说实话，我其实还是没搞太清楚，每次练习是应该修改os.c还是全部手写。<br/>
感觉os.c中的功能已经比较完备了。<br/>
不论哪种方式，把代码搞懂都是关键。<br/>

###最后附上代码：
```
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
```
