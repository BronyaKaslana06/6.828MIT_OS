# Lab8 Locks

[TOC]

​	在本实验中，您将获得重新设计代码以提高并行性的经验。多核机器上并行性差的一个常见症状是频繁的锁争用。提高并行性通常涉及更改数据结构和锁定策略以减少争用。您将对xv6内存分配器和块缓存执行此操作。

## 1. Memory allocator ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	程序`user/kalloctest.c`强调了xv6的内存分配器：三个进程增长和缩小地址空间，导致对`kalloc`和`kfree`的多次调用。`kalloc`和`kfree`获得`kmem.lock`。`kalloctest`打印（作为“#fetch-and-add”）在`acquire`中由于尝试获取另一个内核已经持有的锁而进行的循环迭代次数，如`kmem`锁和一些其他锁。`acquire`中的循环迭代次数是锁争用的粗略度量。完成实验前，`kalloctest`的输出与此类似：

```bash
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 83375 #acquire() 433015
lock: bcache: #fetch-and-add 0 #acquire() 1260
--- top 5 contended locks:
lock: kmem: #fetch-and-add 83375 #acquire() 433015
lock: proc: #fetch-and-add 23737 #acquire() 130718
lock: virtio_disk: #fetch-and-add 11159 #acquire() 114
lock: proc: #fetch-and-add 5937 #acquire() 130786
lock: proc: #fetch-and-add 4080 #acquire() 130786
tot= 83375
test1 FAIL
```

​	`acquire`为每个锁维护要获取该锁的`acquire`调用计数，以及`acquire`中循环尝试但未能设置锁的次数。`kalloctest`调用一个系统调用，使内核打印`kmem`和`bcache`锁（这是本实验的重点）以及5个最有具竞争的锁的计数。如果存在锁争用，则`acquire`循环迭代的次数将很大。系统调用返回`kmem`和`bcache`锁的循环迭代次数之和。

​	对于本实验，您必须使用具有多个内核的专用空载机器。

​	`kalloctest`中锁争用的根本原因是`kalloc()`有一个空闲列表，由一个锁保护。要消除锁争用，您必须重新设计内存分配器，以避免使用单个锁和列表。基本思想是为每个CPU维护一个空闲列表，每个列表都有自己的锁。因为每个CPU将在不同的列表上运行，不同CPU上的分配和释放可以并行运行。主要的挑战将是处理一个CPU的空闲列表为空，而另一个CPU的列表有空闲内存的情况；在这种情况下，一个CPU必须“窃取”另一个CPU空闲列表的一部分。窃取可能会引入锁争用，但这种情况希望不会经常发生。

​	您的工作是实现每个CPU的空闲列表，并在CPU的空闲列表为空时进行窃取。所有锁的命名必须以“`kmem`”开头。也就是说，您应该为每个锁调用`initlock`，并传递一个以“`kmem`”开头的名称。运行`kalloctest`以查看您的实现是否减少了锁争用。要检查它是否仍然可以分配所有内存，请运行`usertests sbrkmuch`。

### 实验步骤

本实验的代码修改均在`kernel/kalloc.c`中进行。

