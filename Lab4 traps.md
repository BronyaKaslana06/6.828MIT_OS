# Lab4 traps

[TOC]



## 1. RISC-V assembly ([easy](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	xv6仓库中有一个文件`user/call.c`。执行`make fs.img`编译它，可在`user/call.asm`中生成可读的汇编版本。阅读`call.asm`中函数`g`、`f`和`main`的代码。RISC-V的使用手册在[参考页](https://pdos.csail.mit.edu/6.828/2020/reference.html)上。回答下面的问题：

### 实验步骤

1. **哪些寄存器保存函数的参数？例如，在`main`对`printf`的调用中，哪个寄存器保存13？**

​	观察 `call.asm `及 `calling convention`，RISC-V 使用 a0-a7 存放函数的传入参数，13 保存在 `a2` 寄存器中。

2. **`main`的汇编代码中对函数`f`的调用在哪里？对`g`的调用在哪里(提示：编译器可能会将函数内）**

​	编译器优化后；无函数调用。由 assembly code 可以看到编译器直接将 `f(8)+1=12` 计算完成并放入` a1`。

3. **`printf`函数位于哪个地址？**

​	0x630

​	aupic 将 uimm20 的数字作为高 20 bit，低 12 bit 填 0，并与 pc 的值相加存入 rd 中，因此 printf 的地址 = 0x600(1536) + pc (0x30) = 0x630

4. **在`main`中`printf`的`jalr`之后的寄存器`ra`中有什么值？**

​	0x38

​	`jalr rd, offset(rs1)` 会将`pc+4 `的数值存入 `rd `中，如果 `rd` 是 `ra` 的话代表这个指令相当于函数调用，否则就只是单纯的跳转，支持的范围为以` rs1 `为基础的 ±2KB (-2048 ~ 2047)。 auipc 的高20位搭配 jalr 的低12位，就可以调用 pc在32位范围内的所有函数。 所以 ra 此时的值为 0x38

5. **运行以下代码。**

```c
unsigned int i = 0x00646c72;
printf("H%x Wo%s", 57616, &i);
```

​	**程序的输出是什么？**

​	**输出取决于RISC-V小端存储的事实。如果RISC-V是大端存储，为了得到相同的输出，你会把`i`设置成什么？是否需要将`57616`更改为其他值？**

​	输出是`He110 World`；i设置为`0x726c6400`；57616 不需要修改。

​	57616 的16进制表示就是 e110，与大小端序无关；i被当成字符串输出，当i是小端序表示的时候，内存中的数据是：`72 6c 64 00`，若改为大端序，则需要将i反转一下，故变为`72 6c 64 00`。

6. **在下面的代码中，“`y=`”之后将打印什么(注：答案不是一个特定的值）？为什么会发生这种情况？**

```c
printf("x=%d y=%d", 3);
```

​	将打印 `x=3 y=`。这是因为 `printf` 函数中的格式字符串 `"x=%d y=%d"` 包含了两个 `%d` 格式说明符，但只提供了一个整数参数 `3`。

## 2. Backtrace ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	回溯(Backtrace)通常对于调试很有用：它是一个存放于栈上用于指示错误发生位置的函数调用列表。在`kernel/printf.c`中实现名为`backtrace()`的函数。在`sys_sleep`中插入一个对此函数的调用，然后运行`bttest`，它将会调用`sys_sleep`。`backtrace`函数应当使用这些帧指针来遍历栈，并在每个栈帧中打印保存的返回地址。

### 实验步骤

1. 根据提示，在`kernel/riscv.h`中添加内联函数`r_fp()`读取栈帧值，函数代码如下：

```c
//lab 4-2: read frame
static inline uint64 r_fp() {
    uint64 x;
    asm volatile("mv %0, s0" : "=r" (x) );
    return x;
}
```

2. 在`kernel/printf.c`中实现`backtrace()`函数，并将该函数的原型声明添加到`defs.h`中。该函数首先获取当前函数的栈帧指针，`top`和`bottom`存储了用户栈的最高地址和最低地址，以避免遍历到超出用户栈。然后使用循环遍历合理范围内的每个栈帧并打印当前栈帧的返回地址，`fp - 8` 是因为在典型的函数调用中，返回地址位于当前栈帧(`fp`) 位置向下偏移 8 字节的位置。该函数代码如下：

```c
//lab 4-2 backtrace
void backtrace() {
    uint64 fp = r_fp();    // get current frame
    uint64 top = PGROUNDUP(fp);    // get the highest address of user stack
    uint64 bottom = PGROUNDDOWN(fp);    // get the lowest address of user stack
    for (; fp >= bottom && fp < top;fp = *((uint64 *) (fp - 16))) {
        printf("%p\n", *((uint64 *) (fp - 8)));    // output return address
    }
}
```

3. 在`kernel/sysproc.c`的`sys_sleep()`函数和在`kernel/printf.c`的`panic()`函数中分别调用`backtrace()`。

```c
//sysproc.c
uint64
sys_sleep(void){
    //...
    release(&tickslock);
    backtrace();	//lab 4-2 add
    return 0;
}

//printf.c
void
panic(char *s)
{
  //...
  backtrace();    //lab 4-2
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}
```

4. 测试：个人测试结果如下，通过：

```shell
init: starting sh
$ bttest
0x0000000080002d4c
0x0000000080002bae
0x0000000080002898
```

单元测试结果如下，通过：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-4/xv6-labs-2020$ ./grade-lab-traps backtrace
make: 'kernel/kernel' is up to date.
== Test backtrace test == backtrace test: OK (0.8s)
```

### 实验中遇到的问题和解决办法

​	本实验的问题主要在于如何遍历访问栈帧。想要访问栈帧首先需要限定用户栈的最高地址和最低地址，防止访问越界。想要访问到下一个栈帧，则需要从当前栈帧的位置`fp`减去16（即指向上一个栈帧的位置）以获取下一个栈帧的FP。这是因为在xv6操作系统中，每个栈帧的偏移量通常是固定为16。在打印栈帧时，要注意在xv6的函数调用中，返回地址位于当前栈帧`fp`位置向下偏移 8 字节的位置。

### 实验心得

​	通过本次实验，我对“栈帧”有了更深刻的了解。

​	栈帧（Stack Frame）是在函数调用过程中在栈上分配的一块内存区域，用于存储函数调用相关的数据和临时变量。每当函数被调用时，都会在栈上创建一个新的栈帧，用于存储该函数的局部变量、函数参数、返回地址和其他与函数执行相关的信息。

​	“栈帧”这一词使我联想到在上一次实验中的“页”这一概念。但实际上，两者明显不同。栈帧是函数调用过程中用于存储函数相关数据的一块内存区域，而页是操作系统对内存的管理单位。

## 3. Alarm(hard)

### 实验目的

​	在这个练习中你将向XV6添加一个特性，在进程使用CPU的时间内，XV6定期向进程发出警报。这对于那些希望限制CPU时间消耗的受计算限制的进程，或者对于那些计算的同时执行某些周期性操作的进程可能很有用。更普遍的来说，你将实现用户级中断/故障处理程序的一种初级形式。例如，你可以在应用程序中使用类似的一些东西处理页面故障。

​	应当添加一个新的`sigalarm(interval, handler)`系统调用，如果一个程序调用了`sigalarm(n, fn)`，那么每当程序消耗了CPU时间达到n个“滴答”，内核应当使应用程序函数`fn`被调用。当`fn`返回时，应用应当在它离开的地方恢复执行。在XV6中，一个滴答是一段相当任意的时间单元，取决于硬件计时器生成中断的频率。如果一个程序调用了`sigalarm(0, 0)`，系统应当停止生成周期性的报警调用。

​	`alarmtest`在`test0`中调用了`sigalarm(2, periodic)`来要求内核每隔两个滴答强制调用`periodic()`，然后旋转一段时间。当`alarmtest`产生如下输出并且`usertests`也能正常运行时，你的方案就是正确的：

```Shell
$ alarmtest
test0 start
........alarm!
test0 passed
test1 start
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
test1 passed
test2 start
................alarm!
test2 passed
$ usertests
...
ALL TESTS PASSED
$
```

### 实验步骤

#### test0: invoke handler

1. 修改`Makefile`文件，添加`alarmtest.c`

```makefile
	$U/_alarmtest\
```

2. 首先在`user/user.h`头文件中添加需要用到的两个系统调用的函数原型

```c
int sigalarm(int ticks,void (*handler)());
int sigreturn(void);
```

3. 在`user/usys.pl`，`kernel/syscall.h`和`kernel/syscall.c`中添加相应的声明和入口，使`alarmtest`可以调用`signalarm`和`sigreturn`这两个系统调用。代码如下：

```perl
# user/usys.pl
entry("sigalarm");
entry("sigreturn");
```

```c
//kernel/syscall.h
#define SYS_sigalarm  22
#define SYS_sigreturn  23

//kernel/syscall.c
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
//...
static uint64 (*syscalls[])(void) = {
//...
[SYS_sigalarm]  sys_sigalarm,
[SYS_sigreturn] sys_sigreturn,
};
```

4. 在`kernel/sysproc.c`中编写`sys_sigreturn()`函数，当前，在`test0`只需要返回0。

```c
//kernel/sysproc.c
uint64 
sys_sigreturn(void) {
    return 0;
}
```

5. 在`kernel/proc.h` 的结构体`proc`中，添加存储时间间隔，调用函数地址和经过的时钟数的成员变量。

```c
// kernel/proc.h
struct proc {
  //...
  int interval;                // alarm interval
  uint64 handler;	             // pointer to the handler function
  int passedticks;	           // how many ticks have passed since the last call to a process's alarm handler
};
```

6. 在`kernel/sysproc.c`中实现`sys_sigalarm()`函数，这个函数需要先保证时间间隔`interval`非负，然后将 `interval` 和 `handler` 的值存到当前进程`myproc()`的相应字段中。函数代码如下：

```c
//sys_sigalarm -- lab 4-3
uint64
sys_sigalarm(void) {
    int interval;
    uint64 handler;
    struct proc *p;
    if (argint(0, &interval) < 0 || argaddr(1, &handler) < 0 || interval < 0) 	  {
        return -1;
    }
    p = myproc();
    p->interval = interval;
    p->handler = handler;
    p->passedticks = 0;   

    return 0;
}
```

7. 在`kernel/proc.c`的 `allocproc()` 函数的最后完成对进程`proc`三个新字段的初始化赋值

```c
static struct proc*
allocproc(void)
{
//...  
//lab 4-3 add
  p->interval=0;
  p->handler=0;
  p->passedticks=0;

  return p;
}
```

8. 在`kernel/trap.c`的`usertrap()`函数中，为时钟中断添加相应的处理代码。这段代码的作用是在发生时钟中断时，检查当前进程是否设置了时间间隔和信号处理器。如果设置了，则根据时间间隔控制时钟中断的触发和信号处理的执行：如果时间间隔不为0且已经过去的时钟数等于时间间隔，即达到了指定的时间间隔，则将陷阱帧的 EPC 字段设置为信号处理器函数的地址，实现信号的触发和处理；如果时间间隔不为0但已经过去的时钟数还未达到时间间隔，则继续累加已经过去的时钟数。

```c
void
usertrap(void)
{
    //...
       // new code lab 4-3
  if(which_dev == 2){   // alarm interrupt
    if(p->interval != 0 && ++p->passedticks == p->interval){
      p->passedticks = 0;
      p->trapframe->epc = p->handler;
    }
  }

  // give up the CPU if this is a timer interrupt.
  //...
}
```

9. `test0`测试：先启动`make qemu`，再输入`alarm test`，输出如下，说明`test0`通过。

```shell
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ alarmtest
test0 start
.............................................alarm!
test0 passed
test1 start
.....alarm!
....alarm!
.....alarm!
....alarm!
.....alarm!
....alarm!
....alarm!
.....alarm!
....alarm!
.....alarm!

test1 failed: foo() executed fewer times than it was called
test2 start
.................................................alarm!
alarm!
test2 failed: alarm handler called more than once
```

#### test1/test2(): resume interrupted code

1. 在`kernel/proc.h`的`proc`结构体中新增 `trapframecopy` 字段。`trapframecopy` 字段的作用是用于保存陷阱帧的副本。

```c
//kernel/proc.h
struct proc {
  //...
  struct trapframe* trapframecopy;  // lab 4-3-2 add 
};
```

2. 在 `kernel/trap.c` 文件的 `usertrap()` 函数中，在覆盖 `p->trapframe->epc` 前，先对陷阱帧进行副本操作：
   - 当时钟中断发生时，如果满足条件，会进行陷阱帧的复制。
   - 首先，将当前陷阱帧的内容复制到进程的 `trapframecopy` 字段指定的位置，以保存原始陷阱帧的副本。
   - 接下来，修改原始陷阱帧的 EPC 字段为信号处理器函数的地址。

```c
  if(p->killed)
    exit(-1);

   // new code lab 4-3
  if(which_dev == 2){   // clock interrupt
    // clock passticks add 1
    if(p->interval != 0 && ++p->passedticks == p->interval){
      //p->passedticks = 0;   //lab 4-3-1 add
      //lab 4-3-2 add
      p->trapframecopy = p->trapframe + 512;  
      memmove(p->trapframecopy,p->trapframe,sizeof(struct trapframe));    		// 复制 trapframe
      p->trapframe->epc = p->handler;
    }
  }
//...
```

3. 在 `kernel/sysproc.c` 文件中实现 `sys_sigreturn()` 函数，用于恢复陷阱帧的副本到原始陷阱帧。也就是当调用 `sys_sigreturn()` 函数时，会将存储在 `trapframecopy` 字段的陷阱帧副本恢复到原始陷阱帧。
   - 首先，检查陷阱帧副本的位置是否正确。
   - 然后，使用 `memmove()` 函数将陷阱帧副本的内容复制回原始陷阱帧。
   - 最后，将过去的时钟数 `passedticks` 重置为 0，并将 `trapframecopy` 字段置为 0。

```c
//add sys_sigreturn: lab-4-3-2
uint64
sys_sigreturn(void) {
    struct proc* p = myproc();

    if(p->trapframecopy != p->trapframe + 512) {
        return -1;
    }
    memmove(p->trapframe, p->trapframecopy, sizeof(struct trapframe));
    p->passedticks = 0;     
    p->trapframecopy = 0;
    return 0;
}
```

4. 在初始进程 `kernel/proc.c` 的 `allocproc()` 中, 初始化 `p->trapframecopy` 为 0, 表明初始时无陷阱帧副本

```c
p->trapframecopy=0;
```

5. 测试：个人测试：`make qemu`启动`qemu`后，运行`alarmtest`，输出如下：

```shell
init: starting sh
$ alarmtest
test0 start
..................................................alarm!
test0 passed
test1 start
.....alarm!
....alarm!
.....alarm!
....alarm!
....alarm!
.....alarm!
.....alarm!
.....alarm!
.....alarm!
.....alarm!
test1 passed
test2 start
...................................................alarm!
test2 passed
```

说明`Lab3`正确。接下来需要运行`usertest`以测试我没有破坏内核的其他部分，输出如下：

```shell
$ usertests
usertests starting
test execout: OK
test copyin: OK
test copyout: OK
test copyinstr1: OK
test copyinstr2: OK
test copyinstr3: OK
test truncate1: OK
test truncate2: OK
test truncate3: OK
test reparent2: OK
test pgbug: OK
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3236
            sepc=0x0000000000005406 stval=0x0000000000005406
usertrap(): unexpected scause 0x000000000000000c pid=3237
            sepc=0x0000000000005406 stval=0x0000000000005406
OK
test badarg: OK
test reparent: OK
test twochildren: OK
test forkfork: OK
test forkforkfork: 0x0000000080002d96
0x0000000080002bf8
0x00000000800028a8
0x0000000080002d96
0x0000000080002bf8
0x00000000800028a8
OK
test argptest: OK
test createdelete: OK
test linkunlink: OK
test linktest: OK
test unlinkread: OK
test concreate: OK
test subdir: OK
test fourfiles: OK
test sharedfd: OK
test exectest: OK
test bigargtest: OK
test bigwrite: OK
test bsstest: OK
test sbrkbasic: OK
test sbrkmuch: OK
test kernmem: usertrap(): unexpected scause 0x000000000000000d pid=6216
            sepc=0x000000000000201a stval=0x0000000080000000
usertrap(): unexpected scause 0x000000000000000d pid=6217
            sepc=0x000000000000201a stval=0x000000008000c350
usertrap(): unexpected scause 0x000000000000000d pid=6218
            sepc=0x000000000000201a stval=0x00000000800186a0
usertrap(): unexpected scause 0x000000000000000d pid=6219
            sepc=0x000000000000201a stval=0x00000000800249f0
usertrap(): unexpected scause 0x000000000000000d pid=6220
            sepc=0x000000000000201a stval=0x0000000080030d40
usertrap(): unexpected scause 0x000000000000000d pid=6221
            sepc=0x000000000000201a stval=0x000000008003d090
usertrap(): unexpected scause 0x000000000000000d pid=6222
            sepc=0x000000000000201a stval=0x00000000800493e0
usertrap(): unexpected scause 0x000000000000000d pid=6223
            sepc=0x000000000000201a stval=0x0000000080055730
usertrap(): unexpected scause 0x000000000000000d pid=6224
            sepc=0x000000000000201a stval=0x0000000080061a80
usertrap(): unexpected scause 0x000000000000000d pid=6225
            sepc=0x000000000000201a stval=0x000000008006ddd0
usertrap(): unexpected scause 0x000000000000000d pid=6226
            sepc=0x000000000000201a stval=0x000000008007a120
usertrap(): unexpected scause 0x000000000000000d pid=6227
            sepc=0x000000000000201a stval=0x0000000080086470
usertrap(): unexpected scause 0x000000000000000d pid=6228
            sepc=0x000000000000201a stval=0x00000000800927c0
usertrap(): unexpected scause 0x000000000000000d pid=6229
            sepc=0x000000000000201a stval=0x000000008009eb10
usertrap(): unexpected scause 0x000000000000000d pid=6230
            sepc=0x000000000000201a stval=0x00000000800aae60
usertrap(): unexpected scause 0x000000000000000d pid=6231
            sepc=0x000000000000201a stval=0x00000000800b71b0
usertrap(): unexpected scause 0x000000000000000d pid=6232
            sepc=0x000000000000201a stval=0x00000000800c3500
usertrap(): unexpected scause 0x000000000000000d pid=6233
            sepc=0x000000000000201a stval=0x00000000800cf850
usertrap(): unexpected scause 0x000000000000000d pid=6234
            sepc=0x000000000000201a stval=0x00000000800dbba0
usertrap(): unexpected scause 0x000000000000000d pid=6235
            sepc=0x000000000000201a stval=0x00000000800e7ef0
usertrap(): unexpected scause 0x000000000000000d pid=6236
            sepc=0x000000000000201a stval=0x00000000800f4240
usertrap(): unexpected scause 0x000000000000000d pid=6237
            sepc=0x000000000000201a stval=0x0000000080100590
usertrap(): unexpected scause 0x000000000000000d pid=6238
            sepc=0x000000000000201a stval=0x000000008010c8e0
usertrap(): unexpected scause 0x000000000000000d pid=6239
            sepc=0x000000000000201a stval=0x0000000080118c30
usertrap(): unexpected scause 0x000000000000000d pid=6240
            sepc=0x000000000000201a stval=0x0000000080124f80
usertrap(): unexpected scause 0x000000000000000d pid=6241
            sepc=0x000000000000201a stval=0x00000000801312d0
usertrap(): unexpected scause 0x000000000000000d pid=6242
            sepc=0x000000000000201a stval=0x000000008013d620
usertrap(): unexpected scause 0x000000000000000d pid=6243
            sepc=0x000000000000201a stval=0x0000000080149970
usertrap(): unexpected scause 0x000000000000000d pid=6244
            sepc=0x000000000000201a stval=0x0000000080155cc0
usertrap(): unexpected scause 0x000000000000000d pid=6245
            sepc=0x000000000000201a stval=0x0000000080162010
usertrap(): unexpected scause 0x000000000000000d pid=6246
            sepc=0x000000000000201a stval=0x000000008016e360
usertrap(): unexpected scause 0x000000000000000d pid=6247
            sepc=0x000000000000201a stval=0x000000008017a6b0
usertrap(): unexpected scause 0x000000000000000d pid=6248
            sepc=0x000000000000201a stval=0x0000000080186a00
usertrap(): unexpected scause 0x000000000000000d pid=6249
            sepc=0x000000000000201a stval=0x0000000080192d50
usertrap(): unexpected scause 0x000000000000000d pid=6250
            sepc=0x000000000000201a stval=0x000000008019f0a0
usertrap(): unexpected scause 0x000000000000000d pid=6251
            sepc=0x000000000000201a stval=0x00000000801ab3f0
usertrap(): unexpected scause 0x000000000000000d pid=6252
            sepc=0x000000000000201a stval=0x00000000801b7740
usertrap(): unexpected scause 0x000000000000000d pid=6253
            sepc=0x000000000000201a stval=0x00000000801c3a90
usertrap(): unexpected scause 0x000000000000000d pid=6254
            sepc=0x000000000000201a stval=0x00000000801cfde0
usertrap(): unexpected scause 0x000000000000000d pid=6255
            sepc=0x000000000000201a stval=0x00000000801dc130
OK
test sbrkfail: usertrap(): unexpected scause 0x000000000000000d pid=6267
            sepc=0x0000000000003e7a stval=0x0000000000012000
OK
test sbrkarg: OK
test validatetest: OK
test stacktest: usertrap(): unexpected scause 0x000000000000000d pid=6271
            sepc=0x0000000000002188 stval=0x000000000000fbc0
OK
test opentest: OK
test writetest: OK
test writebig: OK
test createtest: OK
test openiput: 0x0000000080002d96
0x0000000080002bf8
0x00000000800028a8
OK
test exitiput: OK
test iput: OK
test mem: OK
test pipe1: OK
test preempt: kill... wait... OK
test exitwait: OK
test rmdot: OK
test fourteen: OK
test bigfile: OK
test dirfile: OK
test iref: OK
test forktest: OK
test bigdir: OK
ALL TESTS PASSED
```

`usertests`测试通过，下面进行单元测试：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-4/xv6-labs-2020$ ./grade-lab-traps alarmtest
make: 'kernel/kernel' is up to date.
== Test running alarmtest == (4.1s)
== Test   alarmtest: test0 ==
  alarmtest: test0: OK
== Test   alarmtest: test1 ==
  alarmtest: test1: OK
== Test   alarmtest: test2 ==
  alarmtest: test2: OK
```

单元测试通过。

### 实验中遇到的问题和解决办法

​	实验中，主要需要解决的是“为什么”的问题。`test0`和`test1/test2`有什么不同，提示为什么要求我们这样做？这和MIT课上教授的知识应该是息息相关的。

​	test0的目标是修改内核以跳转到用户空间的信号处理程序，从而使test0打印出"alarm!"。目前不必关心"alarm!"输出后发生的情况；如果程序在打印"alarm!"后崩溃，这是可以接受的。

​	test1/test2：恢复被中断的代码。在test0或test1之后，alarmtest会在打印"alarm!"后崩溃，或者打印出"test1 failed"。为了解决这个问题，必须确保当中断处理程序完成后，控制权返回到被定时器中断中断的用户程序的指令处。还必须确保寄存器的内容被恢复到中断时的值，以便中断处理程序完成后用户程序可以继续执行。最后，应在每次闹钟响起后“重新启动”时钟计数器，以便定时调用处理程序。

### 实验心得

**陷阱帧**：陷阱帧（Trapframe）是在操作系统中用于保存陷阱处理过程中的寄存器和状态信息的数据结构。EPC 字段是陷阱帧中的一个特定字段，用于存储陷阱返回地址（Exception Program Counter，EPC）。在实验中，`trapframecopy` 字段的引入是为了在发生时钟中断时，能够保存原始的陷阱帧，并在需要时恢复到原始的陷阱帧。通常情况下，`trapframecopy` 字段的值为 0，表示没有陷阱帧的副本。当发生特定的事件（如时钟中断）时，会对陷阱帧进行复制，并将 `trapframecopy` 字段指向存储副本的位置。在需要恢复到原始陷阱帧时，会将副本内容复制回原始陷阱帧，并将 `trapframecopy` 字段重置为 0。



**Trap机制**：Trap机制是操作系统中处理异常和中断的一种机制。当应用程序运行时，可能会遇到需要操作系统介入的情况，如系统调用、异常或设备中断。这时，需要将控制权从用户空间切换到内核空间，并执行相应的内核代码来处理这些情况。Trap机制实现了这种用户态到内核态的切换。

​	在 Trap 机制中，关键的指令是 `ecall`，它触发了从用户态到内核态的切换。当应用程序执行系统调用时，会使用 `ecall` 指令，将 CPU 的状态从用户态切换为内核态。此时，会进入内核空间并执行相应的中断处理程序。

下面以在 Shell 中调用 `write` 系统调用为例，解释了 Trap 机制的过程：

1. 在 Shell 中调用 `write` 系统调用，实际上是在进行 C 函数的调用。系统调用最终通过 `ecall` 指令来执行。`ecall` 指令将 CPU 的状态切换为具有 Supervisor Mode 的内核空间。
2. 在内核空间中，执行了一个汇编函数 `uservec`，该函数位于 `trap.c` 文件中。
3. `uservec` 函数跳转到了一个 C 语言函数 `usertrap` 函数中。
4. 在 `usertrap` 函数中，执行了 `syscall` 函数。根据传入的系统调用号，`syscall` 函数在系统调用表中查找对应的函数，并在内核空间执行相应的系统调用功能。
5. 对于 `write` 系统调用，执行了 `sys_write` 函数，将要显示的数据输出到控制台上。
6. 当系统调用完成后，通过一些 C 语言或汇编语言函数的操作，将控制权切换回用户空间，并恢复 `ecall` 指令之后用户程序的执行。

简单来说，Trap 机制的关键步骤是：

1. 使用 `ecall` 指令将 CPU 的状态从用户态切换为内核态，保存用户栈的寄存器信息。
2. 将内核代码地址写入 PC 寄存器，设置堆栈指针寄存器的内容为内核栈的地址，并切换页表到内核页表，开始执行内核代码。
3. 内核执行完毕后，将 CPU 的状态从内核态切换回用户态，并根据之前保存的用户栈信息恢复用户栈的执行状态。

通过 Trap 机制，操作系统能够在需要时切换到内核空间处理异常和中断情况，保证系统的稳定性和安全性。

## 实验评分与提交

在`time.txt`中填入自己实验的用时并将Lab1的答案保存在`answers-traps.txt`中后，运行`make grade`，输出如下：

```shell
== Test answers-traps.txt == answers-traps.txt: OK
== Test backtrace test ==
$ make qemu-gdb
backtrace test: OK (4.7s)
== Test running alarmtest ==
$ make qemu-gdb
(3.3s)
== Test   alarmtest: test0 ==
  alarmtest: test0: OK
== Test   alarmtest: test1 ==
  alarmtest: test1: OK
== Test   alarmtest: test2 ==
  alarmtest: test2: OK
== Test usertests ==
$ make qemu-gdb
usertests: OK (58.4s)
== Test time ==
time: OK
Score: 85/85
```

实验通过！

