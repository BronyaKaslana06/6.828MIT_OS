# Lab 10 mmap

[TOC]

​	`mmap`和`munmap`系统调用允许UNIX程序对其地址空间进行详细控制。它们可用于在进程之间共享内存，将文件映射到进程地址空间，并作为用户级页面错误方案的一部分，如本课程中讨论的垃圾收集算法。在本实验室中，您将把`mmap`和`munmap`添加到xv6中，重点关注内存映射文件（memory-mapped files）。

## Lab: mmap ([hard](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	可以通过多种方式调用`mmap`，但本实验只需要与内存映射文件相关的功能子集。您可以假设`addr`始终为零，这意味着内核应该决定映射文件的虚拟地址。`mmap`返回该地址，如果失败则返回`0xffffffffffffffff`。`length`是要映射的字节数；它可能与文件的长度不同。`prot`指示内存是否应映射为可读、可写，以及/或者可执行的；您可以认为`prot`是`PROT_READ`或`PROT_WRITE`或两者兼有。`flags`要么是`MAP_SHARED`（映射内存的修改应写回文件），要么是`MAP_PRIVATE`（映射内存的修改不应写回文件）。您不必在`flags`中实现任何其他位。`fd`是要映射的文件的打开文件描述符。可以假定`offset`为零（它是要映射的文件的起点）。

​	允许进程映射同一个`MAP_SHARED`文件而不共享物理页面。

​	`	munmap(addr, length)`应删除指定地址范围内的`mmap`映射。如果进程修改了内存并将其映射为`MAP_SHARED`，则应首先将修改写入文件。`munmap`调用可能只覆盖`mmap`区域的一部分，但您可以认为它取消映射的位置要么在区域起始位置，要么在区域结束位置，要么就是整个区域(但不会在区域中间“打洞”)。

​	您应该实现足够的`mmap`和`munmap`功能，以使`mmaptest`测试程序正常工作。如果`mmaptest`不会用到某个`mmap`的特性，则不需要实现该特性。

### 实验步骤

1. 首选需要添加`sys_mmap`和`sys_munmap`两个新的系统调用，需要改动的代码如下：

​		在`kernel/syscall.h`中添加两个新的系统调用的编号

```c
#define SYS_mmap   22   // lab 10 add
#define SYS_munmap 23   // lab 10 add
```

​		在`user/user.h`中添加待完成的系统调用函数定义:

```c
void *mmap(void *, int, int, int, int, int);     // lab 10 add
int munmap(void *, int);                         // lab 10 add		
```

​		在`kernel/syscall.c`中添加引用声明并在`syscalls`数组中引用相关函数。

```c
//lab 10 add
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void); 

static uint64 (*syscalls[])(void) = {
//...
[SYS_mmap]    sys_mmap,     // lab 10 add
[SYS_munmap]  sys_munmap,   // lab 10 add
};
```

​		在`user/usys.pl`中添加系统调用的入口

```perl
entry("mmap");      # lab 10 add
entry("munmap");    # lab 10 add 
```

​		在`Makefile`文件中添加编译链接：

```makefile
	$U/_mmaptest
```

2. 定义虚拟内存区域的结构体和数组

   VMA（Virtual Memory Area）是指用于描述进程的内存映射区域的结构体。在`kernel/proc.h`中定义结构体`vm_area`，包含了内存信息、mmap映射的起始地址、mmap映射内存的大小、用户的权限、mmap标志位、文件的偏移量、文件结构体指针等字段。

   ```c
   //lab 10 add - VMA的定义
   struct vm_area {
       uint64 addr;        // mmap映射的起始地址
       int len;            // mmap映射内存的大小
       int prot;           // 权限
       int flags;          // mmap的标志位
       int offset;         // 文件偏移量
       struct file* f;     //文件指针
   };
   ```

3. 根据提示，每个进程都需要一个VMA的数组来记录它所映射的内存。所以在`kernell/proc.h`的`proc`结构体中，新增了数组`vma`，他的大小设计为`NVMA`，宏定义为16。修改后代码如下：

```c
#define NVMA 16     // vma数组大小
// Per-process state
struct proc {
  //...
  struct vm_area vma[NVMA];    // VMA数组 lab 10 add
};
```

