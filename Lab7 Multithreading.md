# Lab7 Multithreading

[TOC]

​	本实验将使您熟悉多线程。您将在用户级线程包中实现线程之间的切换，使用多个线程来加速程序，并实现一个屏障。

## Uthread: switching between threads ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	在本练习中，您将为用户级线程系统设计上下文切换机制，然后实现它。为了让您开始，您的xv6有两个文件：`user/uthread.c`和`user/uthread_switch.S`，以及一个规则：运行在`Makefile`中以构建`uthread`程序。`uthread.c`包含大多数用户级线程包，以及三个简单测试线程的代码。线程包缺少一些用于创建线程和在线程之间切换的代码。

​	本次实验中需要提出一个创建线程和保存/恢复寄存器以在线程之间切换的计划，并实现该计划。

### 实验步骤

1. 为`thread`结构体添加上下文字段：

   参考`kernel/proc.h` 中定义的 `struct context` 结构体，在`user/uthread.c`定义了`ctx` 结构体，用于记录进程的上下文环境，结构体定义如下：

```c
//lab 7-1 add
struct ctx {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

接下来利用定义的`ctx`结构体，给`thread`结构体添加一个`context`数据成员用于记录某一线程的上下文:

```c
struct thread {
  char stack[STACK_SIZE]; /* the thread's stack */
  int state;              /* FREE, RUNNING, RUNNABLE */
  struct ctx context;     /* 线程的上下文context */
};
```

2. 修改 `thread_create()` 函数：

`thread_create`函数的作用是创建新线程。它在线程数组中找到一个空闲的线程结构，然后将其状态设置为 `RUNNABLE`。我们需要做的工作就是在这个函数的`// YOUR CODE HERE`处添加代码，在建立一个新线程时初始化上下文字段，代码如下：

``` c
void thread_create(void (*func)()) {
  //...
  // YOUR CODE HERE
  // lab 7-1 add
  t->context.ra = (uint64)func;
  t->context.sp = (uint64)t->stack + STACK_SIZE;
}
```

3. 修改 `thread_schedule()` 函数：

   `thread_schedule` 函数用于调度线程并进行线程切换。它通过遍历线程数组寻找下一个可运行的线程，并将其状态设置为 `RUNNING`。然后，如果下一个可运行的线程和当前运行的线程不同，就进行线程切换，线程的切换通过调用需要我们进一步实现的`thread_switch`函数完成。我们需要在代码的`// YOUR CODE HERE`处添加线程切换函数的调用，修改后的代码如下：

   ```c
       /* YOUR CODE HERE
        * Invoke thread_switch to switch from t to next_thread:
        * thread_switch(??, ??);
        */
       // lab 7-1 add
       thread_switch(&t->context, &current_thread->context);
   ```

​	注意要修改`thread_switch`函数的原型定义。

4. 实现 `thread_switch` 函数的汇编实现：

​	根据实验要求和提示，我们需要在`user/switch.S`中用汇编语言实现`thread_switch`函数。为实现该函数，我们可以参考`kernel/swtch.S`中的`swtch`函数。这个函数需要将保存在旧线程的寄存器值恢复到新线程的寄存器，并将控制权切换到新线程。具体实现如下：

```assembly
.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
	# lab 7-1 add
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)
	ret    /* return to ra */
```

下面是对代码的一些解释：

1. `sd ra, 0(a0)`: 将旧线程的 `ra` 寄存器的值存储到旧线程的栈中，偏移量为0。
2. `sd sp, 8(a0)`: 将旧线程的 `sp` 寄存器的值存储到旧线程的栈中，偏移量为8。这个操作将保存旧线程的栈指针，以便在恢复时知道旧线程的栈位置。
3. `sd s0, 16(a0)`: 将旧线程的 `s0` 寄存器的值存储到旧线程的栈中，偏移量为16。
4. ... : 依次将 `s1` 到 `s11` 寄存器的值存储到旧线程的栈中，每个保存的偏移量相应增加。
5. `ld ra, 0(a1)`: 将新线程的 `ra` 寄存器的值从新线程的栈中加载，偏移量为0。
6. `ld sp, 8(a1)`: 将新线程的 `sp` 寄存器的值从新线程的栈中加载，偏移量为8。这个操作将恢复新线程的栈指针，使其指向新线程的栈。
7. `ld s0, 16(a1)`: 将新线程的 `s0` 寄存器的值从新线程的栈中加载，偏移量为16。
8. ... : 依次将 `s1` 到 `s11` 寄存器的值从新线程的栈中加载，每个加载的偏移量相应增加。
9. `ret`: 返回到 `ra` 寄存器保存的地址，实现线程切换。在执行 `ret` 指令后，控制权将从当前线程切换到新线程。



