# Lab 2: System calls

[TOC]

​	在上一个实验中，我使用系统调用编写了一些实用程序。在本实验中，我将向xv6添加一些新的系统调用，这将帮助您了解它们是如何工作的，并使我了解xv6内核的一些内部结构。我将在以后的实验室中添加更多系统调用。

## System call tracing ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	在本作业中，需要添加一个系统调用跟踪功能，该功能可能会在以后调试实验时对您有所帮助。需要创建一个新的`trace`系统调用来控制跟踪。它应该有一个参数，这个参数是一个整数“掩码”（mask），它的比特位指定要跟踪的系统调用。例如，要跟踪`fork`系统调用，程序调用`trace(1 << SYS_fork)`，其中`SYS_fork`是`kernel/syscall.h`中的系统调用编号。如果在掩码中设置了系统调用的编号，则必须修改xv6内核，以便在每个系统调用即将返回时打印出一行。该行应该包含进程id、系统调用的名称和返回值；您不需要打印系统调用参数。`trace`系统调用应启用对调用它的进程及其随后派生的任何子进程的跟踪，但不应影响其他进程。

### 实验步骤

**修改代码，添加trace系统调用**

1. 根据提示，在`user/user.h`中添加`trace`系统调用的原型定义，并在`user/usys.pl`中添加系统调用`trace`的入口，然后再`kernel/syscall.h`中添加系统调用编号。

   ```c
   //user/user.h
   //...
   int uptime(void);
   int trace(int);
   
   //user/usys.pl
   //...
   entry("uptime");
   entry("trace");
   
   //kernel/syscall.h
   //...
   #define SYS_close  21
   #define SYS_trace  22	//新添加的trace系统调用编号
   ```

   ​	在最终编译的过程中，`Makefile`调用`perl`脚本`user/usys.pl`，它生成实际的系统调用存根`user/usys.S`，这个文件中的汇编代码使用RISC-V的`ecall`指令转换到内核。

2.  在`kernel/sysproc.c`中添加一个`sys_trace()`函数，这个函数通过将参数保存到`proc`结构体里的一个新变量中来实现新的系统调用。通过阅读`kernel/proc.h`中的原码推测`proc`结构体是一个类似`PCB`的结构，记录着某个进程存在的基本信息，根据提示，我们需要在`proc`结构体中添加一个新变量`trace_mask`记录该进程系统调用的一些信息（跟踪掩码），代码如下：

   ```c
   struct proc {
   //...
     char name[16];               // Process name (debugging)
     int trace_mask;              // trace system call argument
   }
   ```

   接下来实现`sys_trace`函数。这个函数从`a0`寄存器中获取到系统调用参数，然后将获得到的系统调用参数赋值给进程的`trace_mask`变量。函数如下：

   ```c
   //sysproc.c
   uint64
   sys_trace(void)
   {
     //get argument of system call from register a0
     int maskValue;
     if (argint(0, &maskValue) < 0) {
       return -1;
     }
     myproc()->trace_mask = maskValue;
     return 0;
   }
   ```

3. 修改`fork()`函数将跟踪掩码从父进程复制到子进程，即把父进程的`trace_mask`赋值给子进程的`trace_mask`，代码如下：

   ```c
   //kernel/proc.c
   int fork(void) {
   //...
   	np->cwd = idup(p->cwd);
   	safestrcpy(np->name, p->name, sizeof(p->name));
   //copy trace_mask to children process
   	np->trace_mask = p->trace_mask;
   //...
   }
   ```

4. 修改`syscall.c`中的`syscall()`函数以实现打印跟踪输出。由于实验要求在每个系统调用即将返回时打印出一行，该行应该包含进程id、系统调用的名称和返回值，所以我们应该修改`syscall()`函数以实现这一点。修改后的`syscall()`函数的代码如下：

