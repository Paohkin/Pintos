/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* Project 3 */
static struct list mmap_list;
static bool lazy_load_segment (struct page *page, void *aux);

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
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	file_close(file_page->file);
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
		vm_alloc_page_with_initializer(VM_FILE, upage, writable, lazy_load_segment, inf);
	}

	struct mmap_information *minf = malloc(sizeof(struct mmap_information));
	minf->begin = (uint64_t)addr;
	minf->end = (uint64_t)pg_round_down((uint64_t)addr + length);
	list_push_back(&mmap_list, &minf->elem);

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct list_elem *e;

	for (e = list_begin(&mmap_list); e != list_end(&mmap_list); e = list_next(e))
	{
		struct mmap_information *minf = list_entry(e, struct mmap_information, elem);
		if (minf->begin == (uint64_t)addr)
		{
			for (uint64_t i = (uint64_t)addr; i < minf->end; i += PGSIZE)
			{
				struct page *page = spt_find_page(&thread_current()->spt, (void *)i);
				spt_remove_page(&thread_current()->spt, page);
			}
			list_remove(&minf->elem);
			free(minf);
		}
	}
}

/* Project 3 */
static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct file *file = ((struct file_information *)aux)->file;
	off_t offset = ((struct file_information *)aux)->ofs;
	size_t page_read_bytes = ((struct file_information *)aux)->read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;
	file_seek(file, offset);

	if(file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes){
		palloc_free_page(page->frame->kva);
		return false;
	}
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}