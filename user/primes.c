#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define READ 0
#define WRITE 1

int leftFirstData(int lpipe[2], int *dst)
{
  if (read(lpipe[READ], dst, sizeof(int)) == sizeof(int)) {
    printf("prime %d\n", *dst);
    return 0;
  }
  return -1;
}

void sendData(int lpipe[2], int rpipe[2], int first)
{
  int data;
  // 从左管道读取数据
  while (read(lpipe[READ], &data, sizeof(int)) == sizeof(int)) {
    // 将无法整除的数据传递入右管道
    if (data % first)
      write(rpipe[WRITE], &data, sizeof(int));
  }
  close(lpipe[READ]);
  close(rpipe[WRITE]);
}

void primes(int lpipe[2])
{
  close(lpipe[WRITE]);
  int first;
  if (leftFirstData(lpipe, &first) == 0) {
    int p[2];
    pipe(p); // 当前的管道
    sendData(lpipe, p, first);
    if (fork() == 0) {
      primes(p);    // 递归调用
    }
    else {
      close(p[READ]);
      wait(0);
    }
  }
  exit(0);
}

int main(int argc, char const *argv[])
{
    //初始管道
  int p[2];
  pipe(p);
  for (int i = 2; i <= 35; i++) //写入初始数据
    write(p[WRITE], &i, sizeof(int));
  if (fork() == 0) {
    primes(p);
  } 
  else {
    close(p[WRITE]);
    close(p[READ]);
    wait(0);
  }
  exit(0);
}