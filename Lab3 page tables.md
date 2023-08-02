# Lab 3 page tables

[TOC]

​	本实验将探索页表并对其进行修改，以简化将数据从用户空间复制到内核空间的函数。

## 1. Print a page table ([easy](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	为了帮助了解RISC-V页表和将来的调试，您的第一个任务是编写一个打印页表内容的函数。

​	定义一个名为`vmprint()`的函数。它应当接收一个`pagetable_t`作为参数，并以下面描述的格式打印该页表。在`exec.c`中的`return argc`之前插入`if(p->pid==1) vmprint(p->pagetable)`，以打印第一个进程的页表。

​	当启动xv6时，它应该像这样打印输出来描述第一个进程刚刚完成`exec()`时的页表：

```shell
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
.. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
.. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
.. .. ..510: pte 0x0000000021fdd807 pa 0x0000000087f76000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

### 实验过程

1. 在`kernel/defs.h`中定义函数原型并在`kernel/exec.c`调用：

   在`exec.c`中的`return argc`之前插入`if(p->pid==1) vmprint(p->pagetable)`

```c
//kernel/def.h
//...
// exec.c
int             exec(char*, char**);
void            vmprint(pagetable_t);
//...

//kernel/exec.c
//...
if(p->pid==1)
    vmprint(p->pagetable);
return argc;
```

2. 研究`kernel/vm.c`中的`freewalk`函数，在`freewalk`函数中，首先会遍历整个`pagetable`，当遍历到一个有效的页表项且该页表项不是最后一层时，会`freewalk`函数会递归调用。`freewalk`函数通过`(pte & PTE_V)`判断页表是否有效，`(pte & (PTE_R|PTE_W|PTE_X)) == 0`判断是否在不在最后一层。参考`freewalk`函数，我们可以在`kernel/vm.c`中写出`vmprint`函数的递归实现，代码如下：

```c
void
vmprint_rec(pagetable_t pagetable, int level){
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // PTE_V is a flag for whether the page table is valid
    if(pte & PTE_V){
      for (int j = 0; j < level; j++) {
        if (j) 
          printf(" ");
        printf("..");
      }
      uint64 child = PTE2PA(pte);
      printf("%d: pte %p pa %p\n", i, pte, child);
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
        // this PTE points to a lower-level page table.
        vmprint_rec((pagetable_t)child, level + 1);
      }
    }
  }
}

void
vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  vmprint_rec(pagetable, 1);
}
```

​	上述代码中，`vmprint_rec`是递归函数，`level`传入层级来根据层级数量输出".."，`vmprint`是真正的输出函数，通过调用递归函数`vmprint_rec`实现。

3. 测试

输入`make qemu`，发现有如下输出结果：

```shell
xv6 kernel is booting

hart 2 starting
hart 1 starting
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
.. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
.. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
.. .. ..510: pte 0x0000000021fdd807 pa 0x0000000087f76000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
init: starting sh
```

说明个人测试成功，下面进行单元测试，输出如下结果：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-2020-3/xv6-labs-2020$ ./grade-lab-pgtbl pte print
make: 'kernel/kernel' is up to date.
== Test pte printout == pte printout: OK (0.9s)
```

说明单元测试成功。

### 实验中遇到的问题和解决办法

 本次实验遇到的问题和实验本身需要的知识关系不大，目前推测和自己做的上一个`syscall`的实验有关系，但也无法找到确切的原因，问题描述如下：

​	当我切换到本次实验需要分支`pgtbl`并运行`make qemu`时，发现该命令报错：

```shell
make: *** No rule to make target 'kernel/sysinfo.h', needed by 'kernel/sysproc.o'.  Stop.
```

​	这就奇怪了，报错中提到的`kernel/sysinfo.h`和`kernel/sysproc.o`都和我做的上一个实验有关系，但是此时我已经切换到了分支`pgtbl`中，按照我的理解，在上一个实验分支`syscall`中我所做的修改不应该影响当前的分支。并且我也在当前的分支`pgtbl`中仔细检查过，根本不存在`kernel/sysinfo.h`这个文件，在本地没有合并任何分支的状态下，`Makefile`文件也不可能和上一个分支的相同。我尝试`git`的回退版本操作，再运行`make qemu`，但仍然报相同的错误。