4. 修改`usertrap`。通过读源代码发现，`usertrap`函数中采用了lazy on write的分配方式，在访问文件映射的内存时，需要处理lazy on write机制产生的page default，在mmap机制中，映射的内存是还不存在的，内存的读写执行三种情况都有可能发生，`r_scause()`的值可能是12、13、15。

   根据缺页的地址，通过取整将虚拟地址向下对齐到页面边界，找到对应的VMA结构体，通过遍历当前进程的VMA数组的方式来找到对应的VMA结构体。找到对应的VMA后，使用lazy on write机制分配物理页，然后使用`readi()`函数，从文件中读取相应的内容到这个分配好的物理页面。读取的内容大小为`PGSIZE`，如果文件大小不足一页，则在`readi`函数中会截取相应大小的内容。在进行读取前后，需要对文件的`inode`加锁保护，因为这个操作可能会并发进行。

   读取完毕后，根据VMA的权限信息，设置相应的访问权限，并使用`mappages`函数将物理页映射到用户进程的页面。

```c
//...
else if (r_scause() == 12 || r_scause() == 13 || r_scause() == 15) { 
    //mmap缺页
    char *pa;
    uint64 va = PGROUNDDOWN(r_stval());
    struct vm_area *vma = 0;
    int flags = PTE_U;
    int i;
    // 找到对应的VMA结构
    for (i = 0; i < NVMA; ++i) {
      if (p->vma[i].addr && va >= p->vma[i].addr
          && va < p->vma[i].addr + p->vma[i].len) {
        vma = &p->vma[i];
        break;
      }
    }
    if (!vma) {
      goto err;
    }
    // 设置权限标志
    if (r_scause() == 15 && (vma->prot & PROT_WRITE) && walkaddr(p->pagetable, va)) {
      if (uvmsetdirtywrite(p->pagetable, va)) {
        goto err;
      }
    } 
    else {
      if ((pa = kalloc()) == 0) {
        goto err;
      }
      memset(pa, 0, PGSIZE);
      ilock(vma->f->ip);
      if (readi(vma->f->ip, 0, (uint64) pa, va - vma->addr + vma->offset, PGSIZE) < 0) {
        iunlock(vma->f->ip);
        goto err;
      }
      iunlock(vma->f->ip);
      if ((vma->prot & PROT_READ)) {
        flags |= PTE_R;
      }
      if (r_scause() == 15 && (vma->prot & PROT_WRITE)) {
        flags |= PTE_W | PTE_D;
      }
      if ((vma->prot & PROT_EXEC)) {
        flags |= PTE_X;
      }
      if (mappages(p->pagetable, va, PGSIZE, (uint64) pa, flags) != 0) {
        kfree(pa);
        goto err;
      }
    }
  }
  else if((which_dev = devintr()) != 0){
    // ok
  } 
  else {
err:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
```

5. 实现`sys_mmap`系统调用。`sys_mmap`的作用是在当前进程中创建一个新的VMA，用于映射一个文件到进程的地址空间。思路如下：

​		首先，通过 `argaddr()` 和 `argint()` 从用户空间获取vma结构体中的一系列参数，然后对获取的参数进行合法性检查，包括检查 `flags` 的取值只能是 `MAP_SHARED` 或 `MAP_PRIVATE`，对于 `MAP_SHARED` 映射检查文件是否可写，检查映射长度 `len` 和偏移量 `offset` 的合法性等；然后在vma数组中找到一个未使用的 VMA ，将参数填入该vma的相关字段，并增加文件的引用计数。在设置好 VMA 后，返回起始地址 `addr`，表明映射创建成功。

