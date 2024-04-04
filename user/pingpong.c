#include "kernel/types.h"
#include "user/user.h"

#define READ 0
#define WRITE 1

int main(int argc, char *argv[])
{
    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);

    if (fork() == 0) // child
    {
        close(p1[WRITE]);
        close(p2[READ]);
        char c;
        read(p1[READ], &c, 1);
        printf("%d: received ping\n", getpid());
        c = 'a';
        write(p2[WRITE], &c, 1);
        close(p1[READ]);
        close(p2[WRITE]);
    }
    else
    {
        close(p1[READ]);
        close(p2[WRITE]);
        char c = 'b';
        write(p1[WRITE], &c, 1);
        read(p2[READ], &c, 1);
        printf("%d: received pong\n", getpid());
        close(p1[WRITE]);
        close(p2[READ]);
    }
    exit(0);
}