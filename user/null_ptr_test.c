#include "kernel/types.h"
#include "user/user.h"

int main() {
    print_pgtbl();
    int *ptr = 0;
    printf("Null pointer test\n");
    printf("Value at null pointer: %d\n", *ptr);
    return 0;
}