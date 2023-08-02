# Lab6 Copy-on-Write Fork for xv6

[TOC]

​	虚拟内存提供了一定程度的间接寻址：内核可以通过将PTE标记为无效或只读来拦截内存引用，从而导致页面错误，还可以通过修改PTE来更改地址的含义。在计算机系统中有一种说法，任何系统问题都可以用某种程度的抽象方法来解决。Lazy allocation实验中提供了一个例子。这个实验探索了另一个例子：写时复制分支（copy-on write fork）。

## Implement copy-on write (hard)

### 实验目的

​	xv6中的`fork()`系统调用将父进程的所有用户空间内存复制到子进程中。如果父进程较大，则复制可能需要很长时间。更糟糕的是，这项工作经常造成大量浪费；例如，子进程中的`fork()`后跟`exec()`将导致子进程丢弃复制的内存，而其中的大部分可能都从未使用过。另一方面，如果父子进程都使用一个页面，并且其中一个或两个对该页面有写操作，则确实需要复制。

​	copy-on-write (COW) fork()的目标是推迟到子进程实际需要物理内存拷贝时再进行分配和复制物理内存页面。

​	COW fork()只为子进程创建一个页表，用户内存的PTE指向父进程的物理页。COW fork()将父进程和子进程中的所有用户PTE标记为不可写。当任一进程试图写入其中一个COW页时，CPU将强制产生页面错误。内核页面错误处理程序检测到这种情况将为出错进程分配一页物理内存，将原始页复制到新页中，并修改出错进程中的相关PTE指向新的页面，将PTE标记为可写。当页面错误处理程序返回时，用户进程将能够写入其页面副本。

​	COW fork()将使得释放用户内存的物理页面变得更加棘手。给定的物理页可能会被多个进程的页表引用，并且只有在最后一个引用消失时才应该被释放。

​	您的任务是在xv6内核中实现copy-on-write fork。如果修改后的内核同时成功执行`cowtest`和`usertests`程序就完成了。

### 实验步骤

1. 构造设计cow结构体和引用计数的加一函数`increfcnt`和减一函数`decrefcnt`，保存在一个新建的文件`kernel/cow.c`中.

   ​	在该文件中，定义了一个数组`cows`来记录每个物理页的引用计数和对应的自旋锁。由于xv6操作系统只支持最多64个进程，所以引用计数的数组大小被设计为`(PHYSTOP-KERNBASE)>>12`，其中`PHYSTOP`是系统支持的最大物理内存地址，`KERNBASE`是内核虚拟地址空间的基址。这样的设计能够实现对所有物理页的引用计数进行压缩存储。

   ​	定义了两个函数`increfcnt()`和`decrefcnt()`，用于对引用计数进行加一和减一操作。这两个函数的输入参数是物理地址`pa`，它们首先将物理地址转换成对应的物理页的索引，然后对该物理页的引用计数进行加一或减一操作。在对引用计数进行操作时，使用了自旋锁进行保护.

```c
//lab 6 add
#include "types.h"
#include "spinlock.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// cow
struct {
  uint8 ref_cnt;		//计数
  struct spinlock lock;	
} cows[(PHYSTOP - KERNBASE) >> 12];//压缩的页引用数

// 引用计数+1
void 
increfcnt(uint64 pa) 
{
  if (pa < KERNBASE) {
    return;
  }
    //物理地址所在物理页的引用计数元素
  pa = (pa - KERNBASE) >> 12;
  acquire(&cows[pa].lock);
  ++cows[pa].ref_cnt;
  release(&cows[pa].lock);
}

// 引用计数-1
uint8 
decrefcnt(uint64 pa) 
{
  uint8 ret;
  if (pa < KERNBASE) {
    return 0;
  }
  pa = (pa - KERNBASE) >> 12;
  acquire(&cows[pa].lock);
  ret = --cows[pa].ref_cnt;
  release(&cows[pa].lock);
  return ret;
}
```

​	添加后需要在`kernel/defs.h`中添加这`increfcnt`和`decrefcnt`函数的声明，并在`Makefile`中添加编译链接。

```makefile
$K/cow.o\
```