```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    //new code
    if((1 << num) & p->trace_mask)
      printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num], p->trapframe->a0);
  } 
  else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

掩码`trace_mask `是按位判断应该跟踪哪一个进程的，所以使用按位运算，进程序号可以直接通过 `p->pid` 得到，`num` 是直接从寄存器 `a7 `中读取的系统调用号，系统调用的返回值就是寄存器 `a0` 的值了，通过` p->trapframe->a0` 语句获取。

由于需要输出函数名称，所以需要新定义一个数组记录下各个系统调用编号对应的系统调用名，定义如下：

```c
static char *syscalls_name[] = {
[SYS_fork]    "fork",
[SYS_exit]    "exit",
[SYS_wait]    "wait",
[SYS_pipe]    "pipe",
[SYS_read]    "read",
[SYS_kill]    "kill",
[SYS_exec]    "exec",
[SYS_fstat]   "fstat",
[SYS_chdir]   "chdir",
[SYS_dup]     "dup",
[SYS_getpid]  "getpid",
[SYS_sbrk]    "sbrk",
[SYS_sleep]   "sleep",
[SYS_uptime]  "uptime",
[SYS_open]    "open",
[SYS_write]   "write",
[SYS_mknod]   "mknod",
[SYS_unlink]  "unlink",
[SYS_link]    "link",
[SYS_mkdir]   "mkdir",
[SYS_close]   "close",
[SYS_trace]   "trace",
};
```

为了使`syscall.c`更完整，我们还需要在`syscalls[]`中 添加声明和`extern`声明：

```c
//...
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void);

static uint64 (*syscalls[])(void) = {
//...
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
}
```

5. 修改Makefile文件，在 `UPROGS` 项中最后一行添加 `$U/_trace\`。

**结果验证**：

输出符合预期

```shell
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
$
$ trace 2147483647 grep hello README
5: syscall trace -> 0
5: syscall exec -> 3
5: syscall open -> 3
5: syscall read -> 1023
5: syscall read -> 966
5: syscall read -> 70
5: syscall read -> 0
5: syscall close -> 0
$
$ grep hello README
$
$ trace 2 usertests forkforkfork
usertests starting
9: syscall fork -> 10
test forkforkfork: 9: syscall fork -> 11
11: syscall fork -> 12
12: syscall fork -> 13
13: syscall fork -> 14
12: syscall fork -> 15
13: syscall fork -> 16
12: syscall fork -> 17
13: syscall fork -> 18
14: syscall fork -> 19
12: syscall fork -> 20
13: syscall fork -> 21
14: syscall fork -> 22
12: syscall fork -> 23
13: syscall fork -> 24
14: syscall fork -> 25
12: syscall fork -> 26
13: syscall fork -> 27
12: syscall fork -> 28
14: syscall fork -> 29
12: syscall fork -> 30
13: syscall fork -> 31
12: syscall fork -> 32
14: syscall fork -> 33
12: syscall fork -> 34
13: syscall fork -> 35
12: syscall fork -> 36
14: syscall fork -> 37
12: syscall fork -> 38
13: syscall fork -> 39
12: syscall fork -> 40
13: syscall fork -> 41
12: syscall fork -> 42
14: syscall fork -> 43
12: syscall fork -> 44
13: syscall fork -> 45
14: syscall fork -> 46
12: syscall fork -> 47
13: syscall fork -> 48
14: syscall fork -> 49
12: syscall fork -> 50
13: syscall fork -> 51
14: syscall fork -> 52
12: syscall fork -> 53
13: syscall fork -> 54
12: syscall fork -> 55
13: syscall fork -> 56
12: syscall fork -> 57
13: syscall fork -> 58
14: syscall fork -> 59
12: syscall fork -> 60
13: syscall fork -> 61
12: syscall fork -> 62
13: syscall fork -> 63
12: syscall fork -> 64
13: syscall fork -> 65
14: syscall fork -> 66
12: syscall fork -> 67
13: syscall fork -> 68
12: syscall fork -> 69
13: syscall fork -> 70
12: syscall fork -> 71
14: syscall fork -> -1
13: syscall fork -> -1
12: syscall fork -> -1
OK
9: syscall fork -> 72
ALL TESTS PASSED
```

**结果测试**

已通过全部测试用例。

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-2020$ ./grade-lab-syscall trace
make: 'kernel/kernel' is up to date.
== Test trace 32 grep == trace 32 grep: OK (1.1s)
== Test trace all grep == trace all grep: OK (0.9s)
== Test trace nothing == trace nothing: OK (1.0s)
== Test trace children == trace children: OK (8.9s)
```