```c
//kernel/memlayout.h
#define MMAPMINADDR (TRAPFRAME - 10 * PGSIZE)

//kernel/sysfile.c
// lab 10 add
uint64 sys_mmap(void) {
  uint64 addr;
  int len, prot, flags, offset;
  struct file *f;
  struct vm_area *vma = 0;
  struct proc *p = myproc();
  int i;

  // 检查合法性
  if (argaddr(0, &addr) < 0 || argint(1, &len) < 0
      || argint(2, &prot) < 0 || argint(3, &flags) < 0
      || argfd(4, 0, &f) < 0 || argint(5, &offset) < 0) {
    return -1;
  }
  if (flags != MAP_SHARED && flags != MAP_PRIVATE) {
    return -1;
  }
  if (flags == MAP_SHARED && f->writable == 0 && (prot & PROT_WRITE)) {
    return -1;
  }
  if (len < 0 || offset < 0 || offset % PGSIZE) {
    return -1;
  }

  // 找到空的vma并申请
  for (i = 0; i < NVMA; ++i) {
    if (!p->vma[i].addr) {
      vma = &p->vma[i];
      break;
    }
  }
  if (!vma) {
    return -1;
  }

  addr = MMAPMINADDR;
  for (i = 0; i < NVMA; ++i) {
    if (p->vma[i].addr) {
      // get the max address of the mapped memory
      addr = max(addr, p->vma[i].addr + p->vma[i].len);
    }
  }
  addr = PGROUNDUP(addr);
  if (addr + len > TRAPFRAME) {
    return -1;
  }
  // 将对应的字段填入到vma中
  vma->addr = addr;
  vma->len = len;
  vma->prot = prot;
  vma->flags = flags;
  vma->offset = offset;
  vma->f = f;
  filedup(f);     

  return addr;
}
```

6. 实现`munmap`系统调用：`munmap`的作用是取消当前进程的一个VMA，将该区域从进程的地址空间中移除，实现思路和代码如下：

​	首先，通过 `argaddr()` 和 `argint()` 从用户空间获取参数 `addr`（映射的起始地址）和 `len`（映射内存的大小）并对获取的参数进行合法性检查，确保 `addr` 是页对齐的，并且 `len` 大小合法。寻找当前进程的 VMA 数组，找到与给定 `addr` 和 `len` 对应的 VMA 条目。如果找到了对应的 VMA，首先检查`len` 是否为0，如果是0，则表明不用进一步释放，直接返回即可。

​	下面处理真正需要释放的情况：根据 VMA 的标志位 `flags` 来判断是否是 `MAP_SHARED` 类型的映射。对于 `MAP_SHARED` 类型的映射，需要将对应的页面写回到文件。这里采用循环逐个写回页面，一次能写入的最大长度 为`maxsz`。接下来使用 `uvmunmap()` 函数取消内存映射，将该区域从页表中移除。在这里，也会根据地址 `addr` 和 `len` 对 VMA 进行更新。

​	最后，如果 `addr` 等于 VMA 的起始地址 `vma->addr`，并且 `len` 等于 VMA 的长度 `vma->len`，则说明整个 VMA 需要取消，将 VMA 的相关字段设置为0并关闭文件。如果只是 `addr` 等于 VMA 的起始地址 `vma->addr`而`len`不相等，则更新 VMA 的起始地址 `vma->addr` 和偏移量 `vma->offset`，并减去对应的长度 `len`；如果 `addr + len` 等于 VMA 的结束地址 `vma->addr + vma->len`，则减去对应的长度 `len`。

