/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* Initalize the page on first fault */
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;
	//printf("uninit_init\n");

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;
	//printf("dkdkdkdk type: %d, va: %x, kva: %x\n", uninit->type, (uint64_t)page->va, (uint64_t)kva);
	//init ? printf("Yes\n") : printf("No\n");
	//printf("page: %x, aux: %x\n", &page, &aux);
	/* TODO: You may need to fix this function. */
	bool succ1 = uninit->page_initializer (page, uninit->type, kva);
	//printf("tlqkf type : %d\n", uninit->type);
	//printf("init1 success\n");
	//printf("page: %x\n", page);
	//printf("aux: %x\n", aux);
	ASSERT(page != NULL);
	ASSERT(init ? aux != NULL : true);
	//printf("page: %x\n", page);
	//printf("init : %x\n", init);
	//printf("spt find: %x\n", spt_find_page(&thread_current()->spt, page->va));
	//printf("real last uninit type : %d\n", uninit->type);
	bool succ2 = init ? init (page, aux) : true;
	//printf("init2 success\n");
	return succ1 && succ2;
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
	struct thread *curr = thread_current();

	//list_remove(&page->frame->frame_elem);
	list_remove(&page->elem);
	//free(page->frame);

	return;
}