1. 使用`kernel/param.h`中的常量`NCPU`修改`kmem`的结构体定义，为多核的每个CPU都分配一份结构体，代码如下：

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];   //lab 8-1 modified
```

2. 修改`kinit`函数，为当前所有的CPU的`freelist`分配所有的空闲空间。

```c 
void
kinit()
{
  for (int i=0; i< NCPU; i++){
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}
```

3. 修改`kfree`函数，注意要先关闭中断再获取CPU的id，然后对一个具体的CPU进行操作。

```c
void
kfree(void *pa)
{
  //...
  //获取cpuid
  //lab 8-1 add
  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}
```

4. 修改`kalloc`函数。`kalloc()`函数是用于在内核中分配内存。在多核的情况下，首先检查当前CPU的`freelist`，如果有空闲内存块，则直接从其中取出一个内存块，并将`freelist`指向下一个内存块，这样就实现了从当前CPU的私有内存池中获取内存的目的；如果当前CPU的`freelist`中没有空闲内存块则需要从其他CPU的`freelist`中获取一个空闲内存块，通过循环遍历所有的CPU（除了当前CPU），尝试从每个CPU的`freelist`中获取一个空闲内存块，直到找到一个非空的`freelist`，然后从其中取出一个内存块，并将相应的`freelist`指向下一个内存块。代码如下：

```c
void *
kalloc(void)
{
  struct run *r;

  //获取cpuid
  //lab 8-1 add
  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  //无空闲内存块
  else {
    for (int i = 0; i < NCPU; i++) {
      if (i == id) 
          continue;
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if(r)
        kmem[i].freelist = r->next;
      release(&kmem[i].lock);
      if(r) 
          break;
    }
  }
  release(&kmem[id].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

```

5. 测试：启动`qemu`后，先后进行`kalloctest`测试和`usertests sbrkmuch`测试，输出如下：

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 113118
lock: kmem: #fetch-and-add 0 #acquire() 147311
lock: kmem: #fetch-and-add 0 #acquire() 172601
lock: bcache: #fetch-and-add 0 #acquire() 334
--- top 5 contended locks:
lock: proc: #fetch-and-add 34145 #acquire() 125050
lock: virtio_disk: #fetch-and-add 6234 #acquire() 57
lock: proc: #fetch-and-add 5401 #acquire() 125091
lock: proc: #fetch-and-add 4280 #acquire() 125089
lock: pr: #fetch-and-add 4176 #acquire() 5
tot= 0
test1 OK
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
$ usertests sbrkmuch
usertests starting
test sbrkmuch: OK
ALL TESTS PASSED
```

测试通过！

### 实验中遇到的问题和解决方法

​	本次实验主要的问题是要理解题意。从长段的描述中提取出有用的信息。比如，为什么在多核情况下会存在锁争用的问题？下面是我在反复阅读并查阅相关资料后得出的信息：

​	在`user/kalloctest.c`文件中，有三个进程会增加和缩小地址空间，从而导致对`kalloc()`和`kfree()`的多次调用。这些函数在执行时会获取`kmem.lock`锁。在`kalloctest`中，使用`acquire`打印在获取另一个内核已经持有的锁时进行的循环迭代次数，其中包括`kmem.lock`和其他一些锁。

​	实验中的目标是消除`kalloc()`中存在的锁争用问题。为了解决这个问题，需要重新设计内存分配器，以避免使用单个锁和列表。具体的设计思路是为每个CPU维护一个独立的空闲列表，每个列表都有自己的锁。由于每个CPU将在不同的列表上运行，因此不同CPU上的分配和释放可以并行运行，从而提高了内存分配的并行性和性能。

​	本次实验主要的挑战在于处理一个CPU的空闲列表为空，而另一个CPU的列表有空闲内存的情况。在这种情况下，一个CPU必须从另一个CPU的空闲列表中“窃取”一部分空闲内存。

### 实验心得

​	本次实验主要学习了操作系统中“锁”的一些概念：

​	在多线程操作中，为了保证数据的一致性，保证临界代码的安全性，操作系统引入了锁机制。通过锁机制，能够保证多核多进程环境下，某一个时间点，只有一个线程进入临界区代码，从而保证临界区中操作数据的一致性。

​	锁的本质是一种同步机制，用于协调并发访问共享资源的行为。在多线程或多进程环境中，多个线程或进程可能同时访问共享资源，导致数据不一致、竞态条件和死锁等问题。为了解决这些问题，锁被引入以保护共享资源的访问和修改。

​	锁的基本思想是在竞争共享资源时，只有一个线程或进程能够获得锁，从而获得对共享资源的独占访问权。其他线程或进程必须等待锁的释放才能访问共享资源。锁的使用可以确保共享资源的一致性和可靠性，同时避免并发冲突和数据损坏。在本次实验中，内存的空闲部分即为需要加锁保护的共享资源。

​	本次实验中，引入了多CPU的专用空闲列表，避免了使用单个全局锁来保护内存分配。通过为每个CPU维护一个私有的空闲列表，不同CPU之间可以并行地分配和释放内存，从而减少了锁的争用，提高了内存分配的效率，是“用小锁换大锁”的思维。

## 2. Buffer cache ([hard](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	如果多个进程密集地使用文件系统，它们可能会争夺`bcache.lock`，它保护`kernel/bio.c`中的磁盘块缓存。`bcachetest`创建多个进程，这些进程重复读取不同的文件，以便在`bcache.lock`上生成争用；在完成本实验之前，其输出如下所示：

```bash
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 33035
lock: bcache: #fetch-and-add 16142 #acquire() 65978
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 162870 #acquire() 1188
lock: proc: #fetch-and-add 51936 #acquire() 73732
lock: bcache: #fetch-and-add 16142 #acquire() 65978
lock: uart: #fetch-and-add 7505 #acquire() 117
lock: proc: #fetch-and-add 6937 #acquire() 73420
tot= 16142
test0: FAIL
start test1
test1 OK

```

​	您可能会看到不同的输出，但`bcache`锁的`acquire`循环迭代次数将很高。如果查看`kernel/bio.c`中的代码，您将看到`bcache.lock`保护已缓存的块缓冲区的列表、每个块缓冲区中的引用计数（`b->refcnt`）以及缓存块的标识（`b->dev`和`b->blockno`）。

​	修改块缓存，以便在运行`bcachetest`时，bcache（buffer cache的缩写）中所有锁的`acquire`循环迭代次数接近于零。理想情况下，块缓存中涉及的所有锁的计数总和应为零，但只要总和小于500就可以。修改`bget`和`brelse`，以便bcache中不同块的并发查找和释放不太可能在锁上发生冲突（例如，不必全部等待`bcache.lock`）。你必须保护每个块最多缓存一个副本的不变量。完成后，您的输出应该与下面显示的类似（尽管不完全相同)。

### 实验步骤

1. 按照提示，将`NBUCKET`改为质数13，将`buffer`分为13个桶：

```c
//param.h
#define NBUCKET      13  // size of disk block cache
```

然后修改`bcache`结构体，将`lock`和`head`改为数组，表示`buffer`中的每一个桶都有`lock`和`head`：

```c
struct {
  struct spinlock biglock;
  struct spinlock lock[NBUCKET];  //lock of each bucket; lab 8-2 add
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET]; // lab 8-2 modified
} bcache;
```

2. 给`kernel/buf.h`的`buf`结构体添加`lastuse`字段，这是一个时间戳，表明这个缓冲区上一次被使用过的时间，方便使用LRU法判定：

```c
struct buf {
  //...
  uchar data[BSIZE];
  uint lastuse; //lab 8-2 add
};
```

3. 修改`kernel/bio.c`的`binit`函数，完成链表的初始化。首先初始化散列桶的锁，然后将所有散列桶的前驱指针和后驱指针都指向自身表示为空，并将所有的缓冲区挂载到`bucket[0]`桶上。

```c
void
binit(void)
{
  struct buf *b;

  //  lab 8-2 modified
  initlock(&bcache.biglock, "bcache");
  for (int i = 0; i < NBUCKET; i++)
    initlock(&bcache.lock[i], "bcache");

  // Create linked list of buffers
  /*bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;*/
  //  initialization lab 8-2 add
  for (int i = 0; i < NBUCKET; i++) {
    bcache.head[i].next = &bcache.head[i];
    bcache.head[i].prev = &bcache.head[i];
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}
```

4. 添加一个散列函数`hash`，通过取模进行散列，方便后续将缓冲区对应到相应的桶中。并在`bunpin`和`bpin`函数中调用`hash`函数

```c
// kernel/bio.c
// hash function; lab 8-2 add
int
hash(int blockno)
{
  return blockno % NBUCKET;
}
void
bpin(struct buf *b) {
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt++;
  release(&bcache.lock[i]);
}

void
bunpin(struct buf *b) {
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt--;
  release(&bcache.lock[i]);
}
```

5. 修改`brelse`函数，使`brelse`不需要获取`bcache`锁：

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  //lab 8-2 modified
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    /*b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;*/
    b->lastuse = ticks;
  }
  release(&bcache.lock[i]);
}
```

6. 修改`bget`函数：`bget` 函数用于获取缓冲区。该函数的目标是在缓冲区中查找给定的设备号和块号，如果找到则返回相应的缓冲区，如果未找到则分配一个新的缓冲区。在查找缓冲区的过程中，可能会出现并发访问的情况，因此需要使用锁来确保多个线程之间的安全访问。

   首先，函数会通过哈希函数找到对应的哈希桶，然后获取该哈希桶对应的锁，保证线程安全。接着，遍历当前哈希桶中的缓冲区链表，尝试查找是否存在匹配的缓冲区。如果找到了匹配的缓冲区，说明缓冲区已经存在于缓冲池中，直接增加引用计数，释放哈希桶锁，然后获取缓冲区锁并返回缓冲区。

   如果未找到匹配的缓冲区，说明需要重新分配一个缓冲区。函数会首先释放当前哈希桶的锁，然后再次遍历当前哈希桶的缓冲区链表，寻找一个空闲的缓冲区。如果找到空闲缓冲区，会将其重新设置为目标设备号和块号，并增加引用计数，然后释放哈希桶锁，获取缓冲区锁，并返回缓冲区。

   如果当前哈希桶没有空闲缓冲区，函数会尝试在其他哈希桶中查找空闲缓冲区。这时，函数会依次遍历其他哈希桶，查找空闲缓冲区。如果找到了空闲缓冲区，会将其从原哈希桶中删除，并重新设置为目标设备号和块号，然后将其添加到目标哈希桶中，并增加引用计数，最后释放哈希桶锁，获取缓冲区锁，并返回缓冲区。

   如果所有哈希桶中都没有空闲缓冲区，函数会释放当前哈希桶的锁，并释放大锁（bcache.biglock），产生 panic，表示没有可用的缓冲区。

7. 修改后的`bget`函数如下：

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *b2 = 0;

  int i = hash(blockno), min_ticks = 0;
  acquire(&bcache.lock[i]);

  // 判断是否匹配
  for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
    //匹配则直接返回
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  //lab 8-2 modified
  release(&bcache.lock[i]);
  acquire(&bcache.biglock);
  acquire(&bcache.lock[i]);

  for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock[i]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    }
  }
 
  for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
    if (b->refcnt == 0 && (b2 == 0 || b->lastuse < min_ticks)) {
      min_ticks = b->lastuse;
      b2 = b;
    }
  }
  if (b2) {
    b2->dev = dev;
    b2->blockno = blockno;
    b2->refcnt++;
    b2->valid = 0;
    //acquiresleep(&b2->lock);
    release(&bcache.lock[i]);
    release(&bcache.biglock);
    acquiresleep(&b2->lock);
    return b2;
  }
  // 从其他桶中查找空闲块
  for (int j = hash(i + 1); j != i; j = hash(j + 1)) {
    acquire(&bcache.lock[j]);
    for (b = bcache.head[j].next; b != &bcache.head[j]; b = b->next) {
      if (b->refcnt == 0 && (b2 == 0 || b->lastuse < min_ticks)) {
        min_ticks = b->lastuse;
        b2 = b;
      }
    }
    if(b2) {
      b2->dev = dev;
      b2->refcnt++;
      b2->valid = 0;
      b2->blockno = blockno;
      // remove
      b2->next->prev = b2->prev;
      b2->prev->next = b2->next;
      release(&bcache.lock[j]);
      // lock
      b2->next = bcache.head[i].next;
      b2->prev = &bcache.head[i];
      bcache.head[i].next->prev = b2;
      bcache.head[i].next = b2;
      release(&bcache.lock[i]);
      release(&bcache.biglock);
      acquiresleep(&b2->lock);
      return b2;
    }
    release(&bcache.lock[j]);
  }
  release(&bcache.lock[i]);
  release(&bcache.biglock);
  panic("bget: no buffers");
}
```

