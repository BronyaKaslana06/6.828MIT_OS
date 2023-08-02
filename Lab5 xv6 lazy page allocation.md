# Lab 5 xv6 lazy page allocation

[TOC]

​	操作系统可以使用页表硬件的技巧之一是延迟分配用户空间堆内存（lazy allocation of user-space heap memory）。Xv6应用程序使用`sbrk()`系统调用向内核请求堆内存。在我们给出的内核中，`sbrk()`分配物理内存并将其映射到进程的虚拟地址空间。内核为一个大请求分配和映射内存可能需要很长时间。例如，考虑由262144个4096字节的页组成的千兆字节；即使单独一个页面的分配开销很低，但合起来如此大的分配数量将不可忽视。此外，有些程序申请分配的内存比实际使用的要多（例如，实现稀疏数组），或者为了以后的不时之需而分配内存。为了让`sbrk()`在这些情况下更快地完成，复杂的内核会延迟分配用户内存。也就是说，`sbrk()`不分配物理内存，只是记住分配了哪些用户地址，并在用户页表中将这些地址标记为无效。当进程第一次尝试使用延迟分配中给定的页面时，CPU生成一个页面错误（page fault），内核通过分配物理内存、置零并添加映射来处理该错误。您将在这个实验室中向xv6添加这个延迟分配特性。

## 1.Eliminate allocation from sbrk() ([easy](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	你的首项任务是删除`sbrk(n)`系统调用中的页面分配代码（位于`sysproc.c`中的函数`sys_sbrk()`）。`sbrk(n)`系统调用将进程的内存大小增加n个字节，然后返回新分配区域的开始部分（即旧的大小）。新的`sbrk(n)`应该只将进程的大小（`myproc()->sz`）增加n，然后返回旧的大小。它不应该分配内存——因此您应该删除对`growproc()`的调用（但是您仍然需要增加进程的大小！）。

​	进行此修改，启动xv6，并在`shell`中键入`echo hi`。你应该看到这样的输出：

```bash
init: starting sh
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x0000000000001258 stval=0x0000000000004008
va=0x0000000000004000 pte=0x0000000000000000
panic: uvmunmap: not mapped
```

### 实验过程

