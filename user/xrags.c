#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    //buf是一个大小为 512 字节的字符数组，用于存储从标准输入读取的数据。
    char buf[512];
    //argBuf存储上一个程序的标准输出
    char argBuf[32][32];
    //pass是一个字符指针数组，用于存储参数指针。
    char *argPtr[32];
    for(int i=0; i<32; i++)
        argPtr[i] = argBuf[i];
    for(int i = 1;i<argc;i++) {
        strcpy(argBuf[i-1],argv[i]);
    }

    //n是一个整型变量，用于存储从标准输入读取的字符数
    int n;
    //p是一个字符指针，用于遍历buf
    char *p = buf;
    //pos是一个整型变量，用于指示当前参数的位置
    int pos = argc-1;
    //c是一个字符指针，用于指向当前参数的位置
    char *c = argPtr[pos];
    argPtr[pos+1] = 0;
    n = read(0, buf, 512);

    do {
        p = buf;
        if(n < 0) {
            exit(-1);
        }
        while( p < buf + n ) {
            if(*p == ' ' || *p == '\n') {
                *c = '\0';
                if(fork()) {
                    wait(0);
                } 
                else {
                    exec(argPtr[0],argPtr);
                }
                ++p;
                c = argPtr[pos];
            } 
            else {
                *c++ = *p++;
            }
        }
    } while((n = read(0, buf, 512)) > 0);

    if(n < 0) {
        printf("xargs:read error\n");
        exit(-1);
    }
    exit(0);
}