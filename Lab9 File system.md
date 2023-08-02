# Lab9 File system

[TOC]

在本实验中，您将向xv6文件系统添加大型文件和符号链接。

## Large files ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	在本实验中，您将增加xv6文件的最大大小。目前，xv6文件限制为268个块或`268*BSIZE`字节（xv6中`BSIZE`为1024）。此限制来自以下事实：一个xv6 inode包含12个“直接”块号和一个“间接”块号，“一级间接”块指一个最多可容纳256个块号的块，总共12+256=268个块。预备

​	`mkfs`程序创建xv6文件系统磁盘映像，并确定文件系统的总块数；此大小由***kernel/param.h\***中的`FSSIZE`控制。您将看到，该实验室存储库中的`FSSIZE`设置为200000个块。您应该在`make`输出中看到来自`mkfs/mkfs`的以下输出：

```
nmeta 70 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 25) blocks 199930 total 200000
```

​	这一行描述了`mkfs/mkfs`构建的文件系统：它有70个元数据块（用于描述文件系统的块）和199930个数据块，总计200000个块。

​	如果在实验期间的任何时候，您发现自己必须从头开始重建文件系统，您可以运行`make clean`，强制`make`重建`fs.img`。

**看什么**

​	磁盘索引节点的格式由`fs.h`中的`struct dinode`定义。您应当尤其对`NDIRECT`、`NINDIRECT`、`MAXFILE`和`struct dinode`的`addrs[]`元素感兴趣。

​	在磁盘上查找文件数据的代码位于`fs.c`的`bmap()`中。看看它，确保你明白它在做什么。在读取和写入文件时都会调用`bmap()`。写入时，`bmap()`会根据需要分配新块以保存文件内容，如果需要，还会分配间接块以保存块地址。

​	`	bmap()`处理两种类型的块编号。`bn`参数是一个“逻辑块号”——文件中相对于文件开头的块号。`ip->addrs[]`中的块号和`bread()`的参数都是磁盘块号。您可以将`bmap()`视为将文件的逻辑块号映射到磁盘块号。

**实验任务**

​	修改`bmap()`，以便除了直接块和一级间接块之外，它还实现二级间接块。你只需要有11个直接块，而不是12个，为你的新的二级间接块腾出空间；不允许更改磁盘`inode`的大小。`ip->addrs[]`的前11个元素应该是直接块；第12个应该是一个一级间接块（与当前的一样）；13号应该是你的新二级间接块.

### 实验步骤

1. 实验要求除了直接块和一级索引之外，添加二级索引，所以只需要有11个直接索引块，而不是12个，为新的二级索引块腾出空间，修改后共有一个索引最多有`11+256+256*256=65803`个块。所以首先需要修改的就是`kernel/fs.h`直接块号的宏定义 `NDIRECT` 改为11；添加二级索引块号的总数的宏定义`NDOUBLYINDIRECT`，表示的块号个数是一级索引的平方`NINDIRECT * NINDIRECT`；并修改文件最大体积的宏定义`MAXFILE`。代码如下：

```c
#define NDIRECT 11  //lab 9-1 modified
#define NDOUBLYINDIRECT (NINDIRECT * NINDIRECT)    //lab 9-1 add 二级索引块号总数
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLYINDIRECT)   //lab 9-1 modified
```