2. 设置COW的标记位

   由于COW机制的虚拟页引发的page fault和平常的新物理页面分配的page fault是无法自己分辨的，这里需要给COW机制的虚拟页加上一个标记位来区分。

   ```c
   //kernel/riscv.h
   #define PTE_COW (1L << 8)      // COW lab 6 add
   ```

3. 为了实现cow，需要修改 `uvmcopy()` 函数，根据提示，需要修改`uvmcopy()`将父进程的物理页映射到子进程，而不是分配新页并且在子进程和父进程的PTE中清除`PTE_W`标志，修改如下：

   移除原本的写标志位 `PTE_W`：在原本的`uvmcopy()`函数中，子进程对父进程用户页表的拷贝会复制父进程的页表项，并将其中的写标志位 `PTE_W` 设置为可写，表示子进程可以修改这些页。然而，在COW机制中，子进程不需要实际修改这些页，而是共享父进程的物理页，因此不应该将写标志位设置为可写。

   添加COW标志位 `PTE_COW`：为了标记这是一个COW机制共享的页面，我们在页表项中设置一个特殊的COW标志位 `PTE_COW`。这个标志位用来区分普通的页表项和COW机制共享的页表项。

   最后通过`increfcnt`函数对共享物理页的引用计数+1.

   ```c
   int
   uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
   {
     pte_t *pte;
     uint64 pa, i;
     uint flags;
     //char *mem;
   
     for(i = 0; i < sz; i += PGSIZE){
       if((pte = walk(old, i, 0)) == 0)
         panic("uvmcopy: pte should exist");
       if((*pte & PTE_V) == 0)
         panic("uvmcopy: page not present");
       pa = PTE2PA(*pte);
   
       // 清除 PTE_W 标志，增加 COW 标志
       flags = (PTE_FLAGS(*pte) & (~PTE_W)) | PTE_COW;
       *pte = PA2PTE(pa) | flags;  
   
       //flags = PTE_FLAGS(*pte);
       /*if((mem = kalloc()) == 0)
         goto err;
       memmove(mem, (char*)pa, PGSIZE);*/
       if(mappages(new, i, PGSIZE, pa, flags) != 0){
         //kfree(mem);
         goto err;
       }
       increfcnt(pa);  //lab 6 add  cow引用计数+1
     }
     return 0;
   
    err:
     uvmunmap(new, 0, i / PGSIZE, 1);
     return -1;
   }
   ```

4. 接下来需要实现cow机制，根据提示，我们需要修改`usertrap()`和`copyout()`以识别页面错误。当COW页面出现页面错误时，使用`kalloc()`分配一个新页面，并将旧页面复制到新页面，然后将新页面添加到PTE中并设置`PTE_W`。

​	   由于对`usertrap()`和`copyout()`两函数的处理类似，所以可以在`kernel/vm.c`中新增函数 `walkcowaddr()` 进行了统一处理。`walkcowaddr()` 原型出自 `walkaddr()` 函数，增加了对COW页面的处理。`walkcowaddr()`中，当调用 `walk()` 查找到虚拟地址 `va` 对应的页表项 `pte`后，检查写标志位，然后，函数会检查页表项中的写标志位 `PTE_W` 是否被设置。如果写标志位没有被设置，说明这个物理页是只读的，不可写，因此不需要进行COW处理，直接返回物理地址；若写标志位被设置，则说明需要进行COW处理。首先，函数会检查是否有COW标志位 `PTE_COW`。如果没有COW标志位，说明不可写，直接返回，若有COW标志位，则分配新的物理页并重新映射用户页表，返回新的物理地址，同时需要用`PTE_W`的标志代替现在的`PTE_COW`标志。代码如下：

```c
//lab 6 add
uint64
walkcowaddr(pagetable_t pagetable, uint64 va) 
{
  pte_t *pte;
  uint64 pa;
  char* mem;
  uint flags;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
      return 0;
  if ((*pte & PTE_V) == 0)
      return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  // 写标志位判断
  if ((*pte & PTE_W) == 0) {
    // 无COW标志位，不可写，直接返回
    if ((*pte & PTE_COW) == 0) {
        return 0;
    }
    // 分配新物理页
    if ((mem = kalloc()) == 0) {
      return 0;
    }
    memmove(mem, (void*)pa, PGSIZE);
    flags = (PTE_FLAGS(*pte) & (~PTE_COW)) | PTE_W;
    uvmunmap(pagetable, PGROUNDDOWN(va), 1, 1);
    if (mappages(pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      return 0;
    }
    return (uint64)mem;   
  }
  return pa;
}
```