​	最后实在没有办法，我新建了一个文件夹，重新克隆了一份原始代码仓库，切换到`pgtbl`分支，问题解决。

### 实验心得

通过本次实验，我加深了对页表的理解，并学到了下面的新知识：

1. `exec`:exec是创建地址空间的用户部分的系统调用。它使用一个存储在文件系统中的文件初始化地址空间的用户部分。系统调用 `exec` 将从某个文件（通常是可执行文件）里读取内存镜像，并将其替换到调用它的进程的内存空间。
1. 当进程向xv6请求更多的用户内存时，xv6首先使用`kalloc`来分配物理页面。然后，它将PTE添加到进程的页表中，指向新的物理页面。Xv6在这些PTE中设置`PTE_W`、`PTE_X`、`PTE_R`、`PTE_U`和`PTE_V`标志。大多数进程不使用整个用户地址空间；xv6在未使用的PTE中留空`PTE_V`。在 xv6 操作系统中，`PTE` 是页表条目（Page Table Entry）的缩写。页表条目是用于虚拟内存管理的数据结构，在操作系统中用于映射虚拟地址和物理地址之间的关系。

## 2.A kernel page table per process ([hard](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	Xv6有一个单独的用于在内核中执行程序时的内核页表。内核页表直接映射（恒等映射）到物理地址，也就是说内核虚拟地址`x`映射到物理地址仍然是`x`。Xv6还为每个进程的用户地址空间提供了一个单独的页表，只包含该进程用户内存的映射，从虚拟地址0开始。因为内核页表不包含这些映射，所以用户地址在内核中无效。因此，当内核需要使用在系统调用中传递的用户指针（例如，传递给`write()`的缓冲区指针）时，内核必须首先将指针转换为物理地址。本实验和下一个实验的目标是允许内核直接解引用用户指针。	

​	修改内核来让每一个进程在内核中执行时使用它自己的内核页表的副本。修改`struct proc`来为每一个进程维护一个内核页表，修改调度程序使得切换进程时也切换内核页表。对于这个步骤，每个进程的内核页表都应当与现有的的全局内核页表完全一致。

​	在未修改xv6之前，用户通过系统调用进入内核，这时如果传入用户的指针，会导致因为找不到相应指向的物理地址而失败，因为在虚拟地址到物理地址的转换中，我们使用的是内核kernel pagetable而不是用户自定义的usr pagetable，所以我们要做的是构造一个usr-kernel-pagetable在用户进入内核时使用，将我们的虚拟地址转换为正确的物理地址。

### 实验过程

1. 按照提示，先在`kernel/proc.h `的`struct proc` 结构体的中添加`kernalpagetable`即内核页表字段：

```c
struct proc {
  //...
  pagetable_t pagetable;       // User page table
  pagetable_t kernelpagetable;       // kernel page table
  //...
};
```

2. 初始化内核页表`kernelpagetable`。可以模仿`kernel/vm.c`里的`kvminit`函数，用内核自己`pagetable `初始化的方式初始化用户进程的内核页表，模仿`kvmmap()`函数在`kernel/vm.c`新增`uvmmap()`函数为用户进程的内核页表添加映射，方便初始化内核页表`uvmKernelInit`函数的编写。

```c
void uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm) {
    if(mappages(pagetable, va, sz, pa, perm) != 0) {
        panic("uvmmap");
    }
}

pagetable_t uvmKernelInit(struct proc *p) {
    pagetable_t kpagetable = uvmcreate();
    if(kpagetable == 0){
        return 0;
    }

    uvmmap(kpagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
    uvmmap(kpagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
    uvmmap(kpagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
    uvmmap(kpagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
    uvmmap(kpagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
    uvmmap(kpagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
    uvmmap(kpagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);    // 注意va为TRAMPOLINE

    return kpagetable;
}
```

