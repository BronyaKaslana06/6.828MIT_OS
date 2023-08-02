# Lab11 networking

在本实验中，您将为网络接口卡（NIC）编写一个xv6设备驱动程序。

[TOC]

## networking ([hard](https://pdos.csail.mit.edu/6.828/2020/labs/guidance.html))

### 实验目的

​	您将使用名为E1000的网络设备来处理网络通信。对于xv6（以及您编写的驱动程序），E1000看起来像是连接到真正以太网局域网（LAN）的真正硬件。事实上，用于与您的驱动程序对话的E1000是qemu提供的模拟，连接到的LAN也由qemu模拟。在这个模拟LAN上，xv6（guest）的IP地址为10.0.2.15。qemu还安排运行qemu的计算机出现在IP地址为10.0.2.2的LAN上。当xv6使用E1000将数据包发送到10.0.2.2时，qemu会将数据包发送到运行qemu的（真实）计算机上的相应应用程序（“主机”）。

​	您将使用QEMU的“用户模式网络栈（user-mode network stack）”。QEMU的文档中有更多关于用户模式栈的内容。我们已经更新了`Makefile`以启用QEMU的用户模式网络栈和E1000网卡。

​	您的工作是在`kernel/e1000.c`中完成`e1000_transmit()`和`e1000_recv()`，以便驱动程序可以发送和接收数据包。

​	为了测试实验的驱动，在一个窗口运行 `make server` ，在另一个窗口运行 `make qemu`，然后在 xv6 中运行 `nettests`。`nettests` 第一个测试用例尝试发送一个` UDP packet `给 `host OS`，发送给` host OS `上 `make server` 运行的程序。如果没有完成实验，E1000 驱动不会真正的发送 `packet`，并且什么都不会发生。

​	完成实验之后，E1000 驱动将发送 `packet`，qemu 将` packet `传给 host 计算机，`make server` 能收到这个 packet，且会发送一个响应 `packet`，然后 E1000 驱动和 `nettests` 将收到响应 `packet`。然而，在 host 发送响应之前，它先发送一个 “ARP” 请求 `packet `给 xv6，查找 4848 位的以太网地址，期待 xv6 发回一个 ARP 响应。实验不需要考虑这个问题，`kernel/net.c` 负责处理。如果一切 OK，`nettests` 将打印 `testing ping: OK`,`make server` 将打印 `a message from xv6!`。

### 实验步骤

1. 在 `kernel/e1000.c` 中实现`e1000_transmit（）`函数，该函数的作用是将以太网数据帧发送到网卡，然后让网卡通过网络将这个数据帧发送出去。函数的思路如下：、

   首先，检查网卡的发送缓冲区是否有可用的位置，即检查`E1000_TXD_STAT_DD` 是否被设置，如果没有则返回错误，若发送缓冲区没有溢出，则使用 `mbuffree()` 函数释放最后的已发送但尚未释放的` mbuf`；

   参考 E1000 手册的 Section 3.3，将 `m->head` 指针指向内存中数据包的内容，`m->len` 存储数据包的长度,置描述符的相关标志 `cmd`，并保存指向该 mbuf 的指针，以便稍后释放。

   最后，更新`ring`的索引（`E1000_TDT` 加 11 模 `TX_RING_SIZE`），使网卡知道哪个位置是下一个待发送的数据帧。

   如果`e1000_transmit()`成功地将`mbuf`添加到环中，则返回0。如果失败（例如，没有可用的描述符来传输`mbuf`）.，则返回-1。

代码如下：

```c
int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  acquire(&e1000_lock);
  uint32 next_index = regs[E1000_TDT];
  if((tx_ring[next_index].status & E1000_TXD_STAT_DD) == 0){
      release(&e1000_lock);
      return -1;
  }
  if(tx_mbufs[next_index])
      mbuffree(tx_mbufs[next_index]);
  tx_ring[next_index].addr = (uint64)m->head;
  tx_ring[next_index].length = (uint16)m->len;
  tx_ring[next_index].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  tx_mbufs[next_index] = m;
  regs[E1000_TDT] = (next_index+1)%TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}
```

