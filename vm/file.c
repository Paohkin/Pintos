/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* Project 3 */
static struct list mmap_list;
static bool lazy_load_file (struct page *page, void *aux);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	list_init(&mmap_list);
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
	struct file *file = ((struct file_information *)page->uninit.aux)->file;
	file_page->file = file;
	
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	file_seek(file_page->file, file_page->ofs);
	off_t read_bytes = file_read(file_page->file, kva, file_page->size); 
	memset(kva+read_bytes, 0, PGSIZE-read_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread *curr = thread_current();

	if (pml4_is_dirty(curr->pml4, page->va))
	{
		file_seek(file_page->file, file_page->ofs);
		file_write(file_page->file, page->va, file_page->size);
		pml4_set_dirty(curr->pml4, page->va, false);
	}

	pml4_clear_page(curr->pml4, page->va);
	page->frame = NULL;

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread *curr = thread_current();

	if (pml4_is_dirty(curr->pml4, page->va))
	{
		file_seek(file_page->file, file_page->ofs);
		file_write(file_page->file, page->va, file_page->size);
	}
	file_close(file_page->file);
	if (page->frame != NULL)
	{
		list_remove(&page->frame->frame_elem);
		list_remove(&page->elem);
		free(page->frame);
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	off_t ofs;
	uint64_t read_bytes;

	for (uint64_t i = 0; i < length; i += PGSIZE)
	{
		struct file_information *inf = malloc(sizeof(struct file_information));
		ofs = offset + i;
		
		if (length - i > PGSIZE)
			read_bytes = PGSIZE;
		else
			read_bytes = length - i;
		inf->file = file_reopen(file);
		inf->ofs = ofs;
		inf->read_bytes = read_bytes;
		void *upage = (void *)((uint64_t)addr + i);
		vm_alloc_page_with_initializer(VM_FILE, upage, writable, lazy_load_file, (void *)inf);
	}

	struct mmap_information *minf = malloc(sizeof(struct mmap_information));
	minf->begin = (uint64_t)addr;
	minf->end = (uint64_t)pg_round_down((uint64_t)addr+length-1);
	list_push_back(&mmap_list, &minf->elem);

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct list_elem *e;
	//printf("munmap addr: %x\n", (uint64_t)addr);
	e = list_begin(&mmap_list);

	while (e != list_end(&mmap_list))
	{
		struct mmap_information *minf = list_entry(e, struct mmap_information, elem);
		if (minf->begin == (uint64_t)addr)
		{	
			//printf("end: %x\n", minf->end);
			for (uint64_t i = (uint64_t)addr; i <= minf->end; i += PGSIZE)
			{
				struct thread *curr = thread_current();
				struct page *page = spt_find_page(&curr->spt, (void *)i);
				spt_remove_page(&curr->spt, page);
				pml4_clear_page(curr->pml4, (void *)i);
			}
			e = list_remove(e);
		}
		else
			e = list_next(e);
	}
}

/* Project 3 */
static bool
lazy_load_file (struct page *page, void *aux) {
	struct file_information *inf = (struct file_information *)aux;

	file_seek(inf->file, inf->ofs);
	page->file.size = file_read(inf->file, page->va, inf->read_bytes);
	page->file.ofs = inf->ofs;

	if (page->file.size != PGSIZE)
		memset(page->va + page->file.size, 0, PGSIZE - page->file.size);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	free(inf);

	return true;
}