```c
// lab 10 add
// munmap system call
uint64 sys_munmap(void) {
  uint64 addr, va;
  int len;
  struct proc *p = myproc();
  struct vm_area *vma = 0;
  uint maxsz, n, n1;
  int i;
//判断参数合法性
  if (argaddr(0, &addr) < 0 || argint(1, &len) < 0) {
    return -1;
  }
  if (addr % PGSIZE || len < 0) {
    return -1;
  }

  // 寻找对应的VMA
  for (i = 0; i < NVMA; ++i) {
    if (p->vma[i].addr && addr >= p->vma[i].addr
        && addr + len <= p->vma[i].addr + p->vma[i].len) {
      vma = &p->vma[i];
      break;
    }
  }
   //无VMA，直接返回失败
  if (!vma) {
    return -1;
  }

  if (len == 0) {
    return 0;
  }

  if ((vma->flags & MAP_SHARED)) {
    // 一次能写入的最大长度
    maxsz = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    for (va = addr; va < addr + len; va += PGSIZE) {
      if (uvmgetdirty(p->pagetable, va) == 0) {
        continue;
      }
      
      n = min(PGSIZE, addr + len - va);
      for (i = 0; i < n; i += n1) {
        n1 = min(maxsz, n - i);
        begin_op();
        ilock(vma->f->ip);
        if (writei(vma->f->ip, 1, va + i, va - vma->addr + vma->offset + i, n1) != n1) {
          iunlock(vma->f->ip);
          end_op();
          return -1;
        }
        iunlock(vma->f->ip);
        end_op();
      }
    }
  }
  uvmunmap(p->pagetable, addr, (len - 1) / PGSIZE + 1, 1);
  // 更新VMA
  if (addr == vma->addr && len == vma->len) {
    vma->addr = 0;
    vma->len = 0;
    vma->offset = 0;
    vma->flags = 0;
    vma->prot = 0;
    fileclose(vma->f);
    vma->f = 0;
  } else if (addr == vma->addr) {
    vma->addr += len;
    vma->offset += len;
    vma->len -= len;
  } else if (addr + len == vma->addr + vma->len) {
    vma->len -= len;
  } else {
    panic("unexpected munmap");
  }
  return 0;
}
```

7. 修改`uvmunmap`函数，若取消映射的页面没有实际分配物理页，直接跳过即可。

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  //...
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      //panic("uvmunmap: not mapped");
      continue;   // lab 10 modified
    if(PTE_FLAGS(*pte) == PTE_V)
      //panic("uvmunmap: not a leaf");
      continue;   //lab 10 modified
    //...
  }
}
```

8. `munmap`系统调用将映射内存中修改的内容写回文件时，是根据脏页进行写回的，这里需要处理脏页标志位。

​		首先在 `kernel/riscv.h` 中定义脏页标志位 `PTE_D`，然后在 `kernel/vm.c` 中实现`uvmgetdirty()` 和 `uvmsetdirtywrite()` ，分别用于读取脏页标志位和写入脏页标志位和写标志位，并在`defs.h`中声明这两个函数。这两个函数可以用于跟踪页面是否被修改，从而在需要的时候执行写回操作，以保持内存中的数据与磁盘数据的一致性。

```c
//`kernel/riscv.h` 
#define PTE_D (1L << 7) // 脏页标志位

//kernel/vm.c
// lab 10 add
// 取脏页标志位
int uvmgetdirty(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0) {
    return 0;
  }
  return (*pte & PTE_D);
}

