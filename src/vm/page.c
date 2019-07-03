#include "lib/kernel/hash.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include <string.h>

static unsigned spte_hash_func(const struct hash_elem *elem, void *aux UNUSED) {
    struct supplemental_page_table_entry *entry = hash_entry(elem, struct supplemental_page_table_entry, elem);
    return hash_int((int) entry->upage);
}

static bool spte_less_func(const struct hash_elem *elem1, const struct hash_elem *elem2, void *aux UNUSED) {
    struct supplemental_page_table_entry *entry1 = hash_entry(elem1, struct supplemental_page_table_entry, elem);
    struct supplemental_page_table_entry *entry2 = hash_entry(elem2, struct supplemental_page_table_entry, elem);
    return entry1->upage < entry2->upage;
}

static void spte_destroy_func(struct hash_elem *elem, void *aux UNUSED) {
    struct supplemental_page_table_entry *entry = hash_entry(elem, struct supplemental_page_table_entry, elem);

    if (entry->kpage != NULL) {
        ASSERT(entry->status == ON_FRAME);
        vm_frame_remove_entry(entry->kpage);
    } else if (entry->status == ON_SWAP) {
        vm_swap_free(entry->swap_index);
    }
    free(entry);
}

static bool vm_load_page_from_filesys(struct supplemental_page_table_entry *spte, void *kpage) {
    file_seek(spte->file, spte->file_offset);

    uint32_t n = file_read(spte->file, kpage, spte->read_bytes);
    if (n != spte->read_bytes) {
        return false;
    }

    ASSERT(spte->read_bytes + spte->zero_bytes == PGSIZE);
    memset(kpage + n, 0, spte->zero_bytes);
    return true;
}

// interfaces

struct supplemental_page_table *vm_supt_create() {
    struct supplemental_page_table *supt = (struct supplemental_page_table *) malloc(sizeof(struct supplemental_page_table));
    hash_init(&supt->page_map, spte_hash_func, spte_less_func, NULL);
    return supt;
}

void vm_supt_destroy(struct supplemental_page_table *supt) {
    ASSERT(supt != NULL);

    hash_destroy(&supt->page_map, spte_destroy_func);
    free(supt);
}

bool vm_supt_install_frame(struct supplemental_page_table *supt, void *upage, void *kpage) {
    struct supplemental_page_table_entry *spte = (struct supplemental_page_table_entry *) malloc(sizeof(struct supplemental_page_table_entry));
    spte->upage = upage;
    spte->kpage = kpage;
    spte->status = ON_FRAME;
    spte->is_dirty = false;
    spte->swap_index = -1;

    struct hash_elem *prev = hash_insert(&supt->page_map, &spte->elem);
    if (prev == NULL) {
        return true;
    } else {
        free(spte);
        return false;
    }
}

bool vm_supt_install_zeropage(struct supplemental_page_table *supt, void *upage) {
    struct supplemental_page_table_entry *spte = (struct supplemental_page_table_entry *) malloc(sizeof(struct supplemental_page_table_entry));
    spte->upage = upage;
    spte->kpage = NULL;
    spte->status = ALL_ZEROS;
    spte->is_dirty = false;
    
    struct hash_elem *prev = hash_insert(&supt->page_map, &spte->elem);
    if (prev == NULL) {
        return true;
    } else {
        PANIC("Duplicated supt entry for zeropage. ");
        return false;
    }
}

bool vm_supt_set_swap(struct supplemental_page_table *supt, void *page, swap_index_t swap_index) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, page);
    if (spte == NULL) {
        return false;
    }
    spte->kpage = NULL;
    spte->status = ON_SWAP;
    spte->swap_index = swap_index;
    return true;
}

bool vm_supt_install_filesys(struct supplemental_page_table *supt, void *upage, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
    struct supplemental_page_table_entry *spte = (struct supplemental_page_table_entry *) malloc(sizeof(struct supplemental_page_table_entry));
    spte->upage = upage;
    spte->kpage = NULL;
    spte->status = FROM_FILESYS;
    spte->is_dirty = false;
    spte->file = file;
    spte->file_offset = offset;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;
    spte->writable = writable;

    struct hash_elem *prev = hash_insert(&supt->page_map, &spte->elem);
    if (prev == NULL) {
        return true;
    } else {
        PANIC("Duplicated supt entry for filesys-page. ");
        return false;
    }
}

