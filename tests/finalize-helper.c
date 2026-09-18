#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char path[4096];
    int descriptor;
    if (argc != 2 ||
        snprintf(path, sizeof(path), "%s/finalized-by-embedder", argv[1]) >=
            (int)sizeof(path))
        return 2;
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (descriptor < 0)
        return 3;
    return close(descriptor) == 0 ? 0 : 4;
}
