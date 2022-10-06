#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

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
syscall_init (void) {
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
	/*
	uint64_t syscall_num = f->R.rax;
	uint64_t args[] = {f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8, f->R.r9};
	
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

	}
	*/
	printf ("system call!\n");
	thread_exit ();
}

/* Project 2 system calls */
/*
void halt (void) {
	/* Terminates PintOS 
	power_off();
}
void exit (int status) {
	/* Terminates the current user program 
	thread_exit();
}
pid_t fork (const char *thread_name) {
	
}
int exec (const char *cmd_line) {
	struct thread
}
int wait (pid_t) {

}
bool create (const char *file, unsigned initial_size) {

}
bool remove (const char *file) {

}
int open (const char *file) {

}
int filesize (int fd) {

}
int read (int fd, void *buffer, unsigned length) {

}
int write (int fd, const void *buffer, unsigned length) {

}
void seek (int fd, unsigned position) {

}
unsigned tell (int fd) {

}
void close (int fd) {

}
*/