1. 按照实验提示，在`kernel/sysproc.c`修改 `sys_sbrk()` 函数, 删去原本调用的 `growproc()` 函数，使新的`sbrk(n)`应该只将进程的大小（`myproc()->sz`）增加n，然后返回旧的大小。修改后的函数如下：

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  //lab 5-1 add
  addr = myproc()->sz;
  // lazy allocation
  myproc()->sz += n;
  // lab 5-1 delete
  /*if(growproc(n) < 0)
    return -1;*/
  return addr;
}
```

2. 测试：`make qemu`启动xv6后，运行命令`echo hi`，输出结果如下：

```shell
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x00000000000012ac stval=0x0000000000014008
panic: uvmunmap: not mapped
```

输出符合预期

### 实验中遇到的问题和解决办法

按照提示修改相应的实验代码，没有遇到困难。

### 实验心得

​	`“ usertrap（）：...”`消息来自用户陷入处理程序` trap.c`; 它捕获了一个不知道如何处理的异常。 确保您了解为什么发生此页面错误： ` stval = 0x0..14008`指示导致页面错误的虚拟地址为`0x14008`。这应该是由于`uvmdealloc`释放了错误的内存空间导致的。

## 2. Lazy allocation ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	修改`trap.c`中的代码以响应来自用户空间的页面错误，方法是新分配一个物理页面并映射到发生错误的地址，然后返回到用户空间，让进程继续执行。您应该在生成“`usertrap(): …`”消息的`printf`调用之前添加代码。你可以修改任何其他xv6内核代码，以使`echo hi`正常工作。

### 实验过程

1.在 `kernel/trap.c` 的 `usertrap()` 中添加对 page fault 的处理：根据提示，当 r_scause() 的值为 13 和 15 时为需要处理的 page fault 情况。所以通过检查 `r_scause()` 的值来确定中断原因，如果是 13 或 15，表示发生了页面错误，然后从 `r_stval()` 中获取错误的虚拟地址，并使用 `kalloc()` 分配一个物理页。然后，使用 `mappages()` 将物理页映射到页表中的虚拟地址。如果分配和映射成功，则继续执行用户程序。如果分配或映射失败，则杀死进程。以下是修改后的代码片段：

````c
void
usertrap(void)
{
  //...
  //lab 5-2 add
  else if (r_scause() == 13 || r_scause() == 15) {  
    char *pa;
    if((pa = kalloc()) != 0) {    // 分配物理页
      uint64 va = PGROUNDDOWN(r_stval());   // 引发page fault的虚拟地址向下取整
      memset(pa, 0, PGSIZE);
      // 进行页表映射
      if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_W|PTE_R|PTE_U) != 0) {
          // 页表映射失败
          kfree(pa);
          printf("usertrap(): mappages() failed\n");
          p->killed = 1;
      }
    } else {    // 分配物理页失败
      printf("usertrap(): kalloc() failed\n");
      p->killed = 1;
    }
  } 
  else if((which_dev = devintr()) != 0){
    // ok
  } 
  else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
````

2.处理 `uvmunmap` 的报错

`uvmunmap` 是用来释放内存调用的函数，由于页表内有些地址并没有实际分配内存，因此没有进行映射。如果在 `uvmunmap` 中发现了没有映射的地址，不触发 panic，直接跳过。修改后的代码如下：

````C
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      //panic("uvmunmap: not mapped");
      continue;   //lab 5-2 add
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}
````

3. 测试：输入`echo hi`指令正常执行：

```shell
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ echo hi
hi
```

### 实验中遇到的问题和解决办法

1. 提示中提到了要使用PGROUNDDOWN(va) ，那么这有什么意义？

​	`PGROUNDDOWN(va)` 的作用是将虚拟地址 `va` 向下舍入到最接近的页面边界。在 xv6 操作系统中，页面大小是 4KB（或者 4096 字节），因此页面边界是以 4KB 为间隔的地址。当处理页面错误时，需要将虚拟地址对齐到页面边界，以便正确地进行页面映射和分配。通过使用 `PGROUNDDOWN(va)`，我们可以获得距离 `va` 最近的页面边界的地址。这样，我们就可以确保映射的物理页面和虚拟地址是对齐的，以满足操作系统的页面大小要求。

​	例如，如果 `va` 的值为 0x80001000，那么 `PGROUNDDOWN(va)` 将返回 0x80000000，这是最接近 `va` 的页面边界地址。这样，我们就可以将该页面边界作为起点，进行页面映射或分配的操作。

2. 如何确定我要删去 `uvmunmap`中的哪个`panic`使系统不再报错？

​	在第二个实验中，我们主要删去了下面这种情况中对应的`panic`：

```c
    if((*pte & PTE_V) == 0)
      continue;