### 实验中遇到的问题和解决办法

​	开始实验时，我在纠结，既然让我添加一个系统调用`trace`，那么我是否还需要自己写一个`trace.c`文件，然后再添加一系列的声明呢？在仔细阅读题目要求后，我发现不是这样的。在`user`文件夹下，已经有了`trace.c`文件，我们需要修改xv6内核，以便在每个系统调用即将返回时打印出一行。原码中`trace.c`如下：

```c
int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  exec(nargv[0], nargv);
  exit(0);
}
```

​	那么，既需要添加系统调用，又需要改变内核，我应该从哪一步开始做呢？我最后的解决办法是严格按照实验提示走，一步一步来，最终成功解决问题。

### 实验心得

​	通过这次实验，我对操作系统的系统调用在底层是如何实现的有了更深层次的理解。系统调用就是操作系统为用户程序提供的一系列API接口，来帮助用户程序完成一些操作。系统调用一方面保护了计算机，另一方面也帮助用户程序封装了底层功能的实现，系统调用与程序中的函数调用看起来是一样的，但区别是系统调用会实际运行到系统内核中，并执行内核中对于系统调用的实现。

​	在xv6系统中，以系统调用`trace()`为例，系统调用的执行过程如下：

1.在用户程序中调用`trace()`系统调用；

2.在头文件`user/user.h`中找到其函数原型；

3.`usys.pl`脚本运行，生成`usys.S`，其中包含了系统调用的函数实现；

4.`usys.S`设置a7寄存器为系统调用号，利用`ecall`指令进行系统调用；

5.调用`syscall()`函数，从`syscalls`表中通过索引查找到`sys_trace()`函数并执行；

## Sysinfo ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

添加一个系统调用`sysinfo`，它收集有关正在运行的系统的信息。系统调用采用一个参数：一个指向`struct sysinfo`的指针（参见`kernel/sysinfo.h`）。内核应该填写这个结构的字段：`freemem`字段应该设置为空闲内存的字节数，`nproc`字段应该设置为`state`字段不为`UNUSED`的进程数。

### 实验步骤

1. 根据提示，上一个实验类似，在`user/user.h`中添加`sysinfo()`系统调用声明和`struct sysinfo`结构体声明。

```c
//struct结构体声明
struct stat;
struct rtcdate;
struct sysinfo;

//系统调用声明
//...
int trace(int);
int sysinfo(struct sysinfo *);
```

2. 在`user/usys.pl`中添加`sysinfo`系统调用的入口；在`kernel/syscall.h`中添加系统调用号的宏定义。

```c
//user/usys.pl
entry("sysinfo");

//kernel/syscall.h
#define SYS_sysinfo 23
```

3. 在内核中声明系统调用：先在`kernel/syscall.c`中，新增`sys_sysinfo`函数原型和定义，由于在上一个实验中新增了`syscalls_name`数组以记录各个系统调用的名字，所以需要在`syscalls_name`数组中新增"sysinfo"这个名字。

```c
//...
extern uint64 sys_trace(void);
extern uint64 sys_sysinfo(void);

static uint64 (*syscalls[])(void) = {
//...
[SYS_trace]   sys_trace,
[SYS_sysinfo]   sys_sysinfo,
};

static char *syscalls_name[] = {
//...
[SYS_trace]   "trace",
[SYS_sysinfo] "sysinfo",
};
//...
```

4. 编写函数收集可用内存量

​	题目中要求`sysinfo`的`freemem`字段应该设置为空闲内存的字节数，故我们需要编写一个函数以获得系统中的可用内存量。