// lab 10 add
// 写入脏页标志位
int uvmsetdirtywrite(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0) {
    return -1;
  }
  *pte |= PTE_D | PTE_W;
  return 0;
}
```

9. 修改`exit`函数，该函数负责进程的退出，因此在该函数中需要遍历VMA数组，取消掉所有文件映射内存的映射。思路和源码如下：

   对于每个有效的内存映射，首先根据`MAP_SHARED`标志判断是否是共享映射。如果是共享映射，即多个进程共享同一段物理内存，需要进行写回操作以确保数据的一致性。然后检查该页是否为脏页（`PTE_D`为1），如果是脏页，说明该页内容被修改过。此时需要将脏页的内容写回到对应的文件中，使用`writei`函数将脏页的内容写入到文件的对应偏移位置，数据大小为`min(maxsz, n - i)`字节。接着使用`uvmunmap`函数取消该VMA对应的虚拟内存映射，并释放相应的物理页面。 最后清零`addr`和`len`字段，将该VMA结构设置为无效状态，并关闭与该VMA相关联的文件，减少文件的引用计数。

```c
//kernel/proc.c
void
exit(int status)
{
  //...
  if(p == initproc)
    panic("init exiting");
  //lab 10 add
  // 取消所有映射
  for (i = 0; i < NVMA; ++i) {
    if (p->vma[i].addr == 0) {
      continue;
    }
    vma = &p->vma[i];
    if ((vma->flags & MAP_SHARED)) {
      for (va = vma->addr; va < vma->addr + vma->len; va += PGSIZE) {
        if (uvmgetdirty(p->pagetable, va) == 0) {
          continue;
        }
        n = min(PGSIZE, vma->addr + vma->len - va);
        for (r = 0; r < n; r += n1) {
          n1 = min(maxsz, n - i);
          begin_op();
          ilock(vma->f->ip);
          if (writei(vma->f->ip, 1, va + i, va - vma->addr + vma->offset + i, n1) != n1) {
            iunlock(vma->f->ip);
            end_op();
            panic("exit: writei failed");
          }
          iunlock(vma->f->ip);
          end_op();
        }
      }
    }
    uvmunmap(p->pagetable, vma->addr, (vma->len - 1) / PGSIZE + 1, 1);
    vma->addr = 0;
    vma->len = 0;
    vma->offset = 0;
    vma->flags = 0;
    vma->offset = 0;
    fileclose(vma->f);
    vma->f = 0;
  }
  // Close all open files.
  //...
}
```

10. 修改`fork`系统调用：`fork`函数负责创建子进程，在创建子进程时, 需要将父进程的 VMA 结构体进行拷贝进子进程。此时直接将父进程的 VMA 数组复制到子进程中即可。代码如下:

```c
//kernel/proc.c
int
fork(void)
{
  //...
  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  // lab 10 add
  // 复制父进程所有的VMA
  for (i = 0; i < NVMA; ++i) {
    if (p->vma[i].addr) {
      np->vma[i] = p->vma[i];
      filedup(np->vma[i].f);
    }
  }

  safestrcpy(np->name, p->name, sizeof(p->name));
//...
}
```

11. 测试：进行`mmaptest`测试，输出结果如下，测试通过:

```shell
$ mmaptest
mmap_test starting
test mmap f
test mmap f: OK
test mmap private
test mmap private: OK
test mmap read-only
test mmap read-only: OK
test mmap read/write
test mmap read/write: OK
test mmap dirty
test mmap dirty: OK
test not-mapped unmap
test not-mapped unmap: OK
test mmap two files
test mmap two files: OK
mmap_test: ALL OK
fork_test starting
fork_test OK
mmaptest: all tests succeeded
```

输入`usertests`，输出结果如下，测试通过:

```shell
$ usertests
usertests starting
test manywrites: OK
test execout: OK
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
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3242
            sepc=0x00000000000056a4 stval=0x00000000000056a4
usertrap(): unexpected scause 0x000000000000000c pid=3243
            sepc=0x00000000000056a4 stval=0x00000000000056a4
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
test kernmem: usertrap(): unexpected scause 0x000000000000000d pid=6223
            sepc=0x000000000000215c stval=0x0000000080000000
usertrap(): unexpected scause 0x000000000000000d pid=6224
            sepc=0x000000000000215c stval=0x000000008000c350
usertrap(): unexpected scause 0x000000000000000d pid=6225
            sepc=0x000000000000215c stval=0x00000000800186a0
usertrap(): unexpected scause 0x000000000000000d pid=6226
            sepc=0x000000000000215c stval=0x00000000800249f0
usertrap(): unexpected scause 0x000000000000000d pid=6227
            sepc=0x000000000000215c stval=0x0000000080030d40
usertrap(): unexpected scause 0x000000000000000d pid=6228
            sepc=0x000000000000215c stval=0x000000008003d090
usertrap(): unexpected scause 0x000000000000000d pid=6229
            sepc=0x000000000000215c stval=0x00000000800493e0
usertrap(): unexpected scause 0x000000000000000d pid=6230
            sepc=0x000000000000215c stval=0x0000000080055730
usertrap(): unexpected scause 0x000000000000000d pid=6231
            sepc=0x000000000000215c stval=0x0000000080061a80
usertrap(): unexpected scause 0x000000000000000d pid=6232
            sepc=0x000000000000215c stval=0x000000008006ddd0
usertrap(): unexpected scause 0x000000000000000d pid=6233
            sepc=0x000000000000215c stval=0x000000008007a120
usertrap(): unexpected scause 0x000000000000000d pid=6234
            sepc=0x000000000000215c stval=0x0000000080086470
usertrap(): unexpected scause 0x000000000000000d pid=6235
            sepc=0x000000000000215c stval=0x00000000800927c0
