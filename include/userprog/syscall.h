#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

typedef int tid_t;
typedef int pid_t;

void syscall_init (void);
void is_valid_vaddr (void *);
struct lock file_lock;

void halt(void);
void exit(int);
int exec(const char *);
int wait(pid_t);
bool create(const char *, unsigned);
bool remove(const char *);
int open(const char *);
int filesize(int);
int read(int, void *, unsigned);
int write(int, const void *, unsigned);
void seek(int, unsigned);
unsigned tell(int);
void close(int);

#endif /* userprog/syscall.h */
