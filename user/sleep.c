#include "kernel/types.h"
#include "user/user.h"

const int stderr = 2;
int main(int argc, char *argv[]) {
    if (argc != 2)
    {
        fprintf(stderr, "Usage: sleep seconds\n");
        exit(1);
    }
    sleep(atoi(argv[1]));
    exit(0);
}