2. 修改`bmap`函数：`bmap`函数的功能是将文件系统中逻辑块号映射到磁盘块号。源代码已经完成了直接索引和一级索引的部分，我们只需要添加二级索引的部分代码，首先，代码执行`bn -= NINDIRECT;`，将逻辑块号`bn`减去`NINDIRECT`，这样`bn`的值就是相对于二级索引的偏移量。然后，代码通过判断`bn`是否小于`NDOUBLYINDIRECT`来确定是否需要使用二级索引。`NDOUBLYINDIRECT`是二级索引的大小，如果`bn`小于`NDOUBLYINDIRECT`，说明要访问的逻辑块在二级索引的范围内。接下来实现了对二级索引的访问和分配过程：首先，通过访问`ip->addrs[NDIRECT + 1]`获取二级索引的地址`addr`。如果二级索引的地址为0，表示尚未分配，需要分配一个新的磁盘块号，并保存在`ip->addrs[NDIRECT + 1]`中；然后，通过`bread`函数读取二级索引对应的磁盘块，并将数据保存在`bp`指向的缓冲区中；接下来，我们需要获取一级索引的地址。在二级索引中，每个元素都对应一个一级索引，可以通过`bn / NINDIRECT`来计算出在二级索引数组中的位置，然后通过访问一级索引数组中对应位置的元素来获取一级索引的地址，如果该一级索引的地址为0，表示尚未分配，需要分配一个新的磁盘块号，并记录在对应的一级索引位置上，然后将二级索引的块写回磁盘；然后，需要获取直接索引的地址。由于之前已经计算过在一级索引中的位置（即`bn / NINDIRECT`），因此可以通过`bn %= NINDIRECT`来计算出在一级索引指向的数组中的位置。然后通过访问直接索引数组中对应位置的元素来获取直接索引的地址。如果该直接索引为0，表示尚未分配，需要分配一个新的磁盘块号，并记录在对应的直接索引位置上，然后通过`log_write`函数将一级索引的块写回磁盘。

   代码如下：

```c
static uint
bmap(struct inode *ip, uint bn)
{
  //...
  //lab 9-1 add
  // 二级索引的情况
  bn -= NINDIRECT;
  if(bn < NDOUBLYINDIRECT) {
    if((addr = ip->addrs[NDIRECT + 1]) == 0) {
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    // 获取一级索引地址
    if((addr = a[bn / NINDIRECT]) == 0) {
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    bn %= NINDIRECT;
    // 获取直接索引地址
    if((addr = a[bn]) == 0) {
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  panic("bmap: out of range");
}
```

3. 修改`itrunc`函数：`itrunc`函数的功能是释放一个文件对应的磁盘块并将文件大小修改为0。在xv6文件系统中，文件的大小由`ip->size`字段表示，`itrunc`函数会将`ip->size`设置为0，然后逐个释放文件使用的磁盘块。在添加了二级索引的结构中，需要释放二级索引的数据块，这可以通过两层循环，依次遍历二级索引与内层的一级索引即可，代码如下：

```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;
  //lab 9-1 add
  int k;
  struct buf *bp2;
  uint* a2;

  //...
  // 释放二级索引
  if(ip->addrs[NDIRECT + 1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;
    //遍历二级索引
    for(j = 0; j < NINDIRECT; ++j) {
      if(a[j]) {
        bp2 = bread(ip->dev, a[j]);
        a2 = (uint*)bp2->data;
        //遍历内层
        for(k = 0; k < NINDIRECT; ++k) {
          if(a2[k]) {
            bfree(ip->dev, a2[k]);
          }
        }
        //释放内层
        brelse(bp2);
        bfree(ip->dev, a[j]);
        a[j] = 0;
      }
    }
    //释放外层
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

4. 测试：启动`qemu`后，通过`bigfile`指令进行测试，输出结果如下，说明测试通过：

```shell
init: starting sh
$ bigfile
..................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
wrote 65803 blocks
bigfile done; ok
```

​	然后运行`usertests`测试，输出如下（省略中间大部分输出），说明程序没有问题：

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
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3240
            sepc=0x00000000000056a6 stval=0x00000000000056a6
usertrap(): unexpected scause 0x000000000000000c pid=3241
            sepc=0x00000000000056a6 stval=0x00000000000056a6
......
test iref: OK
test forktest: OK
test bigdir: OK
ALL TESTS PASSED
```