2. 实现`e1000_recv`，该函数的作用是从网卡接收数据帧，处理并传递给网络栈进行进一步处理

   首先通过提取`E1000_RDT`控制寄存器并加11再模 `RX_RING_SIZE`，获取` ring` 索引，向E1000询问下一个等待接收数据包所在的环索引；

   然后检查描述符`status`部分中的`E1000_RXD_STAT_DD`位来检查新数据包是否可用。如果不可用，抛出`panic`并停止；

   然后将`mbuf`的`m->len`更新为描述符中报告的长度。使用`net_rx()`将`mbuf`传送到网络栈；

   接着使用`mbufalloc()`分配一个新的`mbuf`，以替换刚刚给`net_rx()`的`mbuf`。将其数据指针（`m->head`）编程到描述符中。将描述符的状态位清除为零。

   最后，更新 `E1000_RDT` 寄存器的值，指向已经处理的最后一个描述符的索引，标志着接收缓冲区已处理数据包的位置。

   ```c
   static void
   e1000_recv(void)
   {
     //
     // Your code here.
     //
     // Check for packets that have arrived from the e1000
     // Create and deliver an mbuf for each packet (using net_rx()).
     uint32 next_index = (regs[E1000_RDT]+1)%RX_RING_SIZE;
     while(rx_ring[next_index].status & E1000_RXD_STAT_DD){
       if(rx_ring[next_index].length>MBUF_SIZE){
           panic("MBUF_SIZE OVERFLOW!");
       }
       rx_mbufs[next_index]->len = rx_ring[next_index].length;
       net_rx(rx_mbufs[next_index]);
       rx_mbufs[next_index] =  mbufalloc(0);
       rx_ring[next_index].addr = (uint64)rx_mbufs[next_index]->head;
       rx_ring[next_index].status = 0;
       next_index = (next_index+1)%RX_RING_SIZE;
     }
     regs[E1000_RDT] = (next_index-1)%RX_RING_SIZE;
   }
   ```

   

测试：启动qemu后，在终端中运行`make server`启动服务器，输出如下：

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-11/xv6-labs-2020$ make server
python3 server.py 26099
listening on localhost port 26099
```

新启动一个终端，启动qemu后运行`nettests`命令进行测试，输出如下，说明测试通过:

```shell
$ nettests
nettests running on port 26099
testing ping: OK
testing single-process pings: OK
testing multi-process pings: OK
testing DNS
DNS arecord for pdos.csail.mit.edu. is 128.52.129.126
DNS OK
all tests passed.
```

同时服务端输出下面的内容，说明接受到数据包:

```shell
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
a message from xv6!
```

运行`tcpdump -XXnr packets.pcap`输出如下，符合预期

```shell
bronya_k@LAPTOP-TBUJAUQE:~/xv6-labs-11/xv6-labs-2020$ tcpdump -XXnr packets.pcap
reading from file packets.pcap, link-type EN10MB (Ethernet)
03:10:19.412472 IP 10.0.2.15.2000 > 10.0.2.2.26099: UDP, length 19
        0x0000:  ffff ffff ffff 5254 0012 3456 0800 4500  ......RT..4V..E.
        0x0010:  002f 0000 0000 6411 3eae 0a00 020f 0a00  ./....d.>.......
        0x0020:  0202 07d0 65f3 001b 0000 6120 6d65 7373  ....e.....a.mess
        0x0030:  6167 6520 6672 6f6d 2078 7636 21         age.from.xv6!
03:10:19.412940 ARP, Request who-has 10.0.2.15 tell 10.0.2.2, length 28
        0x0000:  ffff ffff ffff 5255 0a00 0202 0806 0001  ......RU........
        0x0010:  0800 0604 0001 5255 0a00 0202 0a00 0202  ......RU........
        0x0020:  0000 0000 0000 0a00 020f                 ..........
03:10:20.336234 ARP, Reply 10.0.2.15 is-at 52:54:00:12:34:56, length 28
        0x0000:  ffff ffff ffff 5254 0012 3456 0806 0001  ......RT..4V....
        0x0010:  0800 0604 0002 5254 0012 3456 0a00 020f  ......RT..4V....
        0x0020:  5255 0a00 0202 0a00 0202                 RU........
