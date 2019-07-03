#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"
#ifdef VM
#include "vm/page.h"
#endif

static void syscall_handler (struct intr_frame *);

static void check_user (const uint8_t *uaddr);
static int32_t get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int memread_user (void *src, void *des, size_t bytes);

static struct file_desc* find_file_desc(struct thread *, int fd);

void sys_halt (void);
void sys_exit (int);
pid_t sys_exec (const char *cmdline);
int sys_wait (pid_t pid);

bool sys_create(const char* filename, unsigned initial_size);
bool sys_remove(const char* filename);
int sys_open(const char* file);
int sys_filesize(int fd);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);

#ifdef VM
mmapid_t sys_mmap(int, void *);
bool sys_munmap(mmapid_t);

static struct mmap_desc *find_mmap_desc(struct thread *, mmapid_t);

void preload_and_pin_pages(const void *, size_t);
void unpin_preloaded_pages(const void *, size_t);
#endif

struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// in case of invalid memory access, fail and exit.
static void fail_invalid_access(void) {
  if (lock_held_by_current_thread(&filesys_lock))
    lock_release (&filesys_lock);

  sys_exit (-1);
  NOT_REACHED();
}

static void
syscall_handler (struct intr_frame *f)
{
  int syscall_number;

  ASSERT( sizeof(syscall_number) == 4 ); // assuming x86

  memread_user(f->esp, &syscall_number, sizeof(syscall_number));

  switch (syscall_number) {
  case SYS_HALT: // 0
    {
      sys_halt();
      NOT_REACHED();
      break;
    }

  case SYS_EXIT: // 1
    {
      int exitcode;
      memread_user(f->esp + 4, &exitcode, sizeof(exitcode));

      sys_exit(exitcode);
      NOT_REACHED();
      break;
    }

  case SYS_EXEC: // 2
    {
      void* cmdline;
      memread_user(f->esp + 4, &cmdline, sizeof(cmdline));

      int return_code = sys_exec((const char*) cmdline);
      f->eax = (uint32_t) return_code;
      break;
    }

  case SYS_WAIT: // 3
    {
      pid_t pid;
      memread_user(f->esp + 4, &pid, sizeof(pid_t));

      int ret = sys_wait(pid);
      f->eax = (uint32_t) ret;
      break;
    }

  case SYS_CREATE: // 4
    {
      const char* filename;
      unsigned initial_size;
      bool return_code;

      memread_user(f->esp + 4, &filename, sizeof(filename));
      memread_user(f->esp + 8, &initial_size, sizeof(initial_size));

      return_code = sys_create(filename, initial_size);
      f->eax = return_code;
      break;
    }

  case SYS_REMOVE: // 5
    {
      const char* filename;
      bool return_code;

      memread_user(f->esp + 4, &filename, sizeof(filename));

      return_code = sys_remove(filename);
      f->eax = return_code;
      break;
    }

  case SYS_OPEN: // 6
    {
      const char* filename;
      int return_code;

      memread_user(f->esp + 4, &filename, sizeof(filename));

      return_code = sys_open(filename);
      f->eax = return_code;
      break;
    }

  case SYS_FILESIZE: // 7
    {
      int fd, return_code;
      memread_user(f->esp + 4, &fd, sizeof(fd));

      return_code = sys_filesize(fd);
      f->eax = return_code;
      break;
    }

  case SYS_READ: // 8
    {
      int fd, return_code;
      void *buffer;
      unsigned size;

      memread_user(f->esp + 4, &fd, sizeof(fd));
      memread_user(f->esp + 8, &buffer, sizeof(buffer));
      memread_user(f->esp + 12, &size, sizeof(size));

      return_code = sys_read(fd, buffer, size);
      f->eax = (uint32_t) return_code;
      break;
    }

  case SYS_WRITE: // 9
    {
      int fd, return_code;
      const void *buffer;
      unsigned size;

      memread_user(f->esp + 4, &fd, sizeof(fd));
      memread_user(f->esp + 8, &buffer, sizeof(buffer));
      memread_user(f->esp + 12, &size, sizeof(size));

      return_code = sys_write(fd, buffer, size);
      f->eax = (uint32_t) return_code;
      break;
    }

  case SYS_SEEK: // 10
    {
      int fd;
      unsigned position;

      memread_user(f->esp + 4, &fd, sizeof(fd));
      memread_user(f->esp + 8, &position, sizeof(position));

      sys_seek(fd, position);
      break;
    }

  case SYS_TELL: // 11
    {
      int fd;
      unsigned return_code;

      memread_user(f->esp + 4, &fd, sizeof(fd));

      return_code = sys_tell(fd);
      f->eax = (uint32_t) return_code;
      break;
    }

  case SYS_CLOSE: // 12
    {
      int fd;
      memread_user(f->esp + 4, &fd, sizeof(fd));

      sys_close(fd);
      break;
    }

#ifdef VM
  case SYS_MMAP:
  {
    int fd;
    void *addr;
    memread_user(f->esp + 4, &fd, sizeof(fd));
    memread_user(f->esp + 8, &addr, sizeof(addr));

    mmapid_t ret = sys_mmap(fd, addr);
    f->eax = ret;
    break;
  }

  case SYS_MUNMAP:
  {
    mmapid_t mid;
    memread_user(f->esp + 4, &mid, sizeof(mid));

    sys_munmap(mid);
    break;
  }
#endif


  default:
    printf("[ERROR] system call %d is unimplemented!\n", syscall_number);
    sys_exit(-1);
    break;
  }

}