`walkcowaddr()` 函数完成后，需要在 `usertrap()` 和 `copyout()` 中调用它，相关代码如下：

```c
//kernel/trap.c
void
usertrap(void)
{
  //...
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } 
  else if(r_scause() == 15) {   //lab 6 add
    if (walkcowaddr(p->pagetable, r_stval()) == 0) {
      goto bad;
    }
  }
  else if((which_dev = devintr()) != 0){
    // ok
  } 
  else {
bad:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  //...
}

//kernel/vm.c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkcowaddr(pagetable, va0);  // lab 6 modified
    //...
  }
  return 0;
}
```

5. 根据提示，由于引入了引用计数，因此还需要修改`kalloc()`函数、`kfree()`函数和`freerange()`函数，首先是`kalloc`函数：

​		`kalloc`函数中，需要执行将一个物理页分配给一个进程的操作，因此需要使引用计数为1，可以调用 `increfcnt()` 函数，实验将引用计数从初始的0加到1，代码如下：

```c
//kernel/kalloc.c
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  increfcnt((uint64)r);		//lab 6 add

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

​		接下来修改`kfree`函数，这个函数负责物理页的释放。在回收物理页之前，需要将引用计数-1，并判断是否为0，不为0则说明有其他进程还在使用这个物理页，在这种情况下不应该回收；如果引用计数为0，就可以回收。代码如下：

```c
//kernel/kalloc.c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // COW计数-1
  if (decrefcnt((uint64) pa)) {
  //计数!=0，返回
    return;
  }

  //计数==0，回收
  memset(pa, 1, PGSIZE);
  //...
}
```

​		最后需要修改的是`freerange()`函数。它把物理内存空间中未使用的部分以物理页划分调用 `kfree()` 将其添加至 `kmem.freelist` 中。`cows`中的引用计数初始值为0，调用`kfree()` 函数时会将引用计数减 1，引发错误。所以需要在`kfree`之前，先将引用计数+1，这样`kfree`后计数刚好是0，可以正常回收。修改后代码如下：

```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    increfcnt((uint64)p); // cow +1 lab 6 add
    kfree(p);
  }
}
```

6. 测试：启动qemu后，分别运行`cowtest`和`usertests`，输出如下，说明实验测试通过

```shell
$ cowtest
simple: ok
simple: ok
three: ok
three: ok
three: ok
file: ok
ALL COW TESTS PASSED
$ usertest
exec usertest failed
$ usertests
usertests starting
test execout: usertrap(): unexpected scause 0x000000000000000f pid=20
            sepc=0x0000000000002ac0 stval=0x0000000000010b88
OK
test copyin: OK
test copyout: OK
test copyinstr1: OK
test copyinstr2: OK
test copyinstr3: OK
test rwsbrk: OK
test truncate1: OK
test truncate2: OK
test truncate3: OK
test reparent2: OK
test pgbug: OK
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3249
            sepc=0x000000000000555e stval=0x000000000000555e
usertrap(): unexpected scause 0x000000000000000c pid=3250
            sepc=0x000000000000555e stval=0x000000000000555e
OK
test badarg: OK
test reparent: OK
test twochildren: OK
test forkfork: OK
test forkforkfork: OK
test argptest: OK
test createdelete: OK
test linkunlink: OK
test linktest: OK
test unlinkread: OK
test concreate: OK
test subdir: OK
test fourfiles: OK
test sharedfd: OK
test dirtest: OK
test exectest: OK
test bigargtest: OK
test bigwrite: OK
test bsstest: OK
test sbrkbasic: OK
test sbrkmuch: OK
test kernmem: usertrap(): unexpected scause 0x000000000000000d pid=6230
            sepc=0x0000000000002026 stval=0x0000000080000000
usertrap(): unexpected scause 0x000000000000000d pid=6231
            sepc=0x0000000000002026 stval=0x000000008000c350
usertrap(): unexpected scause 0x000000000000000d pid=6232
            sepc=0x0000000000002026 stval=0x00000000800186a0
