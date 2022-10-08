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

void
is_valid_vaddr(void *addr){
	struct thread *curr = thread_current();
	if(is_user_vaddr(addr) && (addr != NULL) && (pml4_get_page(curr->pml4, addr) != NULL)){
		return;
	}
	else{
		exit(-1);
	}
}

void
syscall_init (void) {
	lock_init(&file_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	uint64_t syscall_num = f->R.rax;
	uint64_t args[] = {f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8, f->R.r9};
	while(1){
		printf("asdf");
	}
	switch (syscall_num)
	{
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(args[0]);
			break;
		case SYS_FORK:
			fork(args[0]);
			break;
		case SYS_EXEC:
			exec(args[0]);
			break;
		case SYS_WAIT:
			wait(args[0]);
			break;
		case SYS_CREATE:
			create(args[0], args[1]);
			break;
		case SYS_REMOVE:
			remove(args[0]);
			break;
		case SYS_OPEN:
			open(args[0]);
			break;
		case SYS_FILESIZE:
			filesize(args[0]);
			break;
		case SYS_READ:
			read(args[0], args[1], args[2]);
			break;
		case SYS_WRITE:
			write(args[0], args[1], args[2]);
			break;
		case SYS_SEEK:
			seek(args[0], args[1]);
			break;
		case SYS_TELL:
			tell(args[0]);
			break;
		case SYS_CLOSE:
			close(args[0]);
			break;
		default:
			exit(-1);
	}
	exit(-1);
}

/* Project 2 system calls */
void
halt (void) {
	power_off();
}

void
exit (int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	thread_exit();
}

pid_t
fork (const char *thread_name) {
	struct intr_frame *tf = &thread_current()->tf;
	return process_fork(thread_name, tf);
}

int
exec (const char *cmd_line) {
	is_valid_vaddr(cmd_line);
	if(process_exec(cmd_line) == -1){
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
	return filesys_create(file, initial_size);
}

bool
remove (const char *file) {
	is_valid_vaddr(file);
	return filesys_remove(file);
}

int
open (const char *file) {
	is_valid_vaddr(file);
	struct file *f = filesys_open(file);
	if(f == NULL){
		return -1;
	}
	struct thread *curr = thread_current();
	int fd = curr->fdt_idx;
	struct file **fdt = curr->fdt;
	while((fd < FD_LIMIT) && (fdt[fd] != NULL)){
		fd++;
	} 
	if(fd == FD_LIMIT){
		return -1;
	} 
	return fd;
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
	else{
		struct thread *curr = thread_current();
		struct file **fdt = curr->fdt;
		if(fdt[fd] == NULL){
			return -1;
		}
		lock_acquire(file_lock);
		size = file_read(fdt[fd], buffer, length);
		lock_release(file_lock);
	}
	return size;
}

int
write (int fd, const void *buffer, unsigned length) {
	is_valid_vaddr(buffer);
	int size;
	if((fd < 0) || (fd >= FD_LIMIT) || (fd == STDIN_FILENO)){
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
		lock_acquire(file_lock);
		size = file_write(fdt[fd], buffer, length);
		lock_release(file_lock);
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
	file_close(fd);
}