最后进行单元测试，通过：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-9/xv6-labs-2020$ ./grade-lab-fs bigfile
make: 'kernel/kernel' is up to date.
== Test running bigfile == running bigfile: OK (48.5s)
```

### 实验中遇到的困难和解决方法

本次实验的困难主要在于代码编写。将思路理清后需要确保代码编写不出错。

### 实验心得

在做本次实验之前，需要了解xv6文件系统的大致组成:

xv6文件系统实现分为七层，如下表所示。磁盘层读取和写入virtio硬盘上的块。缓冲区高速缓存层缓存磁盘块并同步对它们的访问，确保每次只有一个内核进程可以修改存储在任何特定块中的数据。

| xv6文件系统层级                |
| ------------------------------ |
| 文件描述符（File descriptor）  |
| 路径名（Pathname）             |
| 目录（Directory）              |
| 索引结点（Inode）              |
| 日志（Logging）                |
| 缓冲区高速缓存（Buffer cache） |
| 磁盘（Disk）                   |

本次实验主要研究了文件的索引节点`inode`，这部分内容在理论课内也有体现。

**inode**：inode即索引结点，它有两种相关含义，指包含文件大小和数据块编号列表的磁盘上的数据结构，也可能指内存中的inode，它包含磁盘上inode的副本以及内核中所需的额外信息。磁盘上的inode由`struct dinode`定义，内核将活动的inode集合保存在内存中，`struct inode`是磁盘上`struct dinode`的内存副本。

​	磁盘上的`inode`结构体`struct dinode`包含一个`size`和一个块号数组。`inode`数据可以在`dinode`的`addrs`数组列出的块中找到。前面的`NDIRECT`个数据块被列在数组中的前`NDIRECT`个元素中；这些块称为直接块（direct blocks）。接下来的`NINDIRECT`个数据块不在inode中列出，而是在称为间接块（indirect block）的数据块中列出。`addrs`数组中的最后一个元素给出了间接块的地址。因此，在未修改的仅有一级索引的情况下，可以从inode中列出的块加载文件的前12 kB（`NDIRECT x BSIZE`）字节，而只有在查阅间接块后才能加载下一个256 kB（`NINDIRECT x BSIZE`）字节。函数`bmap`管理这种表示，`bmap(struct inode *ip, uint bn)`返回索引结点`ip`的第`bn`个数据块的磁盘块号。如果`ip`还没有这样的块，`bmap`会分配一个。`inode`结构体的结构可以表示如下：

![](http://xv6.dgs.zone/tranlate_books/book-riscv-rev1/images/c8/p2.png)

## Symbolic links ([moderate](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	在本实验中，您将向xv6添加符号链接。符号链接（或软链接）是指按路径名链接的文件；当一个符号链接打开时，内核跟随该链接指向引用的文件。符号链接类似于硬链接，但硬链接仅限于指向同一磁盘上的文件，而符号链接可以跨磁盘设备。尽管xv6不支持多个设备，但实现此系统调用是了解路径名查找工作原理的一个很好的练习。

​	您将实现`symlink(char *target, char *path)`系统调用，该调用在引用由`target`命名的文件的路径处创建一个新的符号链接。有关更多信息，请参阅`symlink`手册页（注：执行`man symlink`）。要进行测试，请将`symlinktest`添加到`Makefile`并运行它。

### 实验步骤

1. 根据提示，为`symlink`创建一个新的系统调用号，在`user/usys.pl`、`user/user.h`中添加声明，并在`Makefile`文件中添加编译链接。

```c
// kernel/syscall.h
#define SYS_symlink  22     //lab 9-2 add

//kernel/syscall.c
extern uint64 sys_symlink(void);  //lab 9-2 add
//...
[SYS_symlink] sys_symlink,  //lab 9-2 add
};