```

​	通过 `*pte & PTE_V` 的表达式，可以获取页表项中 `PTE_V` 的值。如果结果为 0，表示该页表项无效，意味着对应的页面未映射或未分配。在lazy allocation中，并未先实际分配内存，所以当解除映射关系的时候对于这部分内存要略过，而不是使系统崩溃。

### 实验心得

​	在 xv6 操作系统中，延迟分配（lazy allocation）是一种优化策略，用于在需要时才分配物理内存页面，而不是在进程创建或内存映射时就立即分配。这种延迟分配的方式可以提高内存的利用率，并减少不必要的内存分配。

​	在本次实验中，我主要学习到了：

1. **page fault处理流程**：实验要求对page fault进行处理，需要理解页面错误的原因以及相应的处理步骤。这包括检查页错误的中断原因、获取故障的虚拟地址和处理分配物理内存的过程。
1. **页表操作和物理内存分配：**实验中需要修改页表以实现动态的物理内存分配。这需要熟悉页表的结构和操作方法，以及物理内存的分配和释放过程。主要是使用 `kalloc()` 分配物理页面，并通过 `mappages()` 进行页表映射。

​	通过完成这次实验，我对 xv6 操作系统中的页错误处理机制有了更深入的理解并对延迟分配的概念有了实际的实践。

## 3. Lazytests and Usertests ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	实验中提供了`lazytests`，这是一个xv6用户程序，它测试一些可能会给您的惰性内存分配器带来压力的特定情况。修改内核代码，使所有`lazytests`和`usertests`都通过。一些提示如下：

- 处理`sbrk()`参数为负的情况。
- 如果某个进程在高于`sbrk()`分配的任何虚拟内存地址上出现页错误，则终止该进程。
- 在`fork()`中正确处理父到子内存拷贝。
- 处理这种情形：进程从`sbrk()`向系统调用（如`read`或`write`）传递有效地址，但尚未分配该地址的内存。
- 正确处理内存不足：如果在页面错误处理程序中执行`kalloc()`失败，则终止当前进程。
- 处理用户栈下面的无效页面上发生的错误。

如果内核通过`lazytests`和`usertests`，那么您的解决方案是可以接受的：

### 实验过程

1. 处理`sbrk()`参数为负的情况：如果`n`是负数且`addr + n`大于等于用户堆栈的顶部地址（`PGROUNDUP(p->trapframe->sp)`），则表示需要减少进程的内存大小。此时，将内存释放掉，并将进程的大小更新为`uvmdealloc()`函数的返回值，这是确保新的结束地址不超出用户堆栈的范围；如果`n`是负数且`addr + n`小于用户堆栈的顶部地址（`PGROUNDUP(p->trapframe->sp)`），则表示要缩小内存大小超过了用户堆栈的范围，这是不允许的，因此返回-1，表示操作失败。代码如下：

```c
//kernel/sysproc.c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p;

  if(argint(0, &n) < 0)
    return -1;
  p = myproc();
  addr = p->sz;
  
  if(n >= 0 && addr + n >= addr){
    p->sz += n;    
  } 
  else if(n < 0 && addr + n >= PGROUNDUP(p->trapframe->sp)){
    p->sz = uvmdealloc(p->pagetable, addr, addr + n);
  } 
  else {
    return -1;
  }

// lab 5-1 delete
//  if(growproc(n) < 0)
//    return -1;
  return addr;
}
```

2. 修改 `kernel/vm.c` 中的 `uvmunmap()` 函数, 将`if((pte = walk(pagetable, a, 0)) == 0)` 的情况由引发 panic 改为 continue 跳过。`if((pte = walk(pagetable, a, 0)) == 0)` 是在判断虚拟地址 `a` 在页表 `pagetable` 中的页表项是否存在。 此时, 一般是 sbrk() 申请了较大内存， L2 或 L1 中的 PTE 就未分配，致使 L0 页目录就不存在, 虚拟地址对应的 PTE 也就不存在。

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
 //...
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      //panic("uvmunmap: walk");
      continue;   //lab 5-3 add
    if((*pte & PTE_V) == 0)
      //panic("uvmunmap: not mapped");
      continue;   //lab 5-2 add
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    //...
}
```

3. 在`fork()`中正确处理父到子内存拷贝:

​	`fork()` 是通过 `uvmcopy()` 来进行父进程页表即用户空间向子进程拷贝的。对于 `uvmcopy()` 的处理和 `uvmunmap()` 是一致的, 只需要将页表项PTE不存在和无效的两种情况由引发 panic 改为 continue 跳过即可，代码如下：

```c
//kernel/vm.c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      //panic("uvmcopy: pte should exist");
      continue; //lab 5-3 add
    if((*pte & PTE_V) == 0)
      //panic("uvmcopy: page not present");
      continue; //lab 5-3 add
      //...
}
```

