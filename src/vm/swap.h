#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "lib/stdint.h"

typedef uint32_t swap_index_t;

void vm_swap_init();

swap_index_t vm_swap_out(void *);

void vm_swap_in(swap_index_t, void *);

void vm_swap_free(swap_index_t);

#endif