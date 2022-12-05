#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include "vm/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

typedef int pid_t;

void halt(void);
void exit(int);
tid_t fork(const char *, struct intr_frame *);
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
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
bool chdir(const char *);
bool mkdir(const char *);
bool readdir(int, char *);
bool isdir(int);
int inumber(fd);

void
is_valid_vaddr(void *addr){
	struct thread *curr = thread_current();

	#ifdef VM
	/* For project 3 and later. */
	if(is_kernel_vaddr(addr)){
		exit(-1);
	}
	struct page *page = spt_find_page(&curr->spt, addr);
	if(page == NULL){
		exit(-1);
	}
	else{
		return true;
	}
	#endif

	if(addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(curr->pml4, addr) == NULL){
		exit(-1);
	}
	else{
		return true;
	}
}

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&file_lock); // Initialize file_lock
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	uint64_t syscall_num = f->R.rax;
	uint64_t args[] = {f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8, f->R.r9};

	#ifdef VM
	thread_current()->rsp_stack = f->rsp;
	#endif

	//printf("syscall : %d\n", syscall_num);
	switch (syscall_num)
	{
		case SYS_HALT: //0
			halt();
			break;
		case SYS_EXIT: //1
			exit(args[0]);
			break;
		case SYS_FORK: //2
			f->R.rax = fork(args[0], f);
			break;
		case SYS_EXEC: //3
			if (exec(args[0]) == -1)
				exit(-1);
			break;
		case SYS_WAIT: //4
			f->R.rax = wait(args[0]);
			break;
		case SYS_CREATE: //5
			f->R.rax = create(args[0], args[1]);
			break;
		case SYS_REMOVE: //6
			f->R.rax = remove(args[0]);
			break;
		case SYS_OPEN: //7
			f->R.rax = open(args[0]);
			break;
		case SYS_FILESIZE: //8
			f->R.rax = filesize(args[0]);
			break;
		case SYS_READ: //9
			f->R.rax = read(args[0], args[1], args[2]);
			break;
		case SYS_WRITE: //10
			f->R.rax = write(args[0], args[1], args[2]);
			break;
		case SYS_SEEK: //11
			seek(args[0], args[1]);
			break;
		case SYS_TELL: //12
			tell(args[0]);
			break;
		case SYS_CLOSE: //13
			close(args[0]);
			break;
		case SYS_MMAP: //14
			f->R.rax = mmap(args[0], args[1], args[2] ,args[3] ,args[4]);
			break;
		case SYS_MUNMAP: //15
			break;
		case SYS_CHDIR: //16
			break;
		case SYS_MKDIR: //16
			break;
		case SYS_READDIR: //18
			break;
		case SYS_ISDIR: //19
			break;
		case SYS_INUMBER: //20
			break;
		case SYS_SYMLINK: //21
			break;
		default:
			exit(-1);
	}
}

/* Project 2 system calls */

void halt (void) {
	power_off();
}

