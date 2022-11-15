/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <string.h>

struct list frame_list;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Project 3*/
static uint64_t hash_func (const struct hash_elem *e, void *aux);
static bool less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	//printf("alloc_page_with type: %d, upage: %x, wr: %d\n", type, (uint64_t)upage, writable);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)malloc(sizeof(struct page));
		//printf("alloc page: %x\n", page);
		if(VM_TYPE(type) == VM_ANON){
			uninit_new(page, upage, init, type, aux, anon_initializer);
		}
		if(VM_TYPE(type) == VM_FILE){
			uninit_new(page, upage, init, type, aux, file_backed_initializer);
		}
		page->writable = writable;
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	// struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page *page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;

	page->va = pg_round_down(va);
	e = hash_find(&spt->spt_hash, &page->elem);
	free(page);

	if(e == NULL){
		return NULL;
	}
	else{
		return hash_entry(e, struct page, elem);
	}
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	// int succ = false;
	/* TODO: Fill this function. */
	struct hash_elem *succ = hash_insert(&spt->spt_hash, &page->elem);
	
	if(succ == NULL){
		return true;
	}
	else{
		return false;
	}
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	//struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct thread *curr = thread_current();
	struct list_elem *e;
	struct frame *victim;
	if(!list_empty(&frame_list)){
		for(e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)){
			victim = list_entry(e, struct thread, elem);
			if(pml4_is_accessed(curr->pml4, victim->page->va)){
				pml4_set_accessed(curr->pml4, victim->page->va, 0);
			}
			else{
				return victim;
			}
		}
	}
	return NULL;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	//struct frame *frame = NULL;
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva = palloc_get_page(PAL_USER);
	if(frame->kva == NULL){
		frame = vm_evict_frame();
		frame->page = NULL;
		return frame;
	}
	frame->page = NULL;
	list_push_back(&frame_list, &frame->frame_elem);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	void *sp = pg_round_down(addr);

	for (void *t = sp; t < USER_STACK; t += PGSIZE)
	{
		if (!vm_alloc_page(VM_ANON+VM_MARKER_0, t, true))
			break;
	}
	vm_claim_page(sp);
	//if(vm_alloc_page_with_initializer(VM_ANON, addr, true, NULL, NULL)){
	//	vm_claim_page(addr);
	//	thread_current()->stack_bottom -= PGSIZE;
	//}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
	void *kva = page->frame->kva;
	page->frame->kva = palloc_get_page(PAL_USER);
	memcpy(page->frame->kva, kva, PGSIZE);
	pml4_set_page(thread_current()->pml4, page->va, page->frame->kva, page->writable);

	return true;
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user UNUSED, bool write UNUSED, bool not_present) {
	// struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	//printf("addr: %x, np: %d, wr: %d, usr: %d\n", (uint64_t)addr, not_present, write, user);
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, addr);
	void *sp = pg_round_down(curr->rsp_stack);
	//printf("handle_fault: %x\n", (uint64_t)addr);
	if(is_kernel_vaddr(addr))
	{
		//printf("is_kernel\n");
		exit(-1);
		kill(f);
	}
	if(!not_present && write && page->writable && page){
		return vm_handle_wp(page);
	}
	else if(!not_present && write && !(page->writable && page)){
		//printf("!not_present\n");
		exit(-1);
		kill(f);
	}
	if ((sp-PGSIZE <= addr) && ((uintptr_t)addr < USER_STACK) && write)
	{
		vm_stack_growth(addr);
		return true;
	}
	if(vm_claim_page(addr)){ // lazy load
		return true;
	}
	/*else{
		void *rsp_stack = is_kernel_vaddr(f->rsp) ? curr->rsp_stack : f->rsp;
		if(rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK){ // stack growth
			vm_stack_growth(curr->stack_bottom - PGSIZE);
			return true;
		}
		else{ // true page fault
			//return false;
			exit(-1);
			kill(f);
		}
	}*/
	//printf("FAIL?\n");
	exit(-1);
	kill(f);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	//struct page *page = NULL;
	/* TODO: Fill this function */
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, va);
	//printf("vm_claim_page va: %x\n", (uint64_t)va);
	if(page == NULL){
		return false;
	}
	else{
		return vm_do_claim_page (page);
	}
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	//printf("vm_do_claim_page\n");
	//printf("claim type: %d\n", page->uninit.type);
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *curr = thread_current();
	//printf("va: %x, kva: %x\n", page->va, frame->kva);
	if(!pml4_set_page (curr->pml4, page->va, frame->kva, page->writable)){
		//printf("false\n");
		return false;
	}
	else{
		//printf("swap\n");
		bool temp = swap_in(page, frame->kva);
		//printf("temp: %d\n", temp);
		return temp;
		//return swap_in(page, frame->kva);
	}
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) {
	struct hash_iterator itr;
	hash_first(&itr, &src->spt_hash);
	//printf("copy start\n");
	ASSERT(dst != NULL);
	ASSERT(src != NULL);
	while (hash_next(&itr))
	{
		//printf("****************************\n");
		struct page *page_src = hash_entry(hash_cur(&itr), struct page, elem);
		enum vm_type type = page_get_type(page_src);
		void *upage = page_src->va;
		bool writable = page_src->writable;
		vm_initializer *init = page_src->uninit.init;
		void *aux = page_src->uninit.aux;
		//printf("src_type: %d, src_uninit_type: %d, src_op_type: %d\n", type, page_src->uninit.type, page_src->operations->type);
		//printf("upage: %x\n", (uint64_t)upage);
		if (page_src->uninit.type - type == VM_MARKER_0)
			//printf("BBLUEAR\n");
			break;
		else if(page_src->operations->type == VM_UNINIT){
			//printf("AKOTTAMUK\n");
			//vm_initializer *init = page_src->uninit.init;
			//void *aux = page_src->uninit.aux;
			if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;
		}
		else{
			//printf("Tlqkf type: %d, upage: %x, wr: %d\n", type, (uint64_t)upage, writable);
			//vm_initializer *init = page_src->uninit.init;
			//void *aux = (void *)page_src->file_inf;
			if(!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;
			if(!vm_claim_page(upage))
				return false;
		}
		//printf("&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
		if(page_src->operations->type != VM_UNINIT)
		{
			//printf("zaza\n");
			struct page *page_dst = spt_find_page(dst, upage);
			memcpy(page_dst->frame->kva, page_src->frame->kva, PGSIZE);
		}
		//printf("-----------------------------\n");
	}
	//printf("dldldldld\n");
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator itr;
	hash_first(&itr, &spt->spt_hash);

	while (hash_next(&itr)){
		struct page *page = hash_entry(hash_cur(&itr), struct page, elem);
		if (page->operations->type == VM_FILE)
			do_munmap(page->va);
	}
	hash_destroy(&spt->spt_hash, NULL);
}

/* Project 3*/
uint64_t 
hash_func (const struct hash_elem *e, void *aux UNUSED){
	struct page *p = hash_entry(e, struct page, elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool 
less_func (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
	struct page *a = hash_entry(a_, struct page, elem);
	struct page *b = hash_entry(b_, struct page, elem);
	return a->va < b->va;
}