4. 处理进程从`sbrk()`向系统调用（如`read`或`write`）传递有效地址，但尚未分配该地址的内存这种情况：

   ​	`	walkaddr()` 函数用于根据虚拟地址获取物理地址，如果虚拟地址对应的页表项不存在，即虚拟地址还未分配物理内存，那么 `walkaddr()` 函数会进行额外的处理，即进行延迟分配（lazy allocation）。

   ​	在 `walkaddr()` 函数中，首先检查虚拟地址 `va` 是否在用户堆空间的范围内，这里使用 `va >= PGROUNDUP(p->trapframe->sp) && va < p->sz` 进行判断。如果 `va` 在用户堆空间内，则进行以下处理：

   1. 调用 `kalloc()` 分配一个物理页面。
   2. 将分配的物理页面内容清零，确保其为空。
   3. 调用 `mappages()` 将虚拟地址 `va` 映射到刚刚分配的物理页面。

   这样，在之后的系统调用（如 `read` 或 `write`）中，当进程使用尚未分配的虚拟地址时，会触发页面错误（Page Fault），然后再通过页面错误处理的逻辑，调用 `walkaddr()` 函数，进行上述延迟分配操作，实现了对尚未分配内存的虚拟地址进行处理的功能。

   修改后代码如下：

```c
//kernel/vm.c
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  // lab 5-3 modified
  if(pte == 0 || (*pte & PTE_V) == 0) {
    struct proc *p=myproc();  // lab 5-3 add
    // va在用户的堆空间内
    if(va >= PGROUNDUP(p->trapframe->sp) && va < p->sz){
        char *pa;
        if ((pa = kalloc()) == 0) {
            return 0;
        }
        memset(pa, 0, PGSIZE);
        if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE,
            (uint64) pa, PTE_W | PTE_R | PTE_U) != 0) {
            kfree(pa);
            return 0;
        }
    } 
    else {
        return 0;
    }
  }
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}
```

5. 处理用户栈下面的无效页面上发生的错误和内存不足：

在 `usertrap()` 中对虚拟地址超过进程大小或低于用户栈的情况进行处理，如果超出了合法范围，则杀死进程；如果在页面错误处理程序中执行`kalloc()`失败，则也杀死当前进程。代码如下：

```c
//kernel/trap.c
void
usertrap(void)
{
  //...
  //lab 5-3 modified
  else if (r_scause() == 13 || r_scause() == 15){
    char *pa;
    uint64 va = r_stval();
    // 虚拟地址超过 p->sz或低于用户栈时，kill
    if(va >= p->sz){
      printf("usertrap(): invalid va=%p higher than p->sz=%p\n",
             va, p->sz);
      p->killed = 1;
      goto end;
    }
    if(va < PGROUNDUP(p->trapframe->sp)) {  // new code
      printf("usertrap(): invalid va=%p below the user stack sp=%p\n",
             va, p->trapframe->sp);
      p->killed = 1;
      goto end;
    }
    if ((pa = kalloc()) == 0) {
        printf("usertrap(): kalloc() failed\n");
        p->killed = 1;
        goto end;
    }
    memset(pa, 0, PGSIZE);
    if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64) pa, PTE_W | PTE_R | PTE_U) != 0) {
        kfree(pa);
        printf("usertrap(): mappages() failed\n");
        p->killed = 1;
        goto end;
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  //lab 5-3 add
  end:    
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
```

6. 测试：运行`lazytests`输出如下，说明测试通过

```shell
$ lazytests
lazytests starting
running test lazy alloc
test lazy alloc: OK
running test lazy unmap
usertrap(): invalid va=0x0000000000004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000001004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000002004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000003004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000004004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000005004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000006004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000007004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000008004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000009004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000000a004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000000b004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000000c004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000000d004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000000e004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000000f004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000010004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000011004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000012004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000013004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000014004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000015004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000016004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000017004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000018004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000019004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000001a004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000001b004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000001c004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000001d004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000001e004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000001f004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000020004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000021004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000022004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000023004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000024004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000025004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000026004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000027004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000028004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000029004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000002a004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000002b004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000002c004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000002d004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000002e004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000002f004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000030004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000031004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000032004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000033004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000034004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000035004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000036004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000037004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000038004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x0000000039004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000003a004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000003b004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000003c004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000003d004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000003e004000 higher than p->sz=0x0000000000003000
usertrap(): invalid va=0x000000003f004000 higher than p->sz=0x0000000000003000
test lazy unmap: OK
running test out of memory
usertrap(): invalid va=0xffffffff80003808 higher than p->sz=0x0000000081003810
test out of memory: OK
ALL TESTS PASSED
```