5. 测试：启动qemu后，首先测试`uthread`指令输出是否正确：

```shell
init: starting sh
$ uthread
thread_a started
thread_b started
thread_c started
thread_c 0
thread_a 0
thread_b 0
thread_c 1
thread_a 1
thread_b 1
thread_c 2
thread_a 2
thread_b 2
thread_c 3
thread_a 3
thread_b 3
thread_c 4
thread_a 4
thread_b 4
thread_c 5
thread_a 5
thread_b 5
thread_c 6
thread_a 6
thread_b 6
thread_c 7
thread_a 7
thread_b 7
thread_c 8
thread_a 8
thread_b 8
thread_c 9
thread_a 9
thread_b 9
thread_c 10
thread_a 10
thread_b 10
thread_c 11
thread_a 11
thread_b 11
thread_c 12
thread_a 12
thread_b 12
thread_c 13
thread_a 13
thread_b 13
thread_c 14
thread_a 14
thread_b 14
thread_c 15
thread_a 15
thread_b 15
thread_c 16
thread_a 16
thread_b 16
thread_c 17
thread_a 17
thread_b 17
thread_c 18
thread_a 18
thread_b 18
thread_c 19
thread_a 19
thread_b 19
thread_c 20
thread_a 20
thread_b 20
thread_c 21
thread_a 21
thread_b 21
thread_c 22
thread_a 22
thread_b 22
thread_c 23
thread_a 23
thread_b 23
thread_c 24
thread_a 24
thread_b 24
thread_c 25
thread_a 25
thread_b 25
thread_c 26
thread_a 26
thread_b 26
thread_c 27
thread_a 27
thread_b 27
thread_c 28
thread_a 28
thread_b 28
thread_c 29
thread_a 29
thread_b 29
thread_c 30
thread_a 30
thread_b 30
thread_c 31
thread_a 31
thread_b 31
thread_c 32
thread_a 32
thread_b 32
thread_c 33
thread_a 33
thread_b 33
thread_c 34
thread_a 34
thread_b 34
thread_c 35
thread_a 35
thread_b 35
thread_c 36
thread_a 36
thread_b 36
thread_c 37
thread_a 37
thread_b 37
thread_c 38
thread_a 38
thread_b 38
thread_c 39
thread_a 39
thread_b 39
thread_c 40
thread_a 40
thread_b 40
thread_c 41
thread_a 41
thread_b 41
thread_c 42
thread_a 42
thread_b 42
thread_c 43
thread_a 43
thread_b 43
thread_c 44
thread_a 44
thread_b 44
thread_c 45
thread_a 45
thread_b 45
thread_c 46
thread_a 46
thread_b 46
thread_c 47
thread_a 47
thread_b 47
thread_c 48
thread_a 48
thread_b 48
thread_c 49
thread_a 49
thread_b 49
thread_c 50
thread_a 50
thread_b 50
thread_c 51
thread_a 51
thread_b 51
thread_c 52
thread_a 52
thread_b 52
thread_c 53
thread_a 53
thread_b 53
thread_c 54
thread_a 54
thread_b 54
thread_c 55
thread_a 55
thread_b 55
thread_c 56
thread_a 56
thread_b 56
thread_c 57
thread_a 57
thread_b 57
thread_c 58
thread_a 58
thread_b 58
thread_c 59
thread_a 59
thread_b 59
thread_c 60
thread_a 60
thread_b 60
thread_c 61
thread_a 61
thread_b 61
thread_c 62
thread_a 62
thread_b 62
thread_c 63
thread_a 63
thread_b 63
thread_c 64
thread_a 64
thread_b 64
thread_c 65
thread_a 65
thread_b 65
thread_c 66
thread_a 66
thread_b 66
thread_c 67
thread_a 67
thread_b 67
thread_c 68
thread_a 68
thread_b 68
thread_c 69
thread_a 69
thread_b 69
thread_c 70
thread_a 70
thread_b 70
thread_c 71
thread_a 71
thread_b 71
thread_c 72
thread_a 72
thread_b 72
thread_c 73
thread_a 73
thread_b 73
thread_c 74
thread_a 74
thread_b 74
thread_c 75
thread_a 75
thread_b 75
thread_c 76
thread_a 76
thread_b 76
thread_c 77
thread_a 77
thread_b 77
thread_c 78
thread_a 78
thread_b 78
thread_c 79
thread_a 79
thread_b 79
thread_c 80
thread_a 80
thread_b 80
thread_c 81
thread_a 81
thread_b 81
thread_c 82
thread_a 82
thread_b 82
thread_c 83
thread_a 83
thread_b 83
thread_c 84
thread_a 84
thread_b 84
thread_c 85
thread_a 85
thread_b 85
thread_c 86
thread_a 86
thread_b 86
thread_c 87
thread_a 87
thread_b 87
thread_c 88
thread_a 88
thread_b 88
thread_c 89
thread_a 89
thread_b 89
thread_c 90
thread_a 90
thread_b 90
thread_c 91
thread_a 91
thread_b 91
thread_c 92
thread_a 92
thread_b 92
thread_c 93
thread_a 93
thread_b 93
thread_c 94
thread_a 94
thread_b 94
thread_c 95
thread_a 95
thread_b 95
thread_c 96
thread_a 96
thread_b 96
thread_c 97
thread_a 97
thread_b 97
thread_c 98
thread_a 98
thread_b 98
thread_c 99
thread_a 99
thread_b 99
thread_c: exit after 100
thread_a: exit after 100
thread_b: exit after 100
thread_schedule: no runnable threads
```