usertrap(): unexpected scause 0x000000000000000d pid=6236
            sepc=0x000000000000215c stval=0x000000008009eb10
usertrap(): unexpected scause 0x000000000000000d pid=6237
            sepc=0x000000000000215c stval=0x00000000800aae60
usertrap(): unexpected scause 0x000000000000000d pid=6238
            sepc=0x000000000000215c stval=0x00000000800b71b0
usertrap(): unexpected scause 0x000000000000000d pid=6239
            sepc=0x000000000000215c stval=0x00000000800c3500
usertrap(): unexpected scause 0x000000000000000d pid=6240
            sepc=0x000000000000215c stval=0x00000000800cf850
usertrap(): unexpected scause 0x000000000000000d pid=6241
            sepc=0x000000000000215c stval=0x00000000800dbba0
usertrap(): unexpected scause 0x000000000000000d pid=6242
            sepc=0x000000000000215c stval=0x00000000800e7ef0
usertrap(): unexpected scause 0x000000000000000d pid=6243
            sepc=0x000000000000215c stval=0x00000000800f4240
usertrap(): unexpected scause 0x000000000000000d pid=6244
            sepc=0x000000000000215c stval=0x0000000080100590
usertrap(): unexpected scause 0x000000000000000d pid=6245
            sepc=0x000000000000215c stval=0x000000008010c8e0
usertrap(): unexpected scause 0x000000000000000d pid=6246
            sepc=0x000000000000215c stval=0x0000000080118c30
usertrap(): unexpected scause 0x000000000000000d pid=6247
            sepc=0x000000000000215c stval=0x0000000080124f80
usertrap(): unexpected scause 0x000000000000000d pid=6248
            sepc=0x000000000000215c stval=0x00000000801312d0
usertrap(): unexpected scause 0x000000000000000d pid=6249
            sepc=0x000000000000215c stval=0x000000008013d620
usertrap(): unexpected scause 0x000000000000000d pid=6250
            sepc=0x000000000000215c stval=0x0000000080149970
usertrap(): unexpected scause 0x000000000000000d pid=6251
            sepc=0x000000000000215c stval=0x0000000080155cc0
usertrap(): unexpected scause 0x000000000000000d pid=6252
            sepc=0x000000000000215c stval=0x0000000080162010
usertrap(): unexpected scause 0x000000000000000d pid=6253
            sepc=0x000000000000215c stval=0x000000008016e360
usertrap(): unexpected scause 0x000000000000000d pid=6254
            sepc=0x000000000000215c stval=0x000000008017a6b0
usertrap(): unexpected scause 0x000000000000000d pid=6255
            sepc=0x000000000000215c stval=0x0000000080186a00
usertrap(): unexpected scause 0x000000000000000d pid=6256
            sepc=0x000000000000215c stval=0x0000000080192d50
usertrap(): unexpected scause 0x000000000000000d pid=6257
            sepc=0x000000000000215c stval=0x000000008019f0a0
usertrap(): unexpected scause 0x000000000000000d pid=6258
            sepc=0x000000000000215c stval=0x00000000801ab3f0
usertrap(): unexpected scause 0x000000000000000d pid=6259
            sepc=0x000000000000215c stval=0x00000000801b7740
usertrap(): unexpected scause 0x000000000000000d pid=6260
            sepc=0x000000000000215c stval=0x00000000801c3a90
usertrap(): unexpected scause 0x000000000000000d pid=6261
            sepc=0x000000000000215c stval=0x00000000801cfde0
usertrap(): unexpected scause 0x000000000000000d pid=6262
            sepc=0x000000000000215c stval=0x00000000801dc130
OK
test sbrkfail: usertrap(): unexpected scause 0x000000000000000d pid=6274
            sepc=0x00000000000041fc stval=0x0000000000012000
OK
test sbrkarg: OK
test validatetest: OK
test stacktest: usertrap(): unexpected scause 0x000000000000000d pid=6278
            sepc=0x00000000000022cc stval=0x000000000000fb90
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



### 实现中遇到的困难和解决方法

启动`mmaptest`后，报错如下：