//user/user.h
int symlink(char *target, char *path);  //lab 9-2 add
```

```perl
entry("symlink");   # lab 9-2 add
```

```makefile
$U/_symlinktest\
```

2. 向`kernel/stat.h`添加新的文件类型（`T_SYMLINK`）以表示符号链接。

```shell
#define T_SYMLINK 4	  // lab 9-2 add
```

3. 在`kernel/fcntl.h`中添加一个新标志（`O_NOFOLLOW`），该标志可用于`open`系统调用。

```c
#define O_NOFOLLOW 0x004	//lab 9-2 add
```

4. 实现`sys_symlink()`函数：该函数用于创建一个符号链接文件。代码思路如下：首先，通过`argstr()`函数从用户态获取两个参数：`target`和`path`，这两个参数分别表示要创建的符号链接的目标路径和符号链接文件的路径；然后调用`begin_op()`开始文件系统的操作；接着调用`create()`函数创建符号链接文件，若创建失败，则返回错误并结束文件系统操作；接下来使用`writei()`函数将`target`字符串写入到刚刚创建的符号链接文件中，将符号链接与其目标路径关联起来，最后解锁符号链接文件的inode：`ip`，结束文件系统操作，释放全局锁。返回值为0代表符号链接建立成功。

```c
// lab 9-2 add
// sys_symlink 实现
uint64
sys_symlink(void) {
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;
  int n;

  if ((n = argstr(0, target, MAXPATH)) < 0
    || argstr(1, path, MAXPATH) < 0) {
    return -1;
  }

  begin_op();
  // 创建符号链接
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  // 写入目标路径
  if(writei(ip, 0, (uint64)target, 0, n) != n) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}