输出正确，符合预期，退出qemu进行单元测试：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./grade-lab-thread uthread
make: 'kernel/kernel' is up to date.
== Test uthread == uthread: OK (1.2s)
```

测试通过！

### 实验中遇到的困难和解决方法

在启动`make qemu`编译后报错：

```shell
user/uthread.c: In function 'thread_schedule':
user/uthread.c:81:19: error: passing argument 1 of 'thread_switch' makes integer from pointer without a cast [-Werror=int-conversion]
   81 |     thread_switch(&t->context, &current_thread->context);
      |                   ^~~~~~~~~~~
      |                   |
      |                   struct ctx *
user/uthread.c:39:27: note: expected 'uint64' {aka 'long unsigned int'} but argument is of type 'struct ctx *'
   39 | extern void thread_switch(uint64, uint64);
      |                           ^~~~~~
```

​	这说明我没有修改`thread_switch`函数的原型，将`thread_switch`函数的原型修改为`extern void thread_switch(struct ctx *, struct ctx *)`后问题解决。

### 实验心得

​	通过本次实验，我学习了用户级线程系统设计上下文切换机制，并通过参考其他文件的方式实现了这个上下文切换机制。下面是我查询到的一些资料：

​	xv6中的上下文切换是由 `thread_switch` 函数实现的。`thread_switch` 函数保存了当前旧线程的寄存器状态，并从新线程的上下文中恢复寄存器状态，从而实现线程的切换。当调用 `thread_switch` 时，控制权从当前线程切换到新线程，新线程的代码会从 `ret` 指令处开始执行。

​	用户级线程的切换是由用户态代码触发的，而不是内核态。这意味着用户级线程可以通过特定的系统调用或库函数来实现线程的创建、销毁和切换，而不需要内核的直接支持。这种设计简化了线程的管理和调度，并使用户级线程的实现更加灵活和可扩展。

​	然而，需要注意的是，xv6 中的用户级线程不是真正的轻量级线程，因为它们不能在同一个进程内共享内核资源，而是通过进程切换来实现并发执行。这种设计在一定程度上限制了用户级线程的性能和灵活性，但也使得线程的管理和调度更加简单和可靠。

## 2. Using threads ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	在本作业中，您将探索使用哈希表的线程和锁的并行编程。您应该在具有多个内核的真实Linux或MacOS计算机（不是xv6，不是qemu）上执行此任务。最新的笔记本电脑都有多核处理器。

​	文件`notxv6/ph.c`包含一个简单的哈希表，如果单个线程使用，该哈希表是正确的，但是多个线程使用时，该哈希表是不正确的。在您的xv6主目录（可能是`~/xv6-labs-2020`）中，键入以下内容：

```bash
$ make ph
$ ./ph 1
```

请注意，要构建`ph`，`Makefile`使用操作系统的gcc，而不是6.S081的工具。`ph`的参数指定在哈希表上执行`put`和`get`操作的线程数。运行一段时间后，`ph 1`将产生与以下类似的输出：

```
100000 puts, 3.991 seconds, 25056 puts/second
0: 0 keys missing
100000 gets, 3.981 seconds, 25118 gets/second
```

您看到的数字可能与此示例输出的数字相差两倍或更多，这取决于您计算机的速度、是否有多个核心以及是否正在忙于做其他事情。

`ph`运行两个基准程序。首先，它通过调用`put()`将许多键添加到哈希表中，并以每秒为单位打印puts的接收速率。之后它使用`get()`从哈希表中获取键。它打印由于puts而应该在哈希表中但丢失的键的数量（在本例中为0），并以每秒为单位打印gets的接收数量。

通过给`ph`一个大于1的参数，可以告诉它同时从多个线程使用其哈希表。试试`ph 2`：

```bash
$ ./ph 2
100000 puts, 1.885 seconds, 53044 puts/second
1: 16579 keys missing
0: 16579 keys missing
200000 gets, 4.322 seconds, 46274 gets/second
```

​	这个`ph 2`输出的第一行表明，当两个线程同时向哈希表添加条目时，它们达到每秒53044次插入的总速率。这大约是运行`ph 1`的单线程速度的两倍。这是一个优秀的“并行加速”，大约达到了人们希望的2倍（即两倍数量的核心每单位时间产出两倍的工作）。

​	然而，声明`16579 keys missing`的两行表示散列表中本应存在的大量键不存在。也就是说，puts应该将这些键添加到哈希表中，但出现了一些问题。请看一下`notxv6/ph.c`，特别是`put()`和`insert()`。

​	为什么两个线程都丢失了键，而不是一个线程？确定可能导致键丢失的具有2个线程的事件序列。在`answers-thread.txt`中提交您的序列和简短解释。

[!TIP] 为了避免这种事件序列，请在`notxv6/ph.c`中的`put`和`get`中插入`lock`和`unlock`语句，以便在两个线程中丢失的键数始终为0。相关的pthread调用包括：

- `pthread_mutex_t lock; // declare a lock`
- `pthread_mutex_init(&lock, NULL); // initialize the lock`
- `pthread_mutex_lock(&lock); // acquire lock`
- `pthread_mutex_unlock(&lock); // release lock`

