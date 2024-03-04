#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>

char path[] = "/mnt/pmem1/zzh/testfile";

int main()
{
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        printf("file open error\n");
        exit(1);
    }
    
    int err = posix_fallocate(fd, 0, 0x1000);
    if (err == -1) {
	printf("ftruncate file error\n");
	exit(1);
    }


    void *addr = mmap(0, 0x1000, PROT_READ | PROT_WRITE, 0x80003, fd, 0);
    printf("shabi\n");
    int *arr = (int*)addr;
    for (int i = 0; i < 10; i++) {
	printf("%d\n", arr[i]);
        arr[i] = i;
    }

    munmap(addr, 0x1000);
    close(fd);

    return 0;
}