3. 在`defs.h`中声明`uvmKernelInit()`后在`kernel/proc.c`中的`allocproc`中调用`uvmKernelInit()`函数以实现用户进程内核页表的初始化。

```c
//kernel/defs.h
// vm.c
//...
pagetable_t     uvmKernelInit(struct proc *p)
    
//kernel/proc.c
static struct proc*
allocproc(void)
{
  //...
  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  //add code to call user kernel pagetable
  p->kernelpagetable = uvmKernelInit(p);
  if (p->kernelpagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  } 
  //...
}
```

3. 提示要求每个内核页表中都添加一个对自己内核栈的映射，未修改前，内核栈初始化在`kernel/proc.c`的`procinit`函数中，将`stack`初始化的部分移入`kernel/proc.c`的`allocproc`函数中，完成对`allocproc`函数的修改。

```c
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");

      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.
      //char *pa = kalloc();
      //if(pa == 0)
      //  panic("kalloc");
      //uint64 va = KSTACK((int) (p - proc));
      //kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      //p->kstack = va;
  }
  kvminithart();
}

static struct proc*
allocproc(void)
{
//...
// Allocate a page for the process's kernel stack
  char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;
  // mapping virtual address and physical address
  uvmmap(p->kernelpagetable,va, (uint64)pa,PGSIZE,PTE_R | PTE_W);
  p->kstack = va;


  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
//... 
```

4. 参考`kvminithart`函数，修改`proc.c`里的`scheduler`函数，在进程进行切换的时候把自己的内核页表放入到`stap`寄存器里，让内核使用该进程的页表转换地址，如果没有进程在运行，就使用内核自己的内核页表。`stap`寄存器负责管理内核页面。修改后的`scheduler`代码如下：

```c
void
scheduler(void)
{
  //...
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        // load the process's kernel page table - lab3-2
        w_satp(MAKE_SATP(p->kernelpagetable));
		// flush TLB
        sfence_vma();
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;

        found = 1;
      }
      release(&p->lock);
    }
#if !defined (LAB_FS)
    if(found == 0) {
      intr_on();
      asm volatile("wfi");
    }
#else
    ;
#endif
  }
}
```

5. 进程销毁部分添加对内核页表的释放，由于之前将`kernel stack`的初始化移入了`allocproc`函数中，所以先要对kernel stack进行查找释放，代码如下：

```c
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  // free kernel stack
  if (p->kstack)
  {
    pte_t* pte = walk(p->kernelpagetable, p->kstack, 0);
    if (pte == 0)
      panic("freeproc: kstack");
    kfree((void*)PTE2PA(*pte));
  }
  p->kstack = 0;
  //...
```

然后需要释放用户进程的内核页表：

```c
static void
freeproc(struct proc *p)
{
  //...
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  // free kernel pagetable
  if (p->kernelpagetable)
    proc_freeKernelPageTable(p->kernelpagetable);
  //...
}
```

6. 需要保证释放页表的时候而无需同时释放叶节点的物理内存页，所以需要添加一个`proc_freeKernelPageTable`特殊处理内存页表的释放以保证这一点。该函数代码如下：

```c
void 
proc_freeKernelPageTable(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V)){
      pagetable[i] = 0;
      if ((pte & (PTE_R|PTE_W|PTE_X)) == 0)
      {
        uint64 child = PTE2PA(pte);
        proc_freeKernelPageTable((pagetable_t)child);
      }
    } else if(pte & PTE_V){
      panic("proc free kpt: leaf");
    }
  }
  kfree((void*)pagetable);
}
```

​	最后根据启动`make qemu`时产生的报错将不在`defs.h`中声明过的函数原型添加进去，此时启动`make qemu`输出如下：

```shell
xv6 kernel is booting

hart 2 starting
hart 1 starting
panic: kvmpa
```