​	当`make grade`说您的代码通过`ph_safe`测试时，您就完成了，该测试需要两个线程的键缺失数为0。在此时，`ph_fast`测试失败是正常的。

​	不要忘记调用`pthread_mutex_init()`。首先用1个线程测试代码，然后用2个线程测试代码。您主要需要测试：程序运行是否正确呢（即，您是否消除了丢失的键？）？与单线程版本相比，双线程版本是否实现了并行加速（即单位时间内的工作量更多）？

​	在某些情况下，并发`put()`在哈希表中读取或写入的内存中没有重叠，因此不需要锁来相互保护。您能否更改`ph.c`以利用这种情况为某些`put()`获得并行加速？提示：每个散列桶加一个锁怎么样？

​	修改代码，使某些`put`操作在保持正确性的同时并行运行。当`make grade`说你的代码通过了`ph_safe`和`ph_fast`测试时，你就完成了。`ph_fast`测试要求两个线程每秒产生的`put`数至少是一个线程的1.25倍。

### 实验步骤

0. 有2个或多个线程时为什么造成数据丢失？

​	现在有两个线程T1和T2，他们都走到`put`函数，且假设两个线程中`key%NBUCKET`，即要插入同一个散列桶中。两个线程同时调用`insert(key, value, &table[i], table[i])`，insert是通过头插法实现的。如果首先调用`insert`的线程还执行结束时另一个线程就开始执行`insert`，那么前者的数据会被覆盖。

​	将以上内容写入`answers-thread.txt`中。