void sys_halt(void) {
  shutdown_power_off();
}

void sys_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);

  struct PCB *pcb = thread_current()->pcb;
  if(pcb != NULL) {
    pcb->exited = true;
    pcb->exitcode = status;
  }
  thread_exit();
}

pid_t sys_exec(const char *cmdline) {

  check_user((const uint8_t*) cmdline);
  char *tmp = cmdline;
  while (*tmp != '\0') {
    tmp++;
    check_user((const uint8_t*) tmp);
  }

  lock_acquire (&filesys_lock);
  pid_t pid = process_execute(cmdline);
  lock_release (&filesys_lock);
  return pid;
}

int sys_wait(pid_t pid) {
  return process_wait(pid);
}

bool sys_create(const char* filename, unsigned initial_size) {
  bool return_code;

  check_user((const uint8_t*) filename);

  lock_acquire (&filesys_lock);
  return_code = filesys_create(filename, initial_size);
  lock_release (&filesys_lock);
  return return_code;
}

bool sys_remove(const char* filename) {
  bool return_code;
  check_user((const uint8_t*) filename);

  lock_acquire (&filesys_lock);
  return_code = filesys_remove(filename);
  lock_release (&filesys_lock);
  return return_code;
}

int sys_open(const char* file) {
  check_user((const uint8_t*) file);

  struct file* file_opened;
  struct file_desc* fd = palloc_get_page(0);
  if (!fd) {
    return -1;
  }

  lock_acquire (&filesys_lock);
  file_opened = filesys_open(file);
  if (!file_opened) {
    palloc_free_page (fd);
    lock_release (&filesys_lock);
    return -1;
  }

  fd->file = file_opened;

  struct list* fd_list = &thread_current()->file_descriptors;
  if (list_empty(fd_list)) {
    fd->id = 3;
  }
  else {
    fd->id = (list_entry(list_back(fd_list), struct file_desc, elem)->id) + 1;
  }
  list_push_back(fd_list, &(fd->elem));

  lock_release (&filesys_lock);
  return fd->id;
}

int sys_filesize(int fd) {
  struct file_desc* file_d;

  lock_acquire (&filesys_lock);
  file_d = find_file_desc(thread_current(), fd);

  if(file_d == NULL) {
    lock_release (&filesys_lock);
    return -1;
  }

  int ret = file_length(file_d->file);
  lock_release (&filesys_lock);
  return ret;
}