```shell
$ mmaptest
mmap_test starting
test mmap f
3 mmaptest: unknown sys call 22
mmaptest: mmap_test failed: mmap (1), pid=3
```

这是由于未在`kernel/syscall.c`中的`static uint64 (*syscalls[])`数组中声明系统调用对应的函数所致，在该数组中添加:

```c
[SYS_mmap]    sys_mmap,     // lab 10 add
[SYS_munmap]  sys_munmap,   // lab 10 add
```

问题解决。

**注意引入相关的头文件!**

### 心得体会

本次实验主要探究`mmap`的实现。
	零拷贝技术是操作系统中非常重要的一类技术，而 mmap 则是零拷贝技术的一种实现。下面我们来详细分析一下mmap技术和在这次实验中学到的内容。

**普通读写与mmap对比**
	在unix/linux平台下读写文件，一般有两种方式。第一种是首先open文件，接着使用read系统调用读取文件的全部或一部分。于是内核将文件的内容从磁盘上读取到内核页高速缓冲（即pageCache），再从内核高速缓冲读取到用户进程的地址空间。而写的时候，需要将数据从用户进程拷贝到内核高速缓冲，然后在从内核高速缓冲把数据刷到磁盘中，那么完成一次读写就需要在内核和用户空间之间做四次数据拷贝。而且当多个进程同时读取一个文件时，则每一个进程在自己的地址空间都有这个文件的副本，这样也造成了物理内存的浪费。
	第二种读写方式是使用内存映射的方式。mmap是一种内存映射文件的方法，即将一个文件或者其它对象映射到进程的地址空间，实现文件磁盘地址和进程虚拟地址空间中一段虚拟地址的一一对映关系。实现这样的映射关系后，进程就可以采用指针的方式读写操作这一段内存，而系统会自动回写脏页面到对应的文件磁盘上，即完成了对文件的操作，不必再调用read,write等系统调用函数。相反，内核空间对这段区域的修改也直接反映用户空间，从而可以实现不同进程间的文件共享。如下图所示：

![](https://img-blog.csdnimg.cn/483c92429f1540d09742501ec107039f.png#pic_center)

​	由上图可以看出，进程的虚拟地址空间，由多个虚拟内存区域构成。虚拟内存区域是进程的虚拟地址空间中的一个同质区间，即具有同样特性的连续地址范围。上图中所示的text数据段（代码段）、初始数据段、BSS数据段、堆、栈和内存映射，都是一个独立的虚拟内存区域。而为内存映射服务的地址空间处在堆栈之间的空余部分。

​	所以说，`mmap()`系统调用使得进程之间通过映射同一个普通文件实现共享内存。普通文件被映射到进程地址空间后，进程可以向访问普通内存一样对文件进行访问，不必再调用`read()`，`write()`等操作。

​	两种方式相比，使用`mmap`方式获取磁盘上的文件信息，只需要将磁盘上的数据拷贝至共享内存中，用户进程就可以直接获取到信息。而普通读写方式则必须先把数据从磁盘拷贝至到pageCache中，然后再把数据拷贝至用户进程中，两者相比，mmap方式会少一次数据拷贝的操作，带来巨大的性能提升。

## 实验评分与提交

运行`make grade`后，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-10/xv6-labs-2020'
== Test running mmaptest ==
$ make qemu-gdb
(5.6s)
== Test   mmaptest: mmap f ==
  mmaptest: mmap f: OK
== Test   mmaptest: mmap private ==
  mmaptest: mmap private: OK
== Test   mmaptest: mmap read-only ==
  mmaptest: mmap read-only: OK
== Test   mmaptest: mmap read/write ==
  mmaptest: mmap read/write: OK
== Test   mmaptest: mmap dirty ==
  mmaptest: mmap dirty: OK
== Test   mmaptest: not-mapped unmap ==
  mmaptest: not-mapped unmap: OK
== Test   mmaptest: two files ==
  mmaptest: two files: OK
== Test   mmaptest: fork_test ==
  mmaptest: fork_test: OK
== Test usertests ==
$ make qemu-gdb
usertests: OK (81.8s)
== Test time ==
time: OK
Score: 140/140
```

实验成功，直接提交远程代码仓库即可。