usertrap(): unexpected scause 0x000000000000000d pid=6233
            sepc=0x0000000000002026 stval=0x00000000800249f0
usertrap(): unexpected scause 0x000000000000000d pid=6234
            sepc=0x0000000000002026 stval=0x0000000080030d40
usertrap(): unexpected scause 0x000000000000000d pid=6235
            sepc=0x0000000000002026 stval=0x000000008003d090
usertrap(): unexpected scause 0x000000000000000d pid=6236
            sepc=0x0000000000002026 stval=0x00000000800493e0
usertrap(): unexpected scause 0x000000000000000d pid=6237
            sepc=0x0000000000002026 stval=0x0000000080055730
usertrap(): unexpected scause 0x000000000000000d pid=6238
            sepc=0x0000000000002026 stval=0x0000000080061a80
usertrap(): unexpected scause 0x000000000000000d pid=6239
            sepc=0x0000000000002026 stval=0x000000008006ddd0
usertrap(): unexpected scause 0x000000000000000d pid=6240
            sepc=0x0000000000002026 stval=0x000000008007a120
usertrap(): unexpected scause 0x000000000000000d pid=6241
            sepc=0x0000000000002026 stval=0x0000000080086470
usertrap(): unexpected scause 0x000000000000000d pid=6242
            sepc=0x0000000000002026 stval=0x00000000800927c0
usertrap(): unexpected scause 0x000000000000000d pid=6243
            sepc=0x0000000000002026 stval=0x000000008009eb10
usertrap(): unexpected scause 0x000000000000000d pid=6244
            sepc=0x0000000000002026 stval=0x00000000800aae60
usertrap(): unexpected scause 0x000000000000000d pid=6245
            sepc=0x0000000000002026 stval=0x00000000800b71b0
usertrap(): unexpected scause 0x000000000000000d pid=6246
            sepc=0x0000000000002026 stval=0x00000000800c3500
usertrap(): unexpected scause 0x000000000000000d pid=6247
            sepc=0x0000000000002026 stval=0x00000000800cf850
usertrap(): unexpected scause 0x000000000000000d pid=6248
            sepc=0x0000000000002026 stval=0x00000000800dbba0
usertrap(): unexpected scause 0x000000000000000d pid=6249
            sepc=0x0000000000002026 stval=0x00000000800e7ef0
usertrap(): unexpected scause 0x000000000000000d pid=6250
            sepc=0x0000000000002026 stval=0x00000000800f4240
usertrap(): unexpected scause 0x000000000000000d pid=6251
            sepc=0x0000000000002026 stval=0x0000000080100590
usertrap(): unexpected scause 0x000000000000000d pid=6252
            sepc=0x0000000000002026 stval=0x000000008010c8e0
usertrap(): unexpected scause 0x000000000000000d pid=6253
            sepc=0x0000000000002026 stval=0x0000000080118c30
usertrap(): unexpected scause 0x000000000000000d pid=6254
            sepc=0x0000000000002026 stval=0x0000000080124f80
usertrap(): unexpected scause 0x000000000000000d pid=6255
            sepc=0x0000000000002026 stval=0x00000000801312d0
usertrap(): unexpected scause 0x000000000000000d pid=6256
            sepc=0x0000000000002026 stval=0x000000008013d620
usertrap(): unexpected scause 0x000000000000000d pid=6257
            sepc=0x0000000000002026 stval=0x0000000080149970
usertrap(): unexpected scause 0x000000000000000d pid=6258
            sepc=0x0000000000002026 stval=0x0000000080155cc0
usertrap(): unexpected scause 0x000000000000000d pid=6259
            sepc=0x0000000000002026 stval=0x0000000080162010
usertrap(): unexpected scause 0x000000000000000d pid=6260
            sepc=0x0000000000002026 stval=0x000000008016e360
usertrap(): unexpected scause 0x000000000000000d pid=6261
            sepc=0x0000000000002026 stval=0x000000008017a6b0
usertrap(): unexpected scause 0x000000000000000d pid=6262
            sepc=0x0000000000002026 stval=0x0000000080186a00
usertrap(): unexpected scause 0x000000000000000d pid=6263
            sepc=0x0000000000002026 stval=0x0000000080192d50