```

（仅展示部分输出）

### 实验中遇到的困难和解决办法

本次实验中编程主要通过提示一步一步的进行，同时查阅[E1000 Software Developer's Manual](https://pdos.csail.mit.edu/6.828/2020/readings/8254x_GBe_SDM.pdf)获取具体的编程细节。

### 心得体会

本次实验主要研究通过E1000网卡来处理网络通信。

在实验中，E1000 用作操作系统（如 xv6）与物理网络之间的接口，负责处理数据包的发送和接收。操作系统可以通过 E1000 控制器向网络发送数据包，也可以通过 E1000 控制器接收来自网络的数据包。E1000 控制器的驱动程序负责与操作系统的网络协议栈进行交互，将数据包从物理设备传递到操作系统，并将要发送的数据包发送到物理设备。

实验中实现的过程大致描述了数据在发送和接收过程中在环形缓冲区数组 `tx_ring` 和 `rx_ring` 中的传递。这是一个典型的数据包传输的流程，其中涉及到网卡的硬件支持和中断处理。过程大致如下：

1. 数据发送过程：
   - 将要发送的数据包放入环形缓冲区数组 `tx_ring` 内：首先，驱动程序将要发送的数据包放入环形缓冲区数组 `tx_ring` 中的一个描述符中。该描述符包含了数据包在内存中的地址、数据包的长度以及一些其他的控制信息。
   - 递增 `E1000_TDT`：完成数据包的设置后，驱动程序递增 `E1000_TDT` 寄存器，通知网卡可以开始发送该数据包。
   - 网卡自动发送：网卡会自动根据 `E1000_TDT` 指向的描述符发送数据包。网卡使用 DMA（直接内存访问）技术从内存中读取数据包并发送到网络。
2. 数据接收过程：
   - 网卡接收数据：当网卡从网络接收到数据包时，它使用 DMA 技术将数据包放入环形缓冲区数组 `rx_ring` 中的一个描述符中。这个描述符包含了数据包在内存中的地址、数据包的长度以及一些其他的控制信息。
   - 向 CPU 发起硬件中断：在数据包放入 `rx_ring` 的描述符后，网卡会向 CPU 发起一个硬件中断，通知 CPU 有新的数据包到达。
   - CPU 中断处理：当 CPU 收到网卡的中断请求后，操作系统的中断处理程序会执行相应的操作，通常会调用驱动程序的接收函数 `e1000_recv()`。在该函数中，驱动程序会扫描 `rx_ring`，读取接收到的数据包，并将数据包传递给网络栈进行处理。

这个过程中的环形缓冲区数组 `tx_ring` 和 `rx_ring` 提供了一种循环利用缓冲区的机制，从而实现了高效的数据包传输。网卡的硬件支持和 DMA 技术使得数据包的发送和接收过程可以在不占用 CPU 大量时间的情况下完成，从而提高了数据传输的效率。同时，硬件中断机制使得当有新的数据包到达时，操作系统可以及时处理，并将数据包传递给应用程序或网络栈进行处理

总之，E1000 驱动程序负责管理 E1000 控制器与操作系统之间的数据传输，处理接收到的数据包，并将数据包传递给网络栈进行处理。通过完成驱动程序的实现，可以使操作系统能够与网络进行通信，实现网络通信功能。

## 实验评分和提交

运行`make grade`，输出如下：

```shell
make[1]: Leaving directory '/home/bronya_k/xv6-labs-11/xv6-labs-2020'
== Test running nettests ==
$ make qemu-gdb
(5.8s)
== Test   nettest: ping ==
  nettest: ping: OK
== Test   nettest: single process ==
  nettest: single process: OK
== Test   nettest: multi-process ==
  nettest: multi-process: OK
== Test   nettest: DNS ==
  nettest: DNS: OK
== Test time ==
time: OK
Score: 100/100
```

说明实验通过，上传远程代码仓库即可。