```

5. 修改`sys_open()`函数，实现有符号链接情况下的文件打开。

​	为了寻找符号链接的目标文件，封装了一个独立的函数`follow_symlink()`，而在处理符号链接时需要考虑的两个问题：一是成环检测，即符号链接的目标文件可能是另一个符号链接，而这个目标文件又可能指向之前的符号链接，形成了一个循环链路，导致无限循环；二是链接深度限制，即为了防止过多的递归导致栈溢出或资源耗尽，需要对递归的链接深度进行限制。

​	处理链接深度限制，可以在 `kernel/fs.h` 中宏定义 `NSYMLINK` 表示最大的符号链接深度, 超过该深度将返回错误，代码如下:

```c
#define NSYMLINK 10 // lab 9-2 add 符号链接最大深度
```

​	处理成环检测，则使用下面的办法：创建一个大小为 `NSYMLINK` 的数组 `inums` ，记录每次跟踪到的文件的 `inode number`，每次寻找到一个目标文件后，用这个文件的 `inode number`去`inums`数组中查找，若有搜索到相同的则说明成环。

​	`follow_symlink()`函数限制递归深度，最多进行 `NSYMLINK` 次迭代，通过递归的方式跟踪符号链接的目标文件，并在递归过程中使用 `inums` 数组记录已访问过的 inode 号，从而进行成环检测。代码如下：

```c
//lab 9-2 add
//follow_symlink
static struct inode* 
follow_symlink(struct inode* ip) {
  uint inums[NSYMLINK];
  int i, j;
  char target[MAXPATH];

  for(i = 0; i < NSYMLINK; ++i) {
    inums[i] = ip->inum;
    if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0) {
      iunlockput(ip);
      printf("open_symlink: open symlink failed\n");
      return 0;
    }
    iunlockput(ip);
    if((ip = namei(target)) == 0) {
      printf("open_symlink: path \"%s\" is not exist\n", target);
      return 0;
    }
    //检测是否成环
    for(j = 0; j <= i; ++j) {
      if(ip->inum == inums[j]) {
        printf("open_symlink: links form a cycle\n");
        return 0;
      }
    }
    ilock(ip);
    if(ip->type != T_SYMLINK) {
      return ip;
    }
  }

  iunlockput(ip);
  printf("open_symlink: the depth of links reaches the limit\n");
  return 0;
}
```

随后，修改`sys_open()`文件，在非 `NO_FOLLOW` 的情况下需要将当前文件的`inode`替换为由 `follow_symlink()` 得到的目标文件的 `inode` 再进行后续的操作，代码如下：

```c
uint64
sys_open(void)
{
  //...
  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // lab 9-2 add
  // 符号链接
  if(ip->type == T_SYMLINK && (omode & O_NOFOLLOW) == 0) {
    if((ip = follow_symlink(ip)) == 0) {
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
//...
}
```

6. 测试：启动`qemu`后首先进行`symlinktest` 测试，输出如下，测试通过

```shell
$ symlinktest
Start: test symlinks
open_symlink: path "/testsymlink/a" is not exist
open_symlink: links form a cycle
test symlinks: ok
Start: test concurrent symlinks
test concurrent symlinks: ok
```

接下来进行`usertests`测试，输出如下（省略中间大部分输出），测试通过:

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
test sbrkbugs: usertrap(): unexpected scause 0x000000000000000c pid=3243
            sepc=0x00000000000056a6 stval=0x00000000000056a6
usertrap(): unexpected scause 0x000000000000000c pid=3244
            sepc=0x00000000000056a6 stval=0x00000000000056a6
OK
......
test dirfile: OK
test iref: OK
test forktest: OK
test bigdir: OK
ALL TESTS PASSED
```

接下来进行单元测试，输出如下，通过:

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-9/xv6-labs-2020$ ./grade-lab-fs symlinktest
make: 'kernel/kernel' is up to date.
== Test running symlinktest == (1.0s)
== Test   symlinktest: symlinks ==
  symlinktest: symlinks: OK
== Test   symlinktest: concurrent symlinks ==
  symlinktest: concurrent symlinks: OK
```

### 实验中遇到的困难和解决方法

**符号链接锁的问题及分析**：

​	这部分相关的代码在`follow_symlink()` 函数中体现。

​	符号链接最终打开的是链接的目标文件。因此，在处理符号链接时，会先释放当前符号链接的 `inode` 的锁，并转而获取目标文件的 `inode` 的锁。然而，在处理符号链接时需要读取 `ip->type` 字段，判断当前 `inode` 是否为符号链接，此时不能释放当前符号链接的 `inode` 的锁，因此，在进入 `follow_symlink()` 函数时，会一直持有当前符号链接 `inode` 的锁，确保在读取 `ip->type` 字段时不会发生竞争。当使用 `readi()` 读取了符号链接中记录的目标文件路径后，此时已经获取了目标文件的路径信息，因此不再需要当前符号链接的 `inode`，可以使用 `iunlockput()` 释放当前符号链接的锁和 `inode`。

​	总之，为了保证对符号链接的处理过程中不出现竞争，`follow_symlink()` 函数在读取符号链接中的目标文件路径时，一直持有当前符号链接 `inode` 的锁，直到获取到目标文件的路径信息后才会释放当前符号链接的锁。同时，为了避免死锁，需要正确处理锁的释放和加锁的顺序，确保在函数调用前后都能正确持有目标文件的 `inode` 锁。

### 实验心得

本次实验主要探究了文件系统中的链接。

**硬链接**：硬链接是指多个文件名指向同一个物理文件的链接关系。它们在文件系统中具有相同的inode号，但可以位于不同的目录中。当创建硬链接时，实际上是为文件增加了一个新的路径入口。硬链接与原始文件之间没有区别，它们是完全平等的。删除任何一个链接都不会影响其他链接。

**软链接**：软链接是指一个文件名指向另一个文件或目录的符号链接。与硬链接不同，软链接实际上是一个特殊类型的文件，其中包含指向目标文件或目录的路径信息，也是本次实验主要实现的内容。创建软链接时，操作系统会为其分配一个新的inode，并在文件系统中的目录项中添加软链接的信息，指向目标文件或目录的路径。当访问软链接时，操作系统会通过路径信息找到目标文件或目录。

## 实验评分并提交

运行`make grade`，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-9/xv6-labs-2020'
== Test running bigfile ==
$ make qemu-gdb
running bigfile: OK (85.5s)
== Test running symlinktest ==
$ make qemu-gdb
(0.7s)
== Test   symlinktest: symlinks ==
  symlinktest: symlinks: OK
== Test   symlinktest: concurrent symlinks ==
  symlinktest: concurrent symlinks: OK
== Test usertests ==
$ make qemu-gdb
usertests: OK (150.5s)
== Test time ==
time: OK
Score: 100/100
```

实验通过，上传远程代码仓库即可。