usertrap(): unexpected scause 0x000000000000000d pid=6264
            sepc=0x0000000000002026 stval=0x000000008019f0a0
usertrap(): unexpected scause 0x000000000000000d pid=6265
            sepc=0x0000000000002026 stval=0x00000000801ab3f0
usertrap(): unexpected scause 0x000000000000000d pid=6266
            sepc=0x0000000000002026 stval=0x00000000801b7740
usertrap(): unexpected scause 0x000000000000000d pid=6267
            sepc=0x0000000000002026 stval=0x00000000801c3a90
usertrap(): unexpected scause 0x000000000000000d pid=6268
            sepc=0x0000000000002026 stval=0x00000000801cfde0
usertrap(): unexpected scause 0x000000000000000d pid=6269
            sepc=0x0000000000002026 stval=0x00000000801dc130
OK
test sbrkfail: usertrap(): unexpected scause 0x000000000000000d pid=6281
            sepc=0x00000000000040c6 stval=0x0000000000012000
OK
test sbrkarg: OK
test validatetest: OK
test stacktest: usertrap(): unexpected scause 0x000000000000000d pid=6285
            sepc=0x0000000000002196 stval=0x000000000000fba0
OK
test opentest: OK
test writetest: OK
test writebig: OK
test createtest: OK
test openiput: OK
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

### 实验中遇到的困难和解决方法

1. 在启动`qemu`时报错：

```shell
Makefile:39: *** missing separator.  Stop.
```

这是由于在`Makefile`中添加`cow.o`的编译链接格式不正确导致的，将其修改为正确的格式后问题解决。

2. 编译报错：

```shell
kernel/vm.c: In function 'walkcowaddr':
kernel/vm.c:30:9: error: implicit declaration of function 'walk' [-Werror=implicit-function-declaration]
```

这是由于未将`walk`函数添加到`defs.h`中声明导致的，添加`walk`函数声明后问题解决。

### 实验心得

​	通过本次实验，我学习了copy-on-write这一知识点。

​	在xv6中的fork()系统调用会创建一个新的子进程，该子进程是父进程的副本。这意味着子进程将获得与父进程相同的代码、数据和堆栈内容。为了实现这一点，xv6在fork()系统调用中将父进程的用户空间内存复制到子进程中。

​	然而，父进程的用户空间内存可能非常大，复制整个用户空间内存会导致时间和内存资源的浪费。因为在许多情况下，子进程在接下来的exec()系统调用后会立即丢弃复制的内存，这些复制的内存可能从未被使用过。

​	这种情况下的浪费可以通过Copy-on-Write来避免。Copy-on-Write可以使运行fork()系统调用时并不实际复制父进程的用户空间内存，而是让父子进程共享同一个物理页面，直到有一个进程试图对页面进行写操作时，才会将页面复制，从而保持数据的完整性。这样做的好处是在大多数情况下，子进程不会修改父进程的内存，因此可以避免复制大量的内存，减少内存的浪费。

​	只有当父子进程中的任何一个或两个进程试图修改共享页面时，操作系统才会执行实际的复制操作，将页面内容复制到新的物理页面中，使得父子进程拥有各自的独立内存空间。

​	这在fork()后紧接着exec()的情况下特别有用，因为exec()会导致子进程的内存被完全替换为新的程序，这样就不会再使用父进程的内存，从而避免了之前的浪费。而在fork()后没有紧接着exec()的情况下，父子进程仍然共享内存，直到其中一个进程试图修改它，才会触发复制。这种惰性复制技术显著减少了内存使用，提高了性能。

## 实验评分并提交

运行`make grade`，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-6/xv6-labs-2020'
== Test running cowtest ==
$ make qemu-gdb
(8.3s)
== Test   simple ==
  simple: OK
== Test   three ==
  three: OK
== Test   file ==
  file: OK
== Test usertests ==
$ make qemu-gdb
(69.4s)
== Test   usertests: copyin ==
  usertests: copyin: OK
== Test   usertests: copyout ==
  usertests: copyout: OK
== Test   usertests: all tests ==
  usertests: all tests: OK
== Test time ==
time: OK
Score: 110/110
```

实验成功！提交到远程代码仓库即可。