运行`usertests`输出如下，说明通过：

```shell
$ usertests
usertests starting
usertrap(): kalloc() failed
test execout: usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
usertrap(): kalloc() failed
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
test sbrkbugs: OK
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
test exectest: OK
test bigargtest: OK
test bigwrite: OK
test bsstest: OK
test sbrkbasic: usertrap(): kalloc() failed
OK
test sbrkmuch: OK
test kernmem: usertrap(): invalid va=0x0000000080000000 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008000c350 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800186a0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800249f0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080030d40 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008003d090 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800493e0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080055730 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080061a80 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008006ddd0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008007a120 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080086470 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800927c0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008009eb10 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800aae60 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800b71b0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800c3500 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800cf850 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800dbba0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800e7ef0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000800f4240 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080100590 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008010c8e0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080118c30 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080124f80 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000801312d0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008013d620 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080149970 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080155cc0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080162010 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008016e360 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008017a6b0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080186a00 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x0000000080192d50 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x000000008019f0a0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000801ab3f0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000801b7740 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000801c3a90 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000801cfde0 higher than p->sz=0x0000000000011000
usertrap(): invalid va=0x00000000801dc130 higher than p->sz=0x0000000000011000
OK
test sbrkfail: usertrap(): kalloc() failed
OK
test sbrkarg: OK
test validatetest: OK
test stacktest: usertrap(): invalid va=0x000000000000fbb0 below the user stack sp=0x0000000000010bb0
OK
test opentest: OK
test writetest: OK
test writebig: OK
test createtest: OK
test openiput: OK
test exitiput: OK
test iput: OK
test mem: usertrap(): kalloc() failed
OK
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
usertrap(): kalloc() failed
ALL TESTS PASSED
```

### 实验中遇到的问题和解决办法

在`vm.c`中引入头文件`proc.h`后启动`make qemu`报错如下：

```shell
kernel/proc.h:87:19: error: field 'lock' has incomplete type
   87 |   struct spinlock lock;
```

报错原因是头文件中重复包含了`struct spinlock lock`的定义，解决方法是在头文件中引入`#include "spinlock.h"`，引入后不再报错。

### 实验心得

​	在本次实验中，我认为最核心的是第4步，也就是：处理进程从`sbrk()`向系统调用（如`read`或`write`）传递有效地址，但尚未分配该地址的内存这种情况。因为我认为这一步最能体现lazy allocation的思想。在这一步中，通过页面错误处理的逻辑，调用 `walkaddr()` 函数，进行延迟分配内存页的操作，实现了对尚未分配内存的虚拟地址进行处理的功能。通过这种方式，xv6 操作系统实现了延迟分配机制，使得虚拟内存的分配是按需进行的，而不是一次性分配全部内存，从而节省了内存资源的消耗。

​	当然，其他的步骤也很重要，这主要是由于lazy allocation造成在某些时候并未给用户的虚拟地址分配映射，所以要忽略掉一些在未使用lazy allocation时造成`panic`的情况。

## 实验评分和提交

运行`make qemu`，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-5/xv6-labs-2020'
== Test running lazytests ==
$ make qemu-gdb
(6.2s)
== Test   lazy: map ==
  lazy: map: OK
== Test   lazy: unmap ==
  lazy: unmap: OK
== Test usertests ==
$ make qemu-gdb
(93.9s)
== Test   usertests: pgbug ==
  usertests: pgbug: OK
