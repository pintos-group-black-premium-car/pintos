#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "userprog/process.h"
#include <stdbool.h>
#include <debug.h>

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)
#define READDIR_MAX_LEN 14

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);


void syscall_init (void);

void sys_exit (int);

#ifdef VM
bool sys_munmap(mmapid_t);
#endif


#endif /* lib/user/syscall.h */
