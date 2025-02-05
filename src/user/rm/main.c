#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int i;

    if (argc < 2) {
        printf("Usage: rm <filename1> <filename2> ...\n");
        exit(0);
    }

    for (i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            printf("rm: %s failed to delete\n", argv[i]);
            break;
        }
    }

    exit(0);
}