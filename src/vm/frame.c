#include "lib/stddef.h"
#include "lib/stdint.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

static struct lock frame_lock;
static struct hash frame_hash;
static struct list frame_list;
static struct list_elem *clock_ptr;

struct frame_table_entry {
    void *kpage;
    void *upage;
    struct hash_elem h_elem;
    struct list_elem l_elem;
    struct thread *t;
    bool is_pinned;
};

static unsigned frame_hash_func(const struct hash_elem *elem, void *aux UNUSED) {
    struct frame_table_entry *entry = hash_entry(elem, struct frame_table_entry, h_elem);
    return hash_bytes(&entry->kpage, sizeof(entry->kpage));
}

static bool frame_less_func(const struct hash_elem *elem1, const struct hash_elem *elem2, void *aux UNUSED) {
    struct frame_table_entry *entry1 = hash_entry(elem1, struct frame_table_entry, h_elem);
    struct frame_table_entry *entry2 = hash_entry(elem2, struct frame_table_entry, h_elem);
    return entry1->kpage < entry2->kpage;
}

static struct frame_table_entry *clock_frame_next() {
    if (list_empty(&frame_list)) {
        PANIC("Evict rejected, frame table is now empty. ");
    }

    if (clock_ptr == NULL || clock_ptr == list_end(&frame_list)) {
        clock_ptr = list_begin(&frame_list);
    } else {
        clock_ptr = list_next(clock_ptr);
    }

    struct frame_table_entry *entry = list_entry(clock_ptr, struct frame_table_entry, l_elem);
    return entry;
}

static struct frame_table_entry *frame_to_be_evicted(uint32_t *pagedir) {
    size_t n = hash_size(&frame_hash);
    if (n == 0) {
        PANIC("Evict rejected, frame table is now empty. ");
    }
    
    size_t i;
    for (i = 0; i <= n * 2; ++i) {
        struct frame_table_entry *entry = clock_frame_next();
        if (entry->is_pinned) {
            continue;
        } else if (pagedir_is_accessed(pagedir, entry->upage)) {
            pagedir_set_accessed(pagedir, entry->upage, false);
            continue;
        }
        return entry;
    }

    PANIC("Cannot evict any frame. ");
}

static void vm_frame_do_free(void *kpage, bool free_page) {
    ASSERT(lock_held_by_current_thread(&frame_lock) == true);
    ASSERT(is_kernel_vaddr(kpage));
    ASSERT(pg_ofs(kpage) == 0);

    struct frame_table_entry entry;
    entry.kpage = kpage;

    struct hash_elem *elem = hash_find(&frame_hash, &entry.h_elem);

    if (elem == NULL) {
        PANIC("The page to be freed does not exist. ");
    }

    struct frame_table_entry *entry2 = hash_entry(elem, struct frame_table_entry, h_elem);
    hash_delete(&frame_hash, &entry2->h_elem);
    list_remove(&entry2->l_elem);

    if (free_page) {
        palloc_free_page(kpage);
    }
    free(entry2);
}

static void vm_frame_set_pinned(void *kpage, bool value) {
    lock_acquire(&frame_lock);
    
    struct frame_table_entry entry;
    entry.kpage = kpage;

    struct hash_elem *elem = hash_find(&frame_hash, &entry.h_elem);

    if (elem == NULL) {
        PANIC("The frame to be pinned of unpinned does not exist. ");
    }

    struct frame_table_entry *entry2 = hash_entry(elem, struct frame_table_entry, h_elem);
    entry2->is_pinned = value;

    lock_release(&frame_lock);
}

// interfaces

void vm_frame_init() {
    lock_init(&frame_lock);
    hash_init(&frame_hash, frame_hash_func, frame_less_func, NULL);
    list_init(&frame_list);
    clock_ptr = NULL;
}

void *vm_frame_alloc(enum palloc_flags flags, void *upage) {
    lock_acquire(&frame_lock);

    void *frame_page = palloc_get_page(PAL_USER | flags);
    if (frame_page == NULL) {
        struct frame_table_entry *frame_evicted = frame_to_be_evicted(thread_current()->pagedir);

        ASSERT(frame_evicted != NULL && frame_evicted->t != NULL);
        ASSERT(frame_evicted->t->pagedir != (void *) 0xcccccccc);

        pagedir_clear_page(frame_evicted->t->pagedir, frame_evicted->upage);

        bool is_dirty = pagedir_is_dirty(frame_evicted->t->pagedir, frame_evicted->upage) | pagedir_is_dirty(frame_evicted->t->pagedir, frame_evicted->kpage);
        swap_index_t swap_index = vm_swap_out(frame_evicted->kpage);
        vm_supt_set_swap(frame_evicted->t->supt, frame_evicted->upage, swap_index);
        vm_supt_set_dirty(frame_evicted->t->supt, frame_evicted->upage, is_dirty);
        vm_frame_do_free(frame_evicted->kpage, true);

        frame_page = palloc_get_page(PAL_USER | flags);
        ASSERT(frame_page != NULL);
    }

    struct frame_table_entry *frame = malloc(sizeof(struct frame_table_entry));
    if (frame == NULL) {
        lock_release(&frame_lock);
        return NULL;
    }

    frame->t = thread_current();
    frame->upage = upage;
    frame->kpage = frame_page;
    frame->is_pinned = true;

    hash_insert(&frame_hash, &frame->h_elem);
    list_push_back(&frame_list, &frame->l_elem);

    lock_release(&frame_lock);

    return frame_page;
}

void vm_frame_free(void *kpage) {
    lock_acquire(&frame_lock);
    vm_frame_do_free(kpage, true);
    lock_release(&frame_lock);
}

void vm_frame_remove_entry(void *kpage) {
    lock_acquire(&frame_lock);
    vm_frame_do_free(kpage, false);
    lock_release(&frame_lock);
}

void vm_frame_pin(void *kpage) {
    vm_frame_set_pinned(kpage, true);
}

void vm_frame_unpin(void *kpage) {
    vm_frame_set_pinned(kpage, false);
}