​	这是由于` virtio` 磁盘驱动 `virtio_disk.c` 中的`virtio_disk_rw()`函数调用了 `kvmpa()` 用于将虚拟地址转换为物理地址，此时需要换为当前进程的内核页表。所以我们需要将 `kvmpa()` 的参数增加一个 `pagetable_t`, 在 `virtio_disk_rw()`函数调用时传入 `myproc()->kernelpagetable`.为了保证`virtio_disk.c`在调用时不报错，需要给`virtio_disk.c`添加头文件引用`#include "proc.h"`。修改后的`kvmpa()`函数如下：

```c
uint64
kvmpa(pagetable_t kernelPageTable, uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  pte = walk(kernelPageTable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}
```

​	再次键入`make qemu`发现可以正常启动。

7. 测试，需要花费较长时间，启动`qemu`后运行`usertest`，输出结果如下，说明正确：

```shell
init: starting sh
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
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3234
            sepc=0x0000000000005406 stval=0x0000000000005406
usertrap(): unexpected scause 0x000000000000000c pid=3235
            sepc=0x0000000000005406 stval=0x0000000000005406
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
test exectest: OK
test bigargtest: OK
test bigwrite: OK
test bsstest: OK
test sbrkbasic: OK
test sbrkmuch: OK
test kernmem: usertrap(): unexpected scause 0x000000000000000d pid=6214
            sepc=0x000000000000201a stval=0x0000000080000000
usertrap(): unexpected scause 0x000000000000000d pid=6215
            sepc=0x000000000000201a stval=0x000000008000c350
usertrap(): unexpected scause 0x000000000000000d pid=6216
            sepc=0x000000000000201a stval=0x00000000800186a0
usertrap(): unexpected scause 0x000000000000000d pid=6217
            sepc=0x000000000000201a stval=0x00000000800249f0
usertrap(): unexpected scause 0x000000000000000d pid=6218
            sepc=0x000000000000201a stval=0x0000000080030d40
usertrap(): unexpected scause 0x000000000000000d pid=6219
            sepc=0x000000000000201a stval=0x000000008003d090
usertrap(): unexpected scause 0x000000000000000d pid=6220
            sepc=0x000000000000201a stval=0x00000000800493e0
usertrap(): unexpected scause 0x000000000000000d pid=6221
            sepc=0x000000000000201a stval=0x0000000080055730
usertrap(): unexpected scause 0x000000000000000d pid=6222
            sepc=0x000000000000201a stval=0x0000000080061a80
usertrap(): unexpected scause 0x000000000000000d pid=6223
            sepc=0x000000000000201a stval=0x000000008006ddd0
usertrap(): unexpected scause 0x000000000000000d pid=6224
            sepc=0x000000000000201a stval=0x000000008007a120
usertrap(): unexpected scause 0x000000000000000d pid=6225
            sepc=0x000000000000201a stval=0x0000000080086470
usertrap(): unexpected scause 0x000000000000000d pid=6226
            sepc=0x000000000000201a stval=0x00000000800927c0
usertrap(): unexpected scause 0x000000000000000d pid=6227
            sepc=0x000000000000201a stval=0x000000008009eb10
usertrap(): unexpected scause 0x000000000000000d pid=6228
            sepc=0x000000000000201a stval=0x00000000800aae60
usertrap(): unexpected scause 0x000000000000000d pid=6229
            sepc=0x000000000000201a stval=0x00000000800b71b0
usertrap(): unexpected scause 0x000000000000000d pid=6230
            sepc=0x000000000000201a stval=0x00000000800c3500
usertrap(): unexpected scause 0x000000000000000d pid=6231
            sepc=0x000000000000201a stval=0x00000000800cf850
usertrap(): unexpected scause 0x000000000000000d pid=6232
            sepc=0x000000000000201a stval=0x00000000800dbba0
usertrap(): unexpected scause 0x000000000000000d pid=6233
            sepc=0x000000000000201a stval=0x00000000800e7ef0
usertrap(): unexpected scause 0x000000000000000d pid=6234
            sepc=0x000000000000201a stval=0x00000000800f4240
usertrap(): unexpected scause 0x000000000000000d pid=6235
            sepc=0x000000000000201a stval=0x0000000080100590
usertrap(): unexpected scause 0x000000000000000d pid=6236
            sepc=0x000000000000201a stval=0x000000008010c8e0
usertrap(): unexpected scause 0x000000000000000d pid=6237
            sepc=0x000000000000201a stval=0x0000000080118c30
usertrap(): unexpected scause 0x000000000000000d pid=6238
            sepc=0x000000000000201a stval=0x0000000080124f80
usertrap(): unexpected scause 0x000000000000000d pid=6239
            sepc=0x000000000000201a stval=0x00000000801312d0
usertrap(): unexpected scause 0x000000000000000d pid=6240
            sepc=0x000000000000201a stval=0x000000008013d620
usertrap(): unexpected scause 0x000000000000000d pid=6241
            sepc=0x000000000000201a stval=0x0000000080149970
usertrap(): unexpected scause 0x000000000000000d pid=6242
            sepc=0x000000000000201a stval=0x0000000080155cc0
usertrap(): unexpected scause 0x000000000000000d pid=6243
            sepc=0x000000000000201a stval=0x0000000080162010
usertrap(): unexpected scause 0x000000000000000d pid=6244
            sepc=0x000000000000201a stval=0x000000008016e360
usertrap(): unexpected scause 0x000000000000000d pid=6245
            sepc=0x000000000000201a stval=0x000000008017a6b0
usertrap(): unexpected scause 0x000000000000000d pid=6246
            sepc=0x000000000000201a stval=0x0000000080186a00
usertrap(): unexpected scause 0x000000000000000d pid=6247
            sepc=0x000000000000201a stval=0x0000000080192d50
usertrap(): unexpected scause 0x000000000000000d pid=6248
            sepc=0x000000000000201a stval=0x000000008019f0a0
usertrap(): unexpected scause 0x000000000000000d pid=6249
            sepc=0x000000000000201a stval=0x00000000801ab3f0
usertrap(): unexpected scause 0x000000000000000d pid=6250
            sepc=0x000000000000201a stval=0x00000000801b7740
usertrap(): unexpected scause 0x000000000000000d pid=6251
            sepc=0x000000000000201a stval=0x00000000801c3a90
usertrap(): unexpected scause 0x000000000000000d pid=6252
            sepc=0x000000000000201a stval=0x00000000801cfde0
usertrap(): unexpected scause 0x000000000000000d pid=6253
            sepc=0x000000000000201a stval=0x00000000801dc130
OK
test sbrkfail: usertrap(): unexpected scause 0x000000000000000d pid=6265
            sepc=0x0000000000003e7a stval=0x0000000000012000
OK
test sbrkarg: OK
test validatetest: OK
test stacktest: usertrap(): unexpected scause 0x000000000000000d pid=6269
            sepc=0x0000000000002188 stval=0x000000000000fbc0
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



### 实验中遇到的问题和解决办法

​	该实验步骤繁琐，要注意的点比较多，需要重新学习的知识也很多，在具体的编程过程中遇到的问题主要是在最后一步修改`kvmpa`函数后报错如下：

```c
kernel/virtio_disk.c: In function 'virtio_disk_rw':
kernel/virtio_disk.c:206:51: error: dereferencing pointer to incomplete type 'struct proc'
  206 |   disk.desc[idx[0]].addr = (uint64) kvmpa(myproc()->kernelpagetable, (uint64) &buf0);
      |                                                   ^~
