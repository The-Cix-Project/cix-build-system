#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_all(int fd, const void *data, size_t size) {
    const char *bytes = data;

    while (size != 0) {
        ssize_t written = write(fd, bytes, size);

        if (written <= 0)
            return 0;
        bytes += written;
        size -= (size_t)written;
    }
    return 1;
}

int main(int argc, char **argv) {
    struct sockaddr_in address;
    struct stat file_stat;
    char response[128];
    char request[1024];
    char buffer[4096];
    int client;
    int file;
    int listener;
    int port;
    ssize_t received;

    if (argc != 3)
        return 2;
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535)
        return 2;

    file = open(argv[2], O_RDONLY);
    if (file < 0 || fstat(file, &file_stat) != 0 || file_stat.st_size < 0)
        return 1;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return 1;
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                     sizeof(int));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        close(listener);
        close(file);
        return 1;
    }
    client = accept(listener, NULL, NULL);
    if (client < 0) {
        close(listener);
        close(file);
        return 1;
    }
    received = read(client, request, sizeof(request));
    if (received <= 0)
        goto fail;
    (void)snprintf(response, sizeof(response),
                   "HTTP/1.0 200 OK\r\nContent-Length: %lld\r\n"
                   "Content-Type: application/octet-stream\r\n\r\n",
                   (long long)file_stat.st_size);
    if (!write_all(client, response, strlen(response)))
        goto fail;
    for (;;) {
        ssize_t count = read(file, buffer, sizeof(buffer));

        if (count < 0)
            goto fail;
        if (count == 0)
            break;
        if (!write_all(client, buffer, (size_t)count))
            goto fail;
    }
    close(client);
    close(listener);
    close(file);
    return 0;

fail:
    close(client);
    close(listener);
    close(file);
    return 1;
}