void sys_seek(int fd, unsigned position) {
  lock_acquire (&filesys_lock);
  struct file_desc* file_d = find_file_desc(thread_current(), fd);

  if(file_d && file_d->file) {
    file_seek(file_d->file, position);
  }
  else
    return;

  lock_release (&filesys_lock);
}

unsigned sys_tell(int fd) {
  lock_acquire (&filesys_lock);
  struct file_desc* file_d = find_file_desc(thread_current(), fd);

  unsigned ret;
  if(file_d && file_d->file) {
    ret = file_tell(file_d->file);
  }
  else
    ret = -1;

  lock_release (&filesys_lock);
  return ret;
}

void sys_close(int fd) {
  lock_acquire (&filesys_lock);
  struct file_desc* file_d = find_file_desc(thread_current(), fd);
  if(file_d && file_d->file) {
    file_close(file_d->file);
    list_remove(&(file_d->elem));
    palloc_free_page(file_d);
  }
  lock_release (&filesys_lock);
}

void sys_file_close(struct file* file){
  lock_acquire (&filesys_lock);
  file_close(file);
  lock_release (&filesys_lock);
}

int sys_read(int fd, void *buffer, unsigned size) {
  check_user((const uint8_t*) buffer);
  check_user((const uint8_t*) buffer + size - 1);

  lock_acquire (&filesys_lock);
  int ret;

  if(fd == 0) {
    unsigned i;
    for(i = 0; i < size; ++i) {
      if(! put_user(buffer + i, input_getc()) ) {
        lock_release (&filesys_lock);
        sys_exit(-1);
      }
    }
    ret = size;
  }
  else {
    struct file_desc* file_d = find_file_desc(thread_current(), fd);

    if(file_d && file_d->file) {
#ifdef VM
      preload_and_pin_pages(buffer, size);
#endif

      ret = file_read(file_d->file, buffer, size);

#ifdef VM
      unpin_preloaded_pages(buffer, size);
#endif
    }
    else
      ret = -1;
  }

  lock_release (&filesys_lock);
  return ret;
}

int sys_write(int fd, const void *buffer, unsigned size) {
  check_user((const uint8_t*) buffer);
  check_user((const uint8_t*) buffer + size - 1);

  lock_acquire (&filesys_lock);
  int ret;

  if(fd == 1) {
    putbuf(buffer, size);
    ret = size;
  }
  else {
    struct file_desc* file_d = find_file_desc(thread_current(), fd);
    if(file_d && file_d->file) {
#ifdef VM
      preload_and_pin_pages(buffer, size);
#endif

      ret = file_write(file_d->file, buffer, size);

#ifdef VM
      unpin_preloaded_pages(buffer, size);
#endif
    }
    else
      ret = -1;
  }

  lock_release (&filesys_lock);
  return ret;
}

static void
check_user (const uint8_t *uaddr) {
  if(get_user (uaddr) == -1)
    fail_invalid_access();
#ifdef VM
  struct supplemental_page_table_entry* base = vm_supt_find(thread_current()->supt, pg_round_down(uaddr));
  if (base == NULL){
    fail_invalid_access();
  }
#endif
}

