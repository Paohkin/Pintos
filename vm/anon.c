/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "bitmap.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Project 3*/
//#define ERROR_IDX UINT16_MAX
struct bitmap *swap_table;
const size_t SEC_PPAGE = PGSIZE / DISK_SECTOR_SIZE;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	size_t swap_size = disk_size(swap_disk) / SEC_PPAGE;
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	//struct uninit_page *uninit = &page->uninit;
	//memset(uninit, 0, sizeof(struct uninit_page));
	//struct file_information *file_inf;
	//printf("uninit type: %d\n", page->uninit.type);
	//printf("error idx : %d\n", ERROR_IDX);
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	anon_page->prt = thread_current();
	anon_page->idx = ERROR_IDX;
	//printf("anon init va: %x\n", (uint64_t)page->va);
	//printf("dlrp dksehlsekrh?\n");
	//printf("anon idx : %d\n", anon_page->idx);
	//printf("uninit type: %d\n", page->uninit.type);
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	//size_t sec_ppage = PGSIZE / DISK_SECTOR_SIZE;
	disk_sector_t idx_sector;
	//printf("anon swap va: %x\n", page->va);
	//printf("unitit type 2 : %d\n", page->uninit.type);
	if (anon_page->idx == 100000)
		return false;
	
	for (int i = 0; i < SEC_PPAGE; i++)
	{
		idx_sector = (disk_sector_t)(anon_page->idx * SEC_PPAGE) + 1;
		off_t ofs = i * DISK_SECTOR_SIZE;
		disk_read(swap_disk, idx_sector, kva+ofs);
	}
	bitmap_set(swap_table, anon_page->idx, false);
	anon_page->idx = 100000;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t idx = bitmap_scan_and_flip(swap_table, 0, 1, false);
	//size_t sec_ppage = PGSIZE / DISK_SECTOR_SIZE;
	disk_sector_t idx_sector;
	
	for (int i = 0; i < SEC_PPAGE; i++)
	{
		idx_sector = (disk_sector_t)(idx * SEC_PPAGE) + i;
		off_t ofs = i * DISK_SECTOR_SIZE;
		disk_write(swap_disk, idx_sector, page->frame->kva + ofs);
	}

	anon_page->idx = idx;

	pml4_clear_page(anon_page->prt->pml4, page->va);
	pml4_set_dirty(anon_page->prt->pml4, page->va, false);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	//printf("anon destory\n");
}
