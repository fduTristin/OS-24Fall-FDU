#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    int i;

    if (argc < 2) {
        printf("Usage: mkdir <filename1> <filename2> ...\n");
        exit(0);
    }

    for (i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0) < 0) {
            printf("mkdir: %s failed to create\n", argv[i]);
            break;
        }
    }
    /* (Final) TODO END */
    exit(0);
}