make: *** [<builtin>: kernel/virtio_disk.o] Error 1
```

​	这是由于`virtio_disk.c`文件在编译时无法找到`struct proc`结构体所致，该结构体定义在`proc.h`文件中，于是在`virtio_disk.c`中添加头文件引用`proc.h`即可。

​	内核栈页面：每个进程都有自己的内核栈，它将映射到偏高一些的地址，这样xv6在它之下就可以留下一个未映射的保护页(guard page)。保护页的PTE是无效的（也就是说`PTE_V`没有设置），所以如果内核溢出内核栈就会引发一个异常，内核触发`panic`。如果没有保护页，栈溢出将会覆盖其他内核内存，引发错误操作。panic crash是更可取的方案。

### 实验心得

**内核地址空间**：Xv6为每个进程维护一个页表，用以描述每个进程的用户地址空间，外加一个单独描述内核地址空间的页表。内核配置其地址空间的布局，以允许自己以可预测的虚拟地址访问物理内存和各种硬件资源。下图显示了这种布局如何将内核虚拟地址映射到物理地址。文件`kernel/memlayout.h`声明了xv6内核内存布局的常量。

![](http://xv6.dgs.zone/tranlate_books/book-riscv-rev1/images/c3/p3.png)

**`kvmpa`函数**：

​		在 xv6 操作系统中，`kvmpa` 函数用于将内核虚拟地址转换为物理地址。它是用于内核级别的虚拟地址到物理地址的映射转换。该函数的作用是将给定的虚拟地址 `va` 转换为内核页表 `kernelPageTable` 中对应的物理地址。修改后`kvmpa`函数的工作流程如下：

函数的工作流程如下：

1. 计算虚拟地址 `va` 在页面内的偏移量 `off`（即 `va % PGSIZE`）。
2. 调用 `walk` 函数，根据给定的内核页表 `kernelPageTable` 和虚拟地址 `va` 找到对应的页表条目（`pte`）。
3. 检查页表条目是否存在，如果为 `NULL`，即找不到对应的页表条目，触发 panic。
4. 检查页表条目是否有效（`PTE_V` 标志位是否被设置），如果无效，也触发 panic。
5. 将页表条目转换为物理地址（通过 `PTE2PA` 宏），并保存在变量 `pa` 中。
6. 将物理地址 `pa` 和页面内偏移量 `off` 相加，得到最终的物理地址，并将其作为函数的返回值。

修改后的`kvmpa`函数原码见上文。

## 3. Simplify `copyin/copyinstr` ([hard](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	内核的`copyin`函数读取用户指针指向的内存。它通过将用户指针转换为内核可以直接解引用的物理地址来实现这一点。这个转换是通过在软件中遍历进程页表来执行的。在本部分的实验中，您的工作是将用户空间的映射添加到每个进程的内核页表（上一实验中创建），以允许`copyin`（和相关的字符串函数`copyinstr`）直接解引用用户指针。	

​	将定义在`kernel/vm.c`中的`copyin`的主题内容替换为对`copyin_new`的调用（在`kernel/vmcopyin.c`中定义）；对`copyinstr`和`copyinstr_new`执行相同的操作。为每个进程的内核页表添加用户地址映射，以便`copyin_new`和`copyinstr_new`工作。

​	即：在`kernel/vm.c`中有一个`copyin`函数，该函数读入一个用户空间的指针，通过用户的页表转为内核可以识别的物理地址，然后再交给内核页表转换为物理地址，然后这里实验要求我们将用户页表直接复制一份到我们之前构造的`kernelpagetable`中，那从虚拟地址到物理地址只需要一步转换。

​	此方案依赖于用户的虚拟地址范围不与内核用于自身指令和数据的虚拟地址范围重叠。Xv6使用从零开始的虚拟地址作为用户地址空间，幸运的是内核的内存从更高的地址开始。然而，这个方案将用户进程的最大大小限制为小于内核的最低虚拟地址。内核启动后，在XV6中该地址是`0xC000000`，即PLIC寄存器的地址；您需要修改xv6，以防止用户进程增长到超过PLIC的地址。

### 实验过程

1. 通过调用定义在`kernel/vmcopyin.c`中的函数`copyin_new`和`copyinstr_new`来替换`copyin`和`copyinstr`函数，并在`defs.h`中添加`vmcopyin.c`文件中被用到的函数的声明，修改的代码如下：

```c
//defs.h
//vmcopyin.c
int             copyin_new(pagetable_t,char *,uint64,uint64);
int             copyinstr_new(pagetable_t,char *,uint64,uint64);

