#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include "threads/palloc.h"

void vm_frame_init();
void *vm_frame_alloc(enum palloc_flags, void *);

void vm_frame_free(void *);
void vm_frame_remove_entry(void *);

void vm_frame_pin(void *);
void vm_frame_unpin(void *);

#endif