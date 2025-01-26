#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 512

char buf[BUFSIZE];

void cat(int fd)
{
    int len;
    while ((len = read(fd, buf, BUFSIZE)) > 0) {
        if (write(1, buf, len) != len) {
            printf(1, "cat: write error\n");
            exit(1);
        }
    }
    if (len < 0) {
        printf("cat: failed to read\n");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    int fd;
    if (argc <= 1) {
        cat(0);
        exit(0);
    }
    for (int i = 1; i < argc; ++i) {
        if ((fd = open(argv[i], 0)) < 0) {
            printf("cat: can not open %s\n", argv[i]);
            exit(0);
        }
        cat(fd);
        write(STDOUT_FILENO, "\n", 2);
        close(fd);
    }
    exit(0);
    /* (Final) TODO END */
    return 0;
}