//vm.c
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable,dst,srcva,len);
}
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable,dst,srcva,max);
}
```

2. 在`vm.c`中添加`vmcopypage()`函数，并将该函数声明添加到`defs.h`中。这个函数将进程中用户页表复制到内核页表中，代码如下：

```c
void
vmcopypage(pagetable_t pagetable,pagetable_t kpagetable,uint64 start,uint64 sz){
    for(uint64 i=start;i<start+sz;i+=PGSIZE){
        pte_t* pte=walk(pagetable,i,0);
        pte_t* kpte=walk(kpagetable,i,1);
        if(!pte||!kpte){
            panic("vmcopypage");
        }
        *kpte=(*pte)&~(PTE_U|PTE_W|PTE_X);
    }
}
```

3. 在`sysproc.c`修改和`fork()`，`sbrk()`，`exec()`有关的函数的内容，初次之外，还需要修改`userinit`函数和`exec.c`中的`exec`函数，在他们中调用`vmcopypage`函数，将进程页表复制到进程的内核页表中，修改后的代码如下：

```c
//proc.c
fork(void)
{
    //...
  np->sz = p->sz;
  vmcopypage(np->pagetable,np->kernelpagetable,0,np->sz);   //add code
  np->parent = p;
    //...
}

//exec.c
int
exec(char *path, char **argv)
{
  //..
  //add code, restrict PLIC
  if(sz>=PLIC)
    goto bad;
  //...
  //release old process pagetable map
  uvmunmap(p->kernelpagetable,0,PGROUNDUP(oldsz)/PGSIZE,0);
  //copy process pagetable to kerneal pagetable
  vmcopypage(pagetable,p->kernelpagetable,0,sz);  
  //...
}

