#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void is_valid_vaddr (void *);
struct lock file_lock;

#endif /* userprog/syscall.h */