​	推测空闲空间管理本身应该是内核所做的工作，于是在`kernel`文件夹中寻找，发现`kernel/kalloc.c`文件中有进行和空闲空间管理相关的操作。相关代码如下：

```c
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
```

​	在上述代码中，`run`是一个链表节点，表示一块空闲的内存空间，包含一个指向下一个节点的指针`next`；`kmem`是用于管理空闲空间的链表，`lock`是用于保护对空闲空间链表并发访问的一个自旋锁，通过分析`kalloc()`函数知，`freelist`永远指向最后一个可用页（或理解为第一个），相当于空闲空间链表头结点，如果需要获取空闲页面数量，只需遍历`freelist`链表即可。这就是`free_mem`函数的思路，在`kernel/kalloc.c`中新增函数`free_mem`，函数返回当前空闲的内存空间大小，该函数实现如下：

```c
uint64
free_mem(void)
{
  struct run *p;
  // num: free page number
  uint64 num = 0;
  // spinlock
  acquire(&kmem.lock);
  p = kmem.freelist;  //work pointer
  // travel node list
  while (p)
  {
    num++;
    p = p->next;
  }
  // spinlock
  release(&kmem.lock);
  // memory size = num*PGSIZE
  return num * PGSIZE;
}
```

完成函数后在`kernel/def.h`中声明这个函数。

```c
// kalloc.c
...
uint64          free_mem(void);
```

5. 编写获取当前进程数的函数

​	根据提示，直接浏览`kernel/proc.c`和相关的头文件`kernel/proc.h`。在`proc.h`文件中，有通过枚举定义继承状态，并且该进程状态在`proc`结构体中表明。代码段如下：

```c
enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
 //...
}
```

​	所以，想要获取当前正在运行的所有进程的数目，只需要遍历所有进程的进程状态`state`，统计所有状态不为`UNUSED`的进程数目即可，在`kernel/proc.c`中编写函数`proc_number`来实现这个功能，代码如下：

```c
uint64
proc_number(void)
{
  struct proc *p;
  uint64 num = 0;
  // visit all process
  for (p = proc; p < &proc[NPROC]; p++)
  {
    // lock
    acquire(&p->lock);
    if (p->state != UNUSED)
    {
      num++;
    }
    // lock
    release(&p->lock);
  }
  return num;
}
```

完成函数后在`kernel/def.h`中声明这个函数。

```c
// proc.c
...
uint64          proc_nubmer(void);
```

6. 实现`sys_sysinfo`函数

根据实验提示，`sys_sysinfo`函数需要将一个`struct sysinfo`复制回用户空间，具体实现可以参考`sys_fstat()`和`filestat()`是如何使用`copyout()`的。`sys_sysinfo`函数实现如下，该函数需要添加在`kernel/sysproc.c`中。

```c
//...
#include "sysinfo.h"	//add

//...
uint64
sys_sysinfo(void)
{
  uint64 addr;
  struct sysinfo info;
  struct proc *p = myproc();
  
  if (argaddr(0, &addr) < 0)
	  return -1;
  info.freemem = free_mem();
  info.nproc = proc_number();
  //copy
  if (copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;
  
  return 0;
}
```

7. 在`user`目录下新增一个`sysinfo.c`的用户程序，代码如下:

```c
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    if (argc != 1)
    {
        fprintf(2, "Usage: %s need not param\n", argv[0]);
        exit(1);
    }

    struct sysinfo info;
    sysinfo(&info);
    
    printf("free space: %d\nused process: %d\n", info.freemem, info.nproc);
    exit(0);
}
```

8. 在Makefile中添加`UPROGS`项并进行测试

```shell
hart 1 starting
hart 2 starting
init: starting sh
$ sysinfotest
sysinfotest: start
sysinfotest: OK
$ sysinfo
free space: 133386240
used process: 3
```