1. 分别测试在1个线程和2个线程的情况下是否消除了丢失的键：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ make ph
make: 'ph' is up to date.
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./ph 1
100000 puts, 5.395 seconds, 18537 puts/second
0: 0 keys missing
100000 gets, 5.280 seconds, 18939 gets/second
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./ph 2
100000 puts, 3.092 seconds, 32341 puts/second
0: 16414 keys missing
1: 16414 keys missing
200000 gets, 7.760 seconds, 25773 gets/second
```

可以看出，在未做任何修改的情况下，1个线程并没有任何丢失，2个线程时出现了16414个丢失的键。

2. 接下来开始解决丢失的问题。根据提示，想要解决丢失问题主要使用互斥锁。我们需要先声明锁，再初始化锁，最后在合适的位置加锁和释放锁。根据上面的分析，丢失主要是由于多个线程同时调用`insert`函数导致，所以我们需要在`put`函数调用`insert`函数时加锁。具体操作和代码如下：

​		1). 首先在`notxv6/ph.c`中定义互斥锁数组：

```c
pthread_mutex_t locks[NBUCKET]; // declare a lock lab 7-2 add
```

​		2.).在`notxv6/ph.c`的主函数中，初始化所有互斥锁数组中的所有互斥锁：

```c
int
main(int argc, char *argv[])
{
  //...
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random();
  }
  
  // lab 7-2 add initialize the lock
  for(int i = 0; i < NBUCKET; ++i) {
      pthread_mutex_init(&locks[i], NULL);
  }

  //
  // first the puts
  //...
}
```

​		3).给`put`函数中调用`insert`函数的代码加锁：

```c
static 
void put(int key, int value)
{
  //...
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    pthread_mutex_lock(&locks[i]);    // lock lab 7-2 add
    insert(key, value, &table[i], table[i]);
    pthread_mutex_unlock(&locks[i]);  // unlock lab 7-2 add
  }
}
```

​		4).增大`NBUCKET`，使散列效果更好，这时两个同时运行的线程进行 `put()` 时便大概率不会对同一个桶进行操作。

```c
#define NBUCKET 7   //lab 7-2 add
```

3. 测试：

首先运行`./ph 2`进行个人测试，验证输出是否符合预期:

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ make ph
gcc -o ph -g -O2 notxv6/ph.c -pthread
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./ph 2
100000 puts, 2.918 seconds, 34272 puts/second
0: 0 keys missing
1: 0 keys missing
200000 gets, 5.725 seconds, 34932 gets/second
```

没有key丢失，说明输出符合预期。

接下来进行单元测试，分别测试`ph_fast`和`ph_safe`，结果如下：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./grade-lab-thread ph_fast
make: 'kernel/kernel' is up to date.
== Test ph_fast == make: 'ph' is up to date.
ph_fast: OK (18.0s)
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./grade-lab-thread ph_safe
make: 'kernel/kernel' is up to date.
== Test ph_safe == make: 'ph' is up to date.
ph_safe: OK (8.7s)
```

输出正确，符合预期！

### 实验中遇到的困难和解决方法

​	实验中主要需要解决“为什么两个线程都丢失了键，而不是一个线程？确定可能导致键丢失的具有2个线程的事件序列”这一问题。通过查看`ph`命令的输出和阅读源代码并上网搜索相关资料，我确定问题是由于`put`函数中的`insert(key, value, &table[i], table[i])`所导致。

### 实验心得

​	在这个实验中，首先研究的是一些键为什么会丢失的问题，通过研究代码发现丢失主要是因为多个线程同时调用 `insert` 函数导致数据覆盖。这使我体会到了多线程情况下，某些临界资源不设置锁会导致一系列问题。

​	之后，我通过具体实践明白了互斥锁的实际作用，这可以有效避免多线程环境中的数据丢失和不一致问题。

## 3.Barrier([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	在本作业中，您将实现一个[屏障](http://en.wikipedia.org/wiki/Barrier_(computer_science))（Barrier）：应用程序中的一个点，所有参与的线程在此点上必须等待，直到所有其他参与线程也达到该点。您将使用pthread条件变量，这是一种序列协调技术，类似于xv6的`sleep`和`wakeup`。

文件***notxv6/barrier.c\***包含一个残缺的屏障实现。

```bash
$ make barrier
$ ./barrier 2
barrier: notxv6/barrier.c:42: thread: Assertion `i == t' failed.
```

​	`2`指定在屏障上同步的线程数（`barrier.c`中的`nthread`）。每个线程执行一个循环。在每次循环迭代中，线程都会调用`barrier()`，然后以随机微秒数休眠。如果一个线程在另一个线程到达屏障之前离开屏障将触发断言（assert）。期望的行为是每个线程在`barrier()`中阻塞，直到`nthreads`的所有线程都调用了`barrier()`。

​	您的目标是实现期望的屏障行为。除了在`ph`作业中看到的lock原语外，还需要以下新的`pthread`原语:

