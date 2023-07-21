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