void exit (int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

tid_t
fork (const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}

int
exec (const char *cmd_line) {
	struct thread *curr = thread_current();
	is_valid_vaddr(cmd_line);

	char *cmd_copy = palloc_get_page(PAL_ZERO);
	strlcpy(cmd_copy, cmd_line, strlen(cmd_line) + 1);

	if(process_exec(cmd_copy) == -1){
		return -1;
	}
	NOT_REACHED();
}

int
wait (pid_t pid) {
	return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size) {
	is_valid_vaddr(file);
	lock_acquire(&file_lock);
	bool ret = filesys_create(file, initial_size);
	lock_release(&file_lock);
	return ret;
}

bool
remove (const char *file) {
	is_valid_vaddr(file);
	return filesys_remove(file);
}

int
open (const char *file) {
	is_valid_vaddr(file); //terminate with -1 if invalid pointer

	lock_acquire(&file_lock);
	struct file *f = filesys_open(file);
	lock_release(&file_lock);

	if(f == NULL){
		return -1;
	}
	
	struct thread *curr = thread_current();
	int fid = 2;
	struct file **ft = curr->fdt;

	for(fid; fid < FD_LIMIT; fid++)
	{
		if(ft[fid] == NULL)
		{
			ft[fid] = f;
			break;
		}
	}

	if(fid == FD_LIMIT){
		return -1;
	}

	return fid;
}

int
filesize (int fd) {
	if((fd < 0) || (fd >= FD_LIMIT)){
		return -1;
	}
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;
	if(fdt[fd] == NULL){
		return -1;
	}
	return file_length(fdt[fd]);
}

int
read (int fd, void *buffer, unsigned length) {
	is_valid_vaddr(buffer);
	is_valid_vaddr(buffer + length - 1);
	int size;

	struct page *page = spt_find_page(&thread_current()->spt, buffer);
	if((fd < 0) || (fd >= FD_LIMIT) || (fd == STDOUT_FILENO)){
		return -1;
	}
	else if(fd == STDIN_FILENO){ // see device/input.c and intq.c, lock is already taken
		char byte;
		unsigned char *buf = buffer;
		for(size = 0; size < length; size++){
			byte = input_getc();
			buf[size] = byte;
			if(byte == "\0"){
				return size;
			}
		}
	}
	else if(page->writable == false)
	{
		exit(-1);
	}
	else{
		struct thread *curr = thread_current();
		struct file **fdt = curr->fdt;

		if(fdt[fd] == NULL){
			return -1;
		}
		lock_acquire(&file_lock);
		size = file_read(fdt[fd], buffer, length);
		lock_release(&file_lock);
	}
	return size;
}

int 
write (int fd, const void *buffer, unsigned length) {
	is_valid_vaddr(buffer);
	int size;
	if((fd < 0) || (fd >= 128) || (fd == STDIN_FILENO)){
		return -1;
	}
	else if(fd == STDOUT_FILENO){ // see lib/kernel/console.c, lock is already taken
		putbuf(buffer, length);
		size = length;
	}
	else{
		struct thread *curr = thread_current();
		struct file **fdt = curr->fdt;
		if(fdt[fd] == NULL){
			return -1;
		}
		lock_acquire(&file_lock);
		size = file_write(fdt[fd], buffer, length);
		lock_release(&file_lock);
	}
	return size;
}

void
seek (int fd, unsigned position) {
	if((fd < 2) || (fd >= FD_LIMIT)){
		return;
	}
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;
	if(fdt[fd] == NULL){
		return;
	}
	file_seek(fdt[fd], position);
}

unsigned
tell (int fd) {
	if((fd < 2) || (fd >= FD_LIMIT)){
		return -1;
	}
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;
	if(fdt[fd] == NULL){
		return -1;
	}
	file_tell(fd);
}

void
close (int fd) {
	if((fd < 0) || (fd >= FD_LIMIT)){
		return;
	}
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;
	if(fdt[fd] == NULL){
		return;
	}
	fdt[fd] = NULL;
	file_close(fdt[fd]);
}

void
*mmap(void *addr, size_t length, int writable, int fd, off_t offset) {
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;
	struct file *file = fdt[fd];

	if (!(uint64_t)addr || !is_user_vaddr(addr))
		return NULL;
	if (!is_user_vaddr((uint64_t)addr + length - 1))
		return NULL;
	if ((uint64_t)addr % PGSIZE != 0)
		return NULL;
	if (offset % PGSIZE != 0)
		return NULL;
	if (length == 0)
		return NULL;
	for (uint64_t i = 0; i < length; i += PGSIZE)
	{
		void *upage = (void *)((uint64_t)addr + i);
		if (spt_find_page(&curr->spt, upage) != NULL)
			return NULL;
	}
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
		return NULL;
	if (file == NULL)
		return NULL;

	return do_mmap(addr, length, writable, file, offset);
}

void
munmap(void *addr) {
	do_munmap(addr);
}

bool 
chdir(const char *dir){
	/* temp code */
	return true;
}

bool
mkdir(const char *dir){
	/* temp code */
	return true;
}

bool
readdir(int fd, char *name){
	/* temp code */
	return true;
}

bool
isdir(int fd){
	/* temp code */
	return true;
}

int
inumber(int fd){
	/* temp code */
	return 0;
}