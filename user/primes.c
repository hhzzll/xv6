#include "kernel/types.h"
#include "user/user.h"

int is_prime(int n) {
    if (n <= 1)
        return 0;
    for (int i = 2; i * i <= n; i++)
        if (n % i == 0)
            return 0;
    return 1;
}

int main() {
    for (int i = 2; i <= 35; i++)
        if (is_prime(i))
            printf("prime %d\n", i);
    exit(0);
}
