#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

/* Only one thread may be inside the file system at a time */
extern struct lock filesys_lock;

void syscall_init (void);
void syscall_close_files (void);

#endif /* userprog/syscall.h */
