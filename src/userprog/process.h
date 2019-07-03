#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
typedef int pid_t;
#define PID_ERROR         ((pid_t) -1)
#define PID_INITIALIZING  ((pid_t) -2)

pid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct PCB {

  pid_t pid;

  const char* argv;

  struct list_elem elem;

  bool waiting;
  bool exited;
  bool orphan;
  int32_t exitcode;

  struct semaphore sema_initialization;
  struct semaphore sema_wait;

};

struct file_desc {
  int id;
  struct list_elem elem;
  struct file* file;
};

#ifdef VM
typedef int mmapid_t;

struct mmap_desc {
  mmapid_t id;
  struct list_elem elem;
  struct file* file;

  void *addr;   // where it is mapped to? store the user virtual address
  size_t size;  // file size
};
#endif

#endif /* userprog/process.h */
