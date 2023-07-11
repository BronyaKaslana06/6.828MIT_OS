#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

void find(char *path, const char *filename)
{
  char buf[512], *p;
  int fd; //文件标识符
  struct dirent di; //文件目录项
  struct stat st; //文件基本信息结构体

  //打开目录
  if ((fd = open(path, 0)) < 0) {
    printf("find: cannot open %s\n", path);
    return;
  }

  //获取目录文件基本状态信息
  if (fstat(fd, &st) < 0) {
    printf("find: cannot fstat %s\n", path);
    close(fd);
    return;
  }

  //参数错误，find的第一个参数必须是目录
  if (st.type != T_DIR) {
    printf("The first argument should be a DIRECTORY! \n usage: find <DIRECTORY> <filename>\n");
    return;
  }
  //检查路径长度是否超出缓冲区大小的限制。
  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
    printf("find: path too long\n");
    return;
  }

  //将 path复制到缓冲区buf中，指针p指向buf的末尾。
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/'; //p指向最后一个'/'之后

  //递归查询每个子目录项
  while (read(fd, &di, sizeof(di)) == sizeof(di)) {
    //过滤掉以无效的目录项
    if (di.inum == 0)
      continue;
    memmove(p, di.name, DIRSIZ); //添加路径名称
    p[DIRSIZ] = 0;               //字符串结束标志

    //未能获取目录项的文件信息，输出错误信息后处理下一项
    if (stat(buf, &st) < 0) {
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    //递归，要求st是文件目录且排除.和..的情况
    if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
      find(buf, filename);
    } 
    else if (strcmp(filename, p) == 0)
      printf("%s\n", buf);
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3) {
    printf("usage: find <directory> <filename>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}