== Test   usertests: sbrkbugs ==
  usertests: sbrkbugs: OK
== Test   usertests: argptest ==
  usertests: argptest: OK
== Test   usertests: sbrkmuch ==
  usertests: sbrkmuch: OK
== Test   usertests: sbrkfail ==
  usertests: sbrkfail: OK
== Test   usertests: sbrkarg ==
  usertests: sbrkarg: OK
== Test   usertests: stacktest ==
  usertests: stacktest: OK
== Test   usertests: execout ==
  usertests: execout: OK
== Test   usertests: copyin ==
  usertests: copyin: OK
== Test   usertests: copyout ==
  usertests: copyout: OK
== Test   usertests: copyinstr1 ==
  usertests: copyinstr1: OK
== Test   usertests: copyinstr2 ==
  usertests: copyinstr2: OK
== Test   usertests: copyinstr3 ==
  usertests: copyinstr3: OK
== Test   usertests: rwsbrk ==
  usertests: rwsbrk: OK
== Test   usertests: truncate1 ==
  usertests: truncate1: OK
== Test   usertests: truncate2 ==
  usertests: truncate2: OK
== Test   usertests: truncate3 ==
  usertests: truncate3: OK
== Test   usertests: reparent2 ==
  usertests: reparent2: OK
== Test   usertests: badarg ==
  usertests: badarg: OK
== Test   usertests: reparent ==
  usertests: reparent: OK
== Test   usertests: twochildren ==
  usertests: twochildren: OK
== Test   usertests: forkfork ==
  usertests: forkfork: OK
== Test   usertests: forkforkfork ==
  usertests: forkforkfork: OK
== Test   usertests: createdelete ==
  usertests: createdelete: OK
== Test   usertests: linkunlink ==
  usertests: linkunlink: OK
== Test   usertests: linktest ==
  usertests: linktest: OK
== Test   usertests: unlinkread ==
  usertests: unlinkread: OK
== Test   usertests: concreate ==
  usertests: concreate: OK
== Test   usertests: subdir ==
  usertests: subdir: OK
== Test   usertests: fourfiles ==
  usertests: fourfiles: OK
== Test   usertests: sharedfd ==
  usertests: sharedfd: OK
== Test   usertests: exectest ==
  usertests: exectest: OK
== Test   usertests: bigargtest ==
  usertests: bigargtest: OK
== Test   usertests: bigwrite ==
  usertests: bigwrite: OK
== Test   usertests: bsstest ==
  usertests: bsstest: OK
== Test   usertests: sbrkbasic ==
  usertests: sbrkbasic: OK
== Test   usertests: kernmem ==
  usertests: kernmem: OK
== Test   usertests: validatetest ==
  usertests: validatetest: OK
== Test   usertests: opentest ==
  usertests: opentest: OK
== Test   usertests: writetest ==
  usertests: writetest: OK
== Test   usertests: writebig ==
  usertests: writebig: OK
== Test   usertests: createtest ==
  usertests: createtest: OK
== Test   usertests: openiput ==
  usertests: openiput: OK
== Test   usertests: exitiput ==
  usertests: exitiput: OK
== Test   usertests: iput ==
  usertests: iput: OK
== Test   usertests: mem ==
  usertests: mem: OK
== Test   usertests: pipe1 ==
  usertests: pipe1: OK
== Test   usertests: preempt ==
  usertests: preempt: OK
== Test   usertests: exitwait ==
  usertests: exitwait: OK
== Test   usertests: rmdot ==
  usertests: rmdot: OK
== Test   usertests: fourteen ==
  usertests: fourteen: OK
== Test   usertests: bigfile ==
  usertests: bigfile: OK
== Test   usertests: dirfile ==
  usertests: dirfile: OK
== Test   usertests: iref ==
  usertests: iref: OK
== Test   usertests: forktest ==
  usertests: forktest: OK
== Test time ==
time: OK
Score: 119/119
```

实验通过！