8. 测试：启动`qemu`后，先后进行`bcachetest`测试和`usertests`测试，输出如下，说明测试通过：

```shell
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 32960
lock: kmem: #fetch-and-add 0 #acquire() 17
lock: kmem: #fetch-and-add 0 #acquire() 113
lock: bcache: #fetch-and-add 0 #acquire() 117
lock: bcache: #fetch-and-add 0 #acquire() 2129
lock: bcache: #fetch-and-add 0 #acquire() 4122
lock: bcache: #fetch-and-add 0 #acquire() 2278
lock: bcache: #fetch-and-add 0 #acquire() 4288
lock: bcache: #fetch-and-add 0 #acquire() 4339
lock: bcache: #fetch-and-add 0 #acquire() 6339
lock: bcache: #fetch-and-add 0 #acquire() 6772
lock: bcache: #fetch-and-add 0 #acquire() 6746
lock: bcache: #fetch-and-add 0 #acquire() 8488
lock: bcache: #fetch-and-add 0 #acquire() 6202
lock: bcache: #fetch-and-add 0 #acquire() 6199
lock: bcache: #fetch-and-add 0 #acquire() 4140
lock: bcache: #fetch-and-add 0 #acquire() 4141
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 166657 #acquire() 1284
lock: proc: #fetch-and-add 75699 #acquire() 75833
lock: pr: #fetch-and-add 10582 #acquire() 5
lock: proc: #fetch-and-add 8918 #acquire() 75499
lock: proc: #fetch-and-add 8647 #acquire() 75455
tot= 0
test0: OK
start test1
test1 OK
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
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3240
            sepc=0x00000000000056a4 stval=0x00000000000056a4
usertrap(): unexpected scause 0x000000000000000c pid=3241
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
test kernmem: usertrap(): unexpected scause 0x000000000000000d pid=6221
            sepc=0x000000000000215c stval=0x0000000080000000
usertrap(): unexpected scause 0x000000000000000d pid=6222
            sepc=0x000000000000215c stval=0x000000008000c350
usertrap(): unexpected scause 0x000000000000000d pid=6223
            sepc=0x000000000000215c stval=0x00000000800186a0
usertrap(): unexpected scause 0x000000000000000d pid=6224
            sepc=0x000000000000215c stval=0x00000000800249f0
usertrap(): unexpected scause 0x000000000000000d pid=6225
            sepc=0x000000000000215c stval=0x0000000080030d40
usertrap(): unexpected scause 0x000000000000000d pid=6226
            sepc=0x000000000000215c stval=0x000000008003d090
usertrap(): unexpected scause 0x000000000000000d pid=6227
            sepc=0x000000000000215c stval=0x00000000800493e0
usertrap(): unexpected scause 0x000000000000000d pid=6228
            sepc=0x000000000000215c stval=0x0000000080055730
usertrap(): unexpected scause 0x000000000000000d pid=6229
            sepc=0x000000000000215c stval=0x0000000080061a80
usertrap(): unexpected scause 0x000000000000000d pid=6230
            sepc=0x000000000000215c stval=0x000000008006ddd0
usertrap(): unexpected scause 0x000000000000000d pid=6231
            sepc=0x000000000000215c stval=0x000000008007a120
usertrap(): unexpected scause 0x000000000000000d pid=6232
            sepc=0x000000000000215c stval=0x0000000080086470
usertrap(): unexpected scause 0x000000000000000d pid=6233
            sepc=0x000000000000215c stval=0x00000000800927c0
usertrap(): unexpected scause 0x000000000000000d pid=6234
            sepc=0x000000000000215c stval=0x000000008009eb10
usertrap(): unexpected scause 0x000000000000000d pid=6235
            sepc=0x000000000000215c stval=0x00000000800aae60
usertrap(): unexpected scause 0x000000000000000d pid=6236
            sepc=0x000000000000215c stval=0x00000000800b71b0
usertrap(): unexpected scause 0x000000000000000d pid=6237
            sepc=0x000000000000215c stval=0x00000000800c3500
usertrap(): unexpected scause 0x000000000000000d pid=6238
            sepc=0x000000000000215c stval=0x00000000800cf850
usertrap(): unexpected scause 0x000000000000000d pid=6239
            sepc=0x000000000000215c stval=0x00000000800dbba0
usertrap(): unexpected scause 0x000000000000000d pid=6240
            sepc=0x000000000000215c stval=0x00000000800e7ef0
usertrap(): unexpected scause 0x000000000000000d pid=6241
            sepc=0x000000000000215c stval=0x00000000800f4240
usertrap(): unexpected scause 0x000000000000000d pid=6242
            sepc=0x000000000000215c stval=0x0000000080100590
usertrap(): unexpected scause 0x000000000000000d pid=6243
            sepc=0x000000000000215c stval=0x000000008010c8e0
usertrap(): unexpected scause 0x000000000000000d pid=6244
            sepc=0x000000000000215c stval=0x0000000080118c30
usertrap(): unexpected scause 0x000000000000000d pid=6245
            sepc=0x000000000000215c stval=0x0000000080124f80
usertrap(): unexpected scause 0x000000000000000d pid=6246
            sepc=0x000000000000215c stval=0x00000000801312d0
usertrap(): unexpected scause 0x000000000000000d pid=6247
            sepc=0x000000000000215c stval=0x000000008013d620
usertrap(): unexpected scause 0x000000000000000d pid=6248
            sepc=0x000000000000215c stval=0x0000000080149970
usertrap(): unexpected scause 0x000000000000000d pid=6249
            sepc=0x000000000000215c stval=0x0000000080155cc0
usertrap(): unexpected scause 0x000000000000000d pid=6250
            sepc=0x000000000000215c stval=0x0000000080162010
usertrap(): unexpected scause 0x000000000000000d pid=6251
            sepc=0x000000000000215c stval=0x000000008016e360
usertrap(): unexpected scause 0x000000000000000d pid=6252
            sepc=0x000000000000215c stval=0x000000008017a6b0
usertrap(): unexpected scause 0x000000000000000d pid=6253
            sepc=0x000000000000215c stval=0x0000000080186a00
usertrap(): unexpected scause 0x000000000000000d pid=6254
            sepc=0x000000000000215c stval=0x0000000080192d50
usertrap(): unexpected scause 0x000000000000000d pid=6255
            sepc=0x000000000000215c stval=0x000000008019f0a0
usertrap(): unexpected scause 0x000000000000000d pid=6256
            sepc=0x000000000000215c stval=0x00000000801ab3f0
usertrap(): unexpected scause 0x000000000000000d pid=6257
            sepc=0x000000000000215c stval=0x00000000801b7740
usertrap(): unexpected scause 0x000000000000000d pid=6258
            sepc=0x000000000000215c stval=0x00000000801c3a90
usertrap(): unexpected scause 0x000000000000000d pid=6259
            sepc=0x000000000000215c stval=0x00000000801cfde0
usertrap(): unexpected scause 0x000000000000000d pid=6260
            sepc=0x000000000000215c stval=0x00000000801dc130
OK
test sbrkfail: usertrap(): unexpected scause 0x000000000000000d pid=6272
            sepc=0x00000000000041fc stval=0x0000000000012000
OK
test sbrkarg: OK
test validatetest: OK
test stacktest: usertrap(): unexpected scause 0x000000000000000d pid=6276
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

### 实验中遇到的问题和解决方法

​	`bget`函数中重新分配缓冲区可能会引发的死锁，这在实验的提示中也有提到。

​	在重新分配缓冲区时需要获取两个锁：一个是当前哈希桶的锁，另一个是目标哈希桶的锁。如果线程同时持有了两个哈希桶的锁，且试图申请对方哈希桶的锁，就会出现死锁的情况。

​	为了避免死锁，程序首先会尝试获取当前哈希桶的锁之前，先从自己所属的哈希桶中查找空闲缓冲区。这样做可以减少在不同哈希桶之间频繁地获取锁，从而降低发生死锁的可能性。而当需要从其他同中获取空闲缓冲区时，为了避免这种情况下出现死锁，采取了确定的循环遍历哈希桶的顺序和锁的获取和释放顺序的方法。循环遍历哈希桶的顺序即为在查找空闲缓冲区的过程中，代码使用循环 `for (int j = hash(i + 1); j != i; j = hash(j + 1))` 来遍历其他哈希桶。这样的遍历方式确保了线程在获取哈希桶锁的顺序上是有序的，避免了同时持有两个哈希桶锁的情况。锁的获取和释放顺序即为在每个哈希桶的遍历过程中，对哈希桶的锁的获取和释放是按照特定的顺序进行的。首先，线程会先获取要遍历的哈希桶的锁，然后再进行遍历查找空闲缓冲区。在找到空闲缓冲区并决定将其移到另一个哈希桶之前，线程会释放当前哈希桶的锁。然后，线程再次获取目标哈希桶的锁，将空闲缓冲区移到目标哈希桶，并进行相应的更新。这样的锁获取和释放顺序确保了线程在多个哈希桶之间只持有一个锁，避免了两个线程同时持有两个哈希桶锁的情况。

### 实验心得

本次实验主要探究的操作系统的锁机制在buffer cache中的应用：

1. **buffer cache**：Buffer cache（缓冲区高速缓存）是操作系统中的一种内存管理技术，用于在内存中缓存磁盘数据块，以提高对磁盘的访问速度。磁盘访问通常比内存访问慢得多，因此使用缓冲区高速缓存可以减少对磁盘的实际读写次数，从而加快文件系统和磁盘 I/O 的性能。当应用程序需要从磁盘读取数据或写入数据时，操作系统会首先查找缓冲区高速缓存中是否已经存在相应的数据块。如果数据块在缓冲区中找到了，那么操作系统就可以直接从缓冲区读取数据而不必访问磁盘，从而节省了大量的时间。缓冲区高速缓存的大小是有限的，因此操作系统需要根据某种策略来管理缓冲区中的数据块，实验中采取的就是LRU策略

2. **锁在buffer cache中的作用**：在实验过程中，锁在buffer cache中的作用是确保对缓冲区高速缓存的并发访问是线程安全的。缓冲区高速缓存是多个线程共享的资源，不同的线程可能同时访问同一个缓冲区块，如果没有合适的同步措施，可能会导致数据不一致或其他并发访问问题。

   ​	在buffer cache中有两个主要的锁：一个全局的大锁`bcache.biglock` 和每个桶对应的小锁 `bcache.lock[]`。

   1. `bcache.biglock`：这是全局的大锁，当需要在整个buffer cache中进行一系列操作时，会先获取这个大锁，确保这些操作是原子的，不会被其他线程中断。

   2. `bcache.lock[]`：这是每个哈希桶对应的小锁。当线程需要访问某个特定的桶时，需要先获取这个桶的锁。这样可以确保同一个桶的并发访问是互斥的，避免多个线程同时修改同一个桶。

      在`bget`函数中，就是通过这些锁来保证对缓冲区高速缓存的并发访问的正确性。在查找缓冲区块时，会先获取相应桶的锁，避免不同线程同时访问同一个桶。同时，在查找到缓冲区块时，会先获取`bcache.biglock`大锁，然后再释放相应桶的锁，这样可以保证在整个缓冲区块的获取和处理过程中，其他线程无法插入或删除缓冲区块，确保数据的一致性和完整性。而在遍历桶中的缓冲区块时，由于不需要对整个buffer cache进行操作，只需要获取相应桶的锁即可，从而避免了使用`bcache.biglock`大锁带来的性能开销。



## 实验评分与提交

运行`make grade`，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-8/xv6-labs-2020'
== Test running kalloctest ==
$ make qemu-gdb
(77.3s)
== Test   kalloctest: test1 ==
  kalloctest: test1: OK
== Test   kalloctest: test2 ==
  kalloctest: test2: OK
== Test kalloctest: sbrkmuch ==
$ make qemu-gdb
kalloctest: sbrkmuch: OK (9.3s)
== Test running bcachetest ==
$ make qemu-gdb
(9.0s)
== Test   bcachetest: test0 ==
  bcachetest: test0: OK
== Test   bcachetest: test1 ==
  bcachetest: test1: OK
== Test usertests ==
$ make qemu-gdb
usertests: OK (102.2s)
== Test time ==
time: OK
Score: 70/70
```

说明本次实验通过！上传至远程代码仓库即可。