//sysproc.c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p=myproc();
  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;
  if(growproc(n) < 0)
    return -1;
  if(n>0){
    vmcopypage(p->pagetable,p->kpagetable,addr,n);
  }else{
      for(int j=addr-PGSIZE;j>=addr+n;j-=PGSIZE){
          uvmunmap(p->kpagetable,j,1,0);
      }
  }
  return addr;
}
```

3. 测试：触发了`panic: kerneltrap`，出错，但不知道什么问题，报错如下：

```shell
...
test exectest: OK
test bigargtest: scause 0x000000000000000d
sepc=0x00000000800065aa stval=0x0000000000006778
panic: kerneltrap
```





## 提交



```shell
== Test pte printout ==
$ make qemu-gdb
pte printout: OK (5.5s)
== Test answers-pgtbl.txt == answers-pgtbl.txt: FAIL
    Cannot read answers-pgtbl.txt
== Test count copyin ==
$ make qemu-gdb
Timeout! count copyin: FAIL (150.3s)
    got:
      1
    expected:
      2
    QEMU output saved to xv6.out.count
== Test usertests ==
$ make qemu-gdb
Timeout! (300.3s)
== Test   usertests: copyin ==
  usertests: copyin: OK
== Test   usertests: copyinstr1 ==
  usertests: copyinstr1: OK
== Test   usertests: copyinstr2 ==
  usertests: copyinstr2: OK
== Test   usertests: copyinstr3 ==
  usertests: copyinstr3: OK
== Test   usertests: sbrkmuch ==
  usertests: sbrkmuch: FAIL
    Failed sbrkmuch
== Test   usertests: all tests ==
  usertests: all tests: FAIL
    ...
         test exectest: OK
         test bigargtest: scause 0x000000000000000d
         sepc=0x00000000800065aa stval=0x0000000000006778
         panic: kerneltrap
         qemu-system-riscv64: terminating on signal 15 from pid 910 (make)
    MISSING '^ALL TESTS PASSED$'
== Test time ==
time: OK
Score: 31/66
make: *** [Makefile:316: grade] Error 1
```

