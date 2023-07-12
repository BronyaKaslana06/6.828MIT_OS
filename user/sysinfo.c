#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    if (argc != 1)
    {
        fprintf(2, "Usage: %s need not param\n", argv[0]);
        exit(1);
    }

    struct sysinfo info;
    sysinfo(&info);
    
    printf("free space: %d\nused process: %d\n", info.freemem, info.nproc);
    exit(0);
}