struct supplemental_page_table_entry *vm_supt_find(struct supplemental_page_table *supt, void *page) {
    struct supplemental_page_table_entry spte;
    spte.upage = page;

    struct hash_elem *elem = hash_find(&supt->page_map, &spte.elem);
    if (elem == NULL) {
        return NULL;
    } else {
        return hash_entry(elem, struct supplemental_page_table_entry, elem);
    }
}

bool vm_supt_has_entry(struct supplemental_page_table *supt, void *page) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, page);
    if (spte == NULL) {
        return false;
    } else {
        return true;
    }
}

bool vm_supt_set_dirty(struct supplemental_page_table *supt, void *page, bool value) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, page);
    if (spte == NULL) {
        PANIC("Request page does not exist. ");
    }
    spte->is_dirty |= value;
    return true;
}

bool vm_load_page(struct supplemental_page_table *supt, uint32_t *pagedir, void *upage) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, upage);
    if (spte == NULL) {
        return false;
    }
    if (spte->status == ON_FRAME) {
        return true;
    }
    
    void *frame_page = vm_frame_alloc(PAL_USER, upage);
    if (frame_page == NULL) {
        return false;
    }

    bool writable = true;
    switch (spte->status) {
        case ALL_ZEROS:
            memset(frame_page, 0, PGSIZE);
            break;
        case ON_FRAME:
            break;
        case ON_SWAP:
            vm_swap_in(spte->swap_index, frame_page);
            break;
        case FROM_FILESYS:
            if (vm_load_page_from_filesys(spte, frame_page) == false) {
                vm_frame_free(frame_page);
                return false;
            }
            writable = spte->writable;
            break;
        default:
            PANIC("Invalid status. ");
    }

    if (!pagedir_set_page(pagedir, upage, frame_page, writable)) {
        vm_frame_free(frame_page);
        return false;
    }

    spte->kpage = frame_page;
    spte->status = ON_FRAME;

    pagedir_set_dirty(pagedir, frame_page, false);
    vm_frame_unpin(frame_page);
    return true;
}

bool vm_supt_munmap(struct supplemental_page_table *supt, uint32_t *pagedir, void *page, struct file *file, off_t offset, size_t bytes) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, page);
    if (spte == NULL) {
        PANIC("Some page is missing. ");
    }
    if (spte->status == ON_FRAME) {
        ASSERT(spte->kpage != NULL);
        vm_frame_pin(spte->kpage);
    }

    bool is_dirty;
    switch (spte->status) {
        case ON_FRAME:
            ASSERT(spte->kpage != NULL);
            is_dirty = spte->is_dirty | pagedir_is_dirty(pagedir, spte->upage) | pagedir_is_dirty(pagedir, spte->kpage);
            if (is_dirty) {
                file_write_at(file, spte->upage, bytes, offset);
            }
            vm_frame_free(spte->kpage);
            pagedir_clear_page(pagedir, spte->upage);
            break;
        case ON_SWAP:
            is_dirty = spte->is_dirty | pagedir_is_dirty(pagedir, spte->upage);
            if (is_dirty) {
                void *tmp = palloc_get_page(0);
                vm_swap_in(spte->swap_index, tmp);
                file_write_at(file, tmp, PGSIZE, offset);
                palloc_free_page(tmp);
            } else {
                vm_swap_free(spte->swap_index);
            }
            break;
        case FROM_FILESYS:
            break;
        default:
            PANIC("Invalid status. ");
    }

    hash_delete(&supt->page_map, &spte->elem);
    return true;
}

void vm_pin_page(struct supplemental_page_table *supt, void *page) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, page);
    if (spte == NULL) {
        return;
    }
    ASSERT(spte->status == ON_FRAME);
    vm_frame_pin(spte->kpage);
}

void vm_unpin_page(struct supplemental_page_table *supt, void *page) {
    struct supplemental_page_table_entry *spte = vm_supt_find(supt, page);
    if (spte == NULL) {
        PANIC("Request page does not exist. ");
    }
    if (spte->status == ON_FRAME) {
        vm_frame_unpin(spte->kpage);
    }
}