```c
pthread_cond_wait(&cond, &mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
pthread_cond_broadcast(&cond);     // wake up every thread sleeping on cond
```

### 实验步骤

1. 我们需要在`notxv6/barrier.c`中完成`barrier()`函数的具体实现。该函数思路如下：

​	首先，获取 `bstate.barrier_mutex` 互斥锁；然后，函数检查当前已经到达barrier的线程数 `bstate.nthread` 是否等于总线程数 `nthread`。如果 `bstate.nthread` 不等于 `nthread`，说明并非所有线程都已经到达barrier，需要继续等待其他线程；如果 `bstate.nthread` 等于 `nthread`，表示所有线程都已经到达barrier，此时需要将 `bstate.nthread` 重置为 0，并增加 `bstate.round` 的值，表示已经完成了一轮操作；最后，调用 `pthread_cond_broadcast(&bstate.barrier_cond)` 唤醒所有正在等待在 `bstate.barrier_cond` 条件变量上的线程并释放 `bstate.barrier_mutex` 互斥锁；

```c
static void 
barrier()
{
  // YOUR CODE HERE
  // lab 7-3 add
  pthread_mutex_lock(&bstate.barrier_mutex);
  if(++bstate.nthread != nthread)  {  //   并非全部线程到达barrier
    pthread_cond_wait(&bstate.barrier_cond,&bstate.barrier_mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
  } 
  else {  // 所有线程到达barrier
    bstate.nthread = 0; // 重置nthread
    ++bstate.round;
    pthread_cond_broadcast(&bstate.barrier_cond);   // wake up every thread sleeping on cond
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);  //unlock
}
```

2. 使用一个、两个和两个以上的线程测试代码：首先，需要运行`make barrier`构建`barrier`程序，然后分别运行`./barrier 1`,`./barrier 2`和`./barrier 3`测试各种情况，查看输出是否符合预期：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ make barrier
gcc -o barrier -g -O2 notxv6/barrier.c -pthread
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./barrier 1
OK; passed
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./barrier 2
OK; passed
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./barrier 3
OK; passed
```

​	输出符合预期，进行单元测试：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-7/xv6-labs-2020$ ./grade-lab-thread barrier
make: 'kernel/kernel' is up to date.
== Test barrier == make: 'barrier' is up to date.
barrier: OK (11.8s)
```

​	单元测试通过！

### 实验中遇到的困难和解决方法

​	本次实验关键在于理解`barrier`机制。这可以通过实验题目的介绍和搜索网络资料进行了解。

### 实验心得

​	本次实验主要学习了在多线程下如何实现barrier机制：

​	在操作系统中，Barrier是一种用于同步多个线程或进程的机制。它被用于确保多个线程或进程在特定点上“等待”或“同步”，直到所有相关线程或进程都到达这个点后，才会继续执行后续的操作。Barrier的主要目的是为了协调多个并发执行的线程或进程，以确保它们按照特定的顺序或条件来执行。

​	在操作系统中，Barrier通常由一个系统调用或特殊的同步原语来实现。它可以在用户空间或内核空间提供，具体取决于操作系统的设计。

​	Barrier机制在并发编程中非常有用，特别是在需要多个线程或进程协调工作的情况下。它可以避免竞争条件和死锁，并确保线程按照预期的顺序进行执行。然而，在使用Barrier时需要小心处理潜在的死锁和性能问题，因为Barrier会引入额外的开销和线程间的等待。因此，在设计和实现Barrier时，需要仔细考虑多线程同步和性能方面的问题。

## 评分并提交

运行`make grade`，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
== Test uthread ==
$ make qemu-gdb
uthread: OK (4.3s)
== Test answers-thread.txt == answers-thread.txt: OK
== Test ph_safe == make[1]: Entering directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
gcc -o ph -g -O2 notxv6/ph.c -pthread
make[1]: Leaving directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
ph_safe: OK (9.1s)
== Test ph_fast == make[1]: Entering directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
make[1]: 'ph' is up to date.
make[1]: Leaving directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
ph_fast: OK (18.4s)
== Test barrier == make[1]: Entering directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
make[1]: 'barrier' is up to date.
make[1]: Leaving directory '/home/bronya_k/xv6-labs-7/xv6-labs-2020'
barrier: OK (11.6s)
== Test time ==
time: OK
Score: 60/60
```

测试通过！提交至远程代码仓库即可。