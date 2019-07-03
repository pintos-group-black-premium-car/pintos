#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "filesys/off_t.h"

#include "vm/swap.h"

enum page_status {
    ALL_ZEROS, 
    ON_FRAME, 
    ON_SWAP, 
    FROM_FILESYS
};

struct supplemental_page_table {
    struct hash page_map;
};

struct supplemental_page_table_entry {
    void *upage;
    void *kpage;
    struct hash_elem elem;
    enum page_status status;
    bool is_dirty;
    swap_index_t swap_index;
    struct file *file;
    off_t file_offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
};

struct supplemental_page_table *vm_supt_create();
void vm_supt_destroy(struct supplemental_page_table *);

bool vm_supt_install_frame(struct supplemental_page_table *, void *, void *);
bool vm_supt_install_zeropage(struct supplemental_page_table *, void *);
bool vm_supt_set_swap(struct supplemental_page_table *, void *, swap_index_t);
bool vm_supt_install_filesys(struct supplemental_page_table *, void *, struct file *, off_t, uint32_t, uint32_t, bool);

struct supplemental_page_table_entry *vm_supt_find(struct supplemental_page_table *, void *);
bool vm_supt_has_entry(struct supplemental_page_table *, void *);

bool vm_supt_set_dirty(struct supplemental_page_table *, void *, bool);

bool vm_load_page(struct supplemental_page_table *, uint32_t *, void *);

bool vm_supt_munmap(struct supplemental_page_table *, uint32_t *, void *, struct file *, off_t, size_t);

void vm_pin_page(struct supplemental_page_table *, void *);
void vm_unpin_page(struct supplemental_page_table *, void *);

#endif
