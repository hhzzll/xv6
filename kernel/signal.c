#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// insert trapframe into the user stack
void setup_frame(void *void_tf, pagetable_t pagetable) {
    struct trapframe *tf = (struct trapframe *)void_tf;
    struct trapframe *signal_tf = (struct trapframe *)(tf->sp - sizeof(struct trapframe));
    copyout(pagetable, (uint64)signal_tf, (char *)tf, sizeof(struct trapframe));
    tf->sp = (uint64)signal_tf;
}

void restore_context(void *void_tf, pagetable_t pagetable) {
    struct trapframe *tf = (struct trapframe *)void_tf;
    struct trapframe ori_tf;
    // get the original trapframe in the user stack
    // s0 points to the top of caller's trapframe, which is the original trapframe
    copyin(pagetable, (char *)&ori_tf, (uint64)tf->s0, sizeof(struct trapframe));
    *tf = ori_tf;
}
