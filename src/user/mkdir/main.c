#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    if (argc < 2) {
        printf("Usage: mkdir <filename>\n");
        exit(1);
    }

    if (mkdir(argv[1], 0) < 0) {
        printf("mkdir: failed to create %s\n", argv[1]);
    }
    // printf("finish mkdir %s ...\n", argv[i]);
    /* (Final) TODO END */
    exit(0);
}