static int32_t
get_user (const uint8_t *uaddr) {
  if (((void*)uaddr > PHYS_BASE)) {
    return -1;
  }

  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte) {
  if (! ((void*)udst < PHYS_BASE)) {
    return false;
  }

  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


static int
memread_user (void *src, void *dst, size_t bytes)
{
  int32_t value;
  size_t i;
  for(i=0; i<bytes; i++) {
    value = get_user(src + i);
    if(value == -1) // segfault or invalid memory access
      fail_invalid_access();

    *(char*)(dst + i) = value & 0xff;
  }
  return (int)bytes;
}

static struct file_desc*
find_file_desc(struct thread *t, int fd)
{
  ASSERT (t != NULL);

  if (fd < 3) {
    return NULL;
  }

  struct list_elem *e;

  if (! list_empty(&t->file_descriptors)) {
    for(e = list_begin(&t->file_descriptors);
        e != list_end(&t->file_descriptors); e = list_next(e))
    {
      struct file_desc *desc = list_entry(e, struct file_desc, elem);
      if(desc->id == fd) {
        return desc;
      }
    }
  }

  return NULL; // not found
}

#ifdef VM
mmapid_t sys_mmap(int fd, void *upage) {
  if (upage == NULL || pg_ofs(upage) != 0) {
    return -1;
  }
  if (fd <= 1) {
    return -1;
  }
  
  struct thread *curr = thread_current();

  lock_acquire(&filesys_lock);

  struct file *f = NULL;
  struct file_desc *file_d = find_file_desc(thread_current(), fd);
  if (file_d && file_d->file) {
    f = file_reopen(file_d->file);
  }
  if (f == NULL) {
    goto MMAP_FAIL;
  }

  size_t file_size = file_length(f);
  if (file_size == 0) {
    goto MMAP_FAIL;
  }

  size_t offset;
  for (offset = 0; offset < file_size; offset += PGSIZE) {
    void *addr = upage + offset;
    if (vm_supt_has_entry(curr->supt, addr)) {
      goto MMAP_FAIL;
    }
  }
  
  for (offset = 0; offset < file_size; offset += PGSIZE) {
    void *addr = upage + offset;

    size_t read_bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
    size_t zero_bytes = PGSIZE - read_bytes;

    vm_supt_install_filesys(curr->supt, addr, f, offset, read_bytes, zero_bytes, true);
  }

  mmapid_t mid;
  if (!list_empty(&curr->mmap_list)) {
    mid = list_entry(list_back(&curr->mmap_list), struct mmap_desc, elem)->id + 1;
  } else {
    mid = 1;
  }

  struct mmap_desc *mmap_d = (struct mmap_desc *) malloc(sizeof(struct mmap_desc));
  mmap_d->id = mid;
  mmap_d->file = f;
  mmap_d->addr = upage;
  mmap_d->size = file_size;
  list_push_back(&curr->mmap_list, &mmap_d->elem);

  lock_release(&filesys_lock);
  
  return mid;


MMAP_FAIL:
  lock_release(&filesys_lock);
  return -1;
}

bool sys_munmap(mmapid_t mid) {
  struct thread *curr = thread_current();
  struct mmap_desc *mmap_d = find_mmap_desc(curr, mid);

  if (mmap_d == NULL) {
    return false;
  }

  lock_acquire(&filesys_lock);
  
  {
    size_t offset, file_size = mmap_d->size;
    for (offset = 0; offset < file_size; offset += PGSIZE) {
      void *addr = mmap_d->addr + offset;
      size_t bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
      vm_supt_munmap(curr->supt, curr->pagedir, addr, mmap_d->file, offset, bytes);
    }

    list_remove(&mmap_d->elem);
    file_close(mmap_d->file);
    free(mmap_d);
  }
  
  lock_release(&filesys_lock);

  return true;
}

static struct mmap_desc *find_mmap_desc(struct thread *t, mmapid_t mid) {
  ASSERT(t != NULL);

  struct list_elem *e;

  if (!list_empty(&t->mmap_list)) {
    for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e)) {
      struct mmap_desc *desc = list_entry(e, struct mmap_desc, elem);
      if (desc->id == mid) {
        return desc;
      }
    }
  }

  return NULL;
}


void preload_and_pin_pages(const void *buffer, size_t size) {
  struct supplemental_page_table *supt = thread_current()->supt;
  uint32_t *pagedir = thread_current()->pagedir;

  void *upage;
  for (upage = pg_round_down(buffer); upage < buffer + size; upage += PGSIZE) {
    vm_load_page(supt, pagedir, upage);
    vm_pin_page(supt, upage);
  }
}

void unpin_preloaded_pages(const void *buffer, size_t size) {
  struct supplemental_page_table *supt = thread_current()->supt;

  void *upage;
  for (upage = pg_round_down(buffer); upage < buffer + size; upage += PGSIZE) {
    vm_unpin_page(supt, upage);
  }
}

#endif