个人测试正常，下面进行单元测试：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-2020$ ./grade-lab-syscall sysinfo
make: 'kernel/kernel' is up to date.
== Test sysinfotest == sysinfotest: OK (2.1s)
```

单元测试通过，说明编写正确。

### 实验中遇到的问题和解决办法

​	在实验中需要认真阅读`hint`，`hint`中的提示往往是解题的关键。我在编写`proc_number`和`free_mem`函数的过程中，需要仔细阅读`hint`中提到的`kernel/proc.c`文件和`kernel/kalloc.c`文件才能找到代码编写的关键突破点。

​	在代码编写的过程中，我也第一次接触到了“自旋锁”`spinlock`。若在编写`proc_number`和`free_mem`函数时不注意自旋锁的存在，就会导致错误。在访问临界资源时，需要先`acquire`再`release`。

### 实验心得

​	**自旋锁**：在操作系统中，Spinlock（自旋锁）是一种用于实现对共享资源的互斥访问的同步机制。它是一种基本的锁机制，用于保护临界区代码，在多线程或多核处理器系统中防止并发访问和竞争条件。

Spinlock 的特点是在资源被占用时，等待线程或处理器会循环（自旋）检查锁的状态，而不是进入睡眠状态，从而避免了线程切换和上下文切换的开销。当锁变为可用时，等待线程会立即获得锁，并继续执行临界区代码。

Spinlock 的基本操作包括：

- `acquire`：尝试获取锁。如果锁已经被占用，则等待（自旋）直到锁可用。
- `release`：释放锁，使其变为可用状态。

Spinlock 的优点是操作简单且响应时间快，特别适合在临界区的保护时间非常短暂的情况下使用。它避免了线程上下文切换和睡眠唤醒的开销，适用于在高并发场景中提供较低的延迟和较高的吞吐量。

然而，Spinlock 也有一些局限性：

- 自旋等待期间，占用锁的线程会一直占用处理器时间，对于较长时间的自旋等待，可能会导致资源浪费和系统性能下降。
- 自旋等待期间，如果占用锁的线程长时间未释放锁，其他等待线程会一直自旋，可能导致线程饥饿或死锁。

因此，在选择使用 Spinlock 时，需要根据具体情况进行权衡。Spinlock 通常用于对临界区的保护时间非常短的情况，例如对共享变量的简单读写操作，或者在内核中的一些关键代码路径中。对于临界区的保护时间较长的情况，可以考虑使用其他同步机制，如互斥锁（Mutex）或信号量（Semaphore）。

​	**xv6系统如何添加系统调用**:

1.在`kernel/syscall.h`添加宏定义，定义这个系统调用的序号；

````C
#define SYS_XXX  xx
````

2.在`user/user.h`添加用户态下的`XXX`系统调用的声明，声明用户态可以调用系统调用`XXX`；

````C
int XXX(int); 
````

3.`user/usys.pl`添加系统调用系统调用XXX的入口；

````C
entry("XXX");
````

4.`kernel/syscall.c`的函数指针数组`*syscalls[]`添加这个新增的`XXX`系统调用；

```` C
static uint64 (*syscalls[])(void) = {
  ...
  [SYS_XXX]   sys_xxx,
};
````

5.`kernel/syscall.c`为内核态的系统调用添加声明；

````C
extern uint64 sys_xxx(void);
````

6.在`kernel`或`user`中添加或修改需要的函数，以完成该系统调用的功能；

7.在`Makefile`的`UPROGS`添加编译配置；

````makefile
UPROGS=\
  ...
  $U/_xxx\
````

## 实验提交

添加`time.txt`后运行`make grade`输出评分如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-2020'
== Test trace 32 grep ==
$ make qemu-gdb
trace 32 grep: OK (4.9s)
== Test trace all grep ==
$ make qemu-gdb
trace all grep: OK (0.6s)
== Test trace nothing ==
$ make qemu-gdb
trace nothing: OK (0.9s)
== Test trace children ==
$ make qemu-gdb
trace children: OK (9.6s)
== Test sysinfotest ==
$ make qemu-gdb
sysinfotest: OK (1.7s)
== Test time ==
time: OK
Score: 35/35
```

说明实验全部正确，将该分支上传到远程代码仓库即可。

