# JOS

## Physical Memory Management
Physical memory is managed by pages. Physical pages are managed by the kernel using the `struct PageInfo` structure.
The kernel keeps an array of `struct PageInfo` to keep track of all the physical pages.
The kernel also keeps a linked list of free pages that can be allocated.
```c
// These variables are set in mem_init()
pde_t *kern_pgdir;		// Kernel's initial page directory
struct PageInfo *pages;		// Physical page state array
static struct PageInfo *page_free_list;	// Free list of physical pages
```

```c
/*
 * Page descriptor structures, mapped at UPAGES.
 * Read/write to the kernel, read-only to user programs.
 *
 * Each struct PageInfo stores metadata for one physical page.
 * Is it NOT the physical page itself, but there is a one-to-one
 * correspondence between physical pages and struct PageInfo's.
 * You can map a struct PageInfo * to the corresponding physical address
 * with page2pa() in kern/pmap.h.
 */
struct PageInfo {
	// Next page on the free list.
	struct PageInfo *pp_link;

	// pp_ref is the count of pointers (usually in page table entries)
	// to this page, for pages allocated using page_alloc.
	// Pages allocated at boot time using pmap.c's
	// boot_alloc do not have valid reference count fields.

	uint16_t pp_ref;
};
```


### Allocation before initializing the page free list
Before allocating the physical pages using the free list,
we need to allocate the memory for the PageInfo in the free list.
To do the self-lifting, we use a simple allocator called `boot_alloc`
that allocates memory by numerically incrementing a global variable `nextfree`.
```c
static void *
boot_alloc(uint32_t n)
{
	static char *nextfree;	// virtual address of next byte of free memory
	char *result;

	// Initialize nextfree if this is the first time.
	// 'end' is a magic symbol automatically generated by the linker,
	// which points to the end of the kernel's bss segment:
	// the first virtual address that the linker did *not* assign
	// to any kernel code or global variables.
	if (!nextfree) {
		extern char end[];
		nextfree = ROUNDUP((char *) end, PGSIZE);
	}

	// Allocate a chunk large enough to hold 'n' bytes, then update
	// nextfree.  Make sure nextfree is kept aligned
	// to a multiple of PGSIZE.
	//
	// LAB 2: Your code here.
    if(n == 0) {
        return nextfree;
    }else if(n > 0) {
        result = nextfree;
        nextfree += ROUNDUP(n, PGSIZE);
        return result;
    }

	return NULL;
}
```

### Initialization
Initialize page structure and memory free list.
```c
	//////////////////////////////////////////////////////////////////////
    // Allocate an array of npages 'struct PageInfo's and store it in 'pages'.
    // The kernel uses this array to keep track of physical pages: for
    // each physical page, there is a corresponding struct PageInfo in this
    // array.  'npages' is the number of physical pages in memory.  Use memset
    // to initialize all fields of each struct PageInfo to 0.
    // Your code goes here:
    pages = (struct PageInfo *) boot_alloc(npages * sizeof(struct PageInfo));
    memset(pages, 0, npages * sizeof(struct PageInfo));
```
```c
	//////////////////////////////////////////////////////////////////////
	// Now that we've allocated the initial kernel data structures, we set
	// up the list of free physical pages. Once we've done so, all further
	// memory management will go through the page_* functions. In
	// particular, we can now map memory using boot_map_region
	// or page_insert
	page_init();
```

```c
void
page_init(void)
{
	// The example code here marks all physical pages as free.
	// However this is not truly the case.  What memory is free?
	//  1) Mark physical page 0 as in use.
	//     This way we preserve the real-mode IDT and BIOS structures
	//     in case we ever need them.  (Currently we don't, but...)
	//  2) The rest of base memory, [PGSIZE, npages_basemem * PGSIZE)
	//     is free.
	//  3) Then comes the IO hole [IOPHYSMEM, EXTPHYSMEM), which must
	//     never be allocated.
	//  4) Then extended memory [EXTPHYSMEM, ...).
	//     Some of it is in use, some is free. Where is the kernel
	//     in physical memory?  Which pages are already in use for
	//     page tables and other data structures?
	//
	// Change the code to reflect this.
	// NB: DO NOT actually touch the physical memory corresponding to
	// free pages!
    pages[0].pp_ref = 1;
    pages[0].pp_link = NULL;

    for(int i = 1; i < npages_basemem; i++) {
        pages[i].pp_ref = 0;
        pages[i].pp_link = page_free_list;
        page_free_list = &pages[i];
    }

    for(int i = IOPHYSMEM / PGSIZE; i < EXTPHYSMEM / PGSIZE; i++) {
        pages[i].pp_ref = 1;
        pages[i].pp_link = NULL;
    }

    int boot_alloc_end = PADDR(boot_alloc(0)) / PGSIZE;
    for(int i = EXTPHYSMEM / PGSIZE; i < boot_alloc_end; i++) {
        pages[i].pp_ref = 1;
        pages[i].pp_link = NULL;
    }

    for(int i = boot_alloc_end; i < npages; i++) {
        pages[i].pp_ref = 0;
        pages[i].pp_link = page_free_list;
        page_free_list = &pages[i];
    }
}
```


### Allocation after initializing the page free list
To allocate a page, we need to remove a page from the free list.
```c
struct PageInfo *
page_alloc(int alloc_flags)
{
	// Fill this function in
    struct PageInfo* free_page = page_free_list;
    if(free_page == NULL) {
        return NULL;
    }
    page_free_list = free_page->pp_link;
    free_page->pp_link = NULL;

    if(alloc_flags & ALLOC_ZERO) {
        memset(page2kva(free_page), 0, PGSIZE);
    }
	return free_page;
}
```

### Freeing
When a physical page is no longer in use, we need to decrement the reference count of the page
so that the page can be freed when the reference count reaches 0.
```c
void
page_decref(struct PageInfo* pp)
{
	if (--pp->pp_ref == 0)
		page_free(pp);
}

```
To free a page, we need to return the page to the free list.
```c
//
// Return a page to the free list.
// (This function should only be called when pp->pp_ref reaches 0.)
//
void
page_free(struct PageInfo *pp)
{
	// Fill this function in
	// Hint: You may want to panic if pp->pp_ref is nonzero or
	// pp->pp_link is not NULL.
    if(pp->pp_ref != 0 || pp->pp_link != NULL) {
        panic("page_free: pp->pp_ref != 0 || pp->pp_link != NULL");
    }

    pp->pp_link = page_free_list;
    page_free_list = pp;
}
```






## Virtual Memory Management
Every process has its own virtual address space.
The whole virtual address space is divided into kernel space and user space.
Both of them can be divided into many virtual pages.
To read or write data in a virtual page, process usually needs to give a linear address that consists of a segment base and an offset.
Adding the segment base to the offset gives the logical address of the data.
Then the logical address is translated to a physical address using the 2-level page table.

The following is a general virtual memory layout of a process in JOS.

```c

/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     |                              | RW/--
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE, ---->  +------------------------------+ 0xf0000000      --+
 *    KSTACKTOP        |     CPU0's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     |     CPU1's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *    MMIOLIM ------>  +------------------------------+ 0xefc00000      --+
 *                     |       Memory-mapped I/O      | RW/--  PTSIZE
 * ULIM, MMIOBASE -->  +------------------------------+ 0xef800000
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xef400000
 *                     |          RO PAGES            | R-/R-  PTSIZE
 *    UPAGES    ---->  +------------------------------+ 0xef000000
 *                     |           RO ENVS            | R-/R-  PTSIZE
 * UTOP,UENVS ------>  +------------------------------+ 0xeec00000
 * UXSTACKTOP -/       |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebfd000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+
 *
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.  JOS user programs map pages temporarily at UTEMP.
 */


// All physical memory mapped at this address
#define	KERNBASE	0xF0000000

// At IOPHYSMEM (640K) there is a 384K hole for I/O.  From the kernel,
// IOPHYSMEM can be addressed at KERNBASE + IOPHYSMEM.  The hole ends
// at physical address EXTPHYSMEM.
#define IOPHYSMEM	0x0A0000
#define EXTPHYSMEM	0x100000

// Kernel stack.
#define KSTACKTOP	KERNBASE
#define KSTKSIZE	(8*PGSIZE)   		// size of a kernel stack
#define KSTKGAP		(8*PGSIZE)   		// size of a kernel stack guard

// Memory-mapped IO.
#define MMIOLIM		(KSTACKTOP - PTSIZE)
#define MMIOBASE	(MMIOLIM - PTSIZE)

#define ULIM		(MMIOBASE)

/*
 * User read-only mappings! Anything below here til UTOP are readonly to user.
 * They are global pages mapped in at env allocation time.
 */

// User read-only virtual page table (see 'uvpt' below)
#define UVPT		(ULIM - PTSIZE)
// Read-only copies of the Page structures
#define UPAGES		(UVPT - PTSIZE)
// Read-only copies of the global env structures
#define UENVS		(UPAGES - PTSIZE)

/*
 * Top of user VM. User can manipulate VA from UTOP-1 and down!
 */

// Top of user-accessible VM
#define UTOP		UENVS
// Top of one-page user exception stack
#define UXSTACKTOP	UTOP
// Next page left invalid to guard against exception stack overflow; then:
// Top of normal user stack
#define USTACKTOP	(UTOP - 2*PGSIZE)

// Where user programs generally begin
#define UTEXT		(2*PTSIZE)

// Used for temporary page mappings.  Typed 'void*' for convenience
#define UTEMP		((void*) PTSIZE)
// Used for temporary page mappings for the user page-fault handler
// (should not conflict with other temporary page mappings)
#define PFTEMP		(UTEMP + PTSIZE - PGSIZE)
// The location of the user-level STABS data structure
#define USTABDATA	(PTSIZE / 2)

// Physical address of startup code for non-boot CPUs (APs)
#define MPENTRY_PADDR	0x7000
```

### Initialization
JOS uses a two-level page table to manage the virtual memory. 
The initialization of kernel space involves 
allocating a physical page for the page directory
and allocating some pages for the kernel.


```c
// Set up a two-level page table:
//    kern_pgdir is its linear (virtual) address of the root
//
// This function only sets up the kernel part of the address space
// (ie. addresses >= UTOP).  The user part of the address space
// will be set up later.
//
// From UTOP to ULIM, the user is allowed to read but not write.
// Above ULIM the user cannot read or write.
void
mem_init(void)
```
```c
	// create initial page directory.
	kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
	memset(kern_pgdir, 0, PGSIZE);
```

```c
	//////////////////////////////////////////////////////////////////////
	// Map 'pages' read-only by the user at linear address UPAGES
	// Permissions:
	//    - the new image at UPAGES -- kernel R, user R
	//      (ie. perm = PTE_U | PTE_P)
	//    - pages itself -- kernel RW, user NONE
	// Your code goes here:
    boot_map_region(kern_pgdir, UPAGES, ROUNDUP(npages * sizeof(struct PageInfo), PGSIZE), PADDR(pages), PTE_U | PTE_P);

	//////////////////////////////////////////////////////////////////////
	// Map the 'envs' array read-only by the user at linear address UENVS
	// (ie. perm = PTE_U | PTE_P).
	// Permissions:
	//    - the new image at UENVS  -- kernel R, user R
	//    - envs itself -- kernel RW, user NONE
	// LAB 3: Your code here.
    boot_map_region(kern_pgdir, UENVS, ROUNDUP(NENV * sizeof(struct Env), PGSIZE), PADDR(envs), PTE_U | PTE_P);

	//////////////////////////////////////////////////////////////////////
	// Use the physical memory that 'bootstack' refers to as the kernel
	// stack.  The kernel stack grows down from virtual address KSTACKTOP.
	// We consider the entire range from [KSTACKTOP-PTSIZE, KSTACKTOP)
	// to be the kernel stack, but break this into two pieces:
	//     * [KSTACKTOP-KSTKSIZE, KSTACKTOP) -- backed by physical memory
	//     * [KSTACKTOP-PTSIZE, KSTACKTOP-KSTKSIZE) -- not backed; so if
	//       the kernel overflows its stack, it will fault rather than
	//       overwrite memory.  Known as a "guard page".
	//     Permissions: kernel RW, user NONE
	// Your code goes here:
    boot_map_region(kern_pgdir, KSTACKTOP - KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W | PTE_P);
//    boot_map_region(kern_pgdir, KSTACKTOP - PTSIZE, PTSIZE - KSTKSIZE, 0, PTE_W | PTE_P);

	//////////////////////////////////////////////////////////////////////
	// Map all of physical memory at KERNBASE.
	// Ie.  the VA range [KERNBASE, 2^32) should map to
	//      the PA range [0, 2^32 - KERNBASE)
	// We might not have 2^32 - KERNBASE bytes of physical memory, but
	// we just set up the mapping anyway.
	// Permissions: kernel RW, user NONE
	// Your code goes here:
    boot_map_region(kern_pgdir, KERNBASE, 0xffffffff - KERNBASE, 0, PTE_W | PTE_P);
```

```c
//
// Map [va, va+size) of virtual address space to physical [pa, pa+size)
// in the page table rooted at pgdir.  Size is a multiple of PGSIZE, and
// va and pa are both page-aligned.
// Use permission bits perm|PTE_P for the entries.
//
// This function is only intended to set up the ``static'' mappings
// above UTOP. As such, it should *not* change the pp_ref field on the
// mapped pages.
//
// Hint: the TA solution uses pgdir_walk
static void
boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
	// Fill this function in
    for(int i = 0; i < size; i += PGSIZE) {
        pte_t* pte = pgdir_walk(pgdir, (void *) va, 1);
        *pte = pa | perm | PTE_P;
        va += PGSIZE;
        pa += PGSIZE;
    }
}
```

To get the page table entry for a virtual address, we need to walk the page directory and the page table.
```c
// Given 'pgdir', a pointer to a page directory, pgdir_walk returns
// a pointer to the page table entry (PTE) for linear address 'va'.
// This requires walking the two-level page table structure.
//
// The relevant page table page might not exist yet.
// If this is true, and create == false, then pgdir_walk returns NULL.
// Otherwise, pgdir_walk allocates a new page table page with page_alloc.
//    - If the allocation fails, pgdir_walk returns NULL.
//    - Otherwise, the new page's reference count is incremented,
//	the page is cleared,
//	and pgdir_walk returns a pointer into the new page table page.
//
// Hint 1: you can turn a PageInfo * into the physical address of the
// page it refers to with page2pa() from kern/pmap.h.
//
// Hint 2: the x86 MMU checks permission bits in both the page directory
// and the page table, so it's safe to leave permissions in the page
// directory more permissive than strictly necessary.
//
// Hint 3: look at inc/mmu.h for useful macros that manipulate page
// table and page directory entries.
//
pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create)
{
	// Fill this function in
    pde_t* pde = &pgdir[PDX(va)];
    if(!(*pde & PTE_P)) {
        if(!create) {
            return NULL;
        }
        struct PageInfo* new_page = page_alloc(ALLOC_ZERO);
        if(new_page == NULL) {
            return NULL;
        }
        new_page->pp_ref++;
        *pde = page2pa(new_page) | PTE_P | PTE_W | PTE_U;
    }
    pte_t* pte = (pte_t *) KADDR(PTE_ADDR(*pde));
    return &pte[PTX(va)];
}
```

### Map a physical page to a virtual address
To map a physical page to a virtual address,
we need to find the page table entry for the virtual address and set the entry to the physical address.
```c
int
page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
{
	// Fill this function in
    pte_t* pte = pgdir_walk(pgdir, va, 1);
    if(pte == NULL) {
        return -E_NO_MEM;
    }
    pp->pp_ref++;
    if(*pte & PTE_P) {
        page_remove(pgdir, va);
    }
    *pte = page2pa(pp) | perm | PTE_P;
    return 0;
}
```

### Unmap a physical page from a virtual address
To unmap a physical page from a virtual address,
we need to find the page table entry for the virtual address and set the entry to 0.
Also, we need to decrement the reference count of the physical page.
```c
void
page_remove(pde_t *pgdir, void *va)
{
	// Fill this function in
    pte_t* pte;
    struct PageInfo* page = page_lookup(pgdir, va, &pte);
    if(page == NULL) {
        return;
    }
    page_decref(page);
    *pte = 0;
    tlb_invalidate(pgdir, va);
    return;
}
```




## Process Management
In JOS, a process is called an environment. 
Each environment has its own virtual address space. 
The kernel provides a mechanism for creating, destroying, and managing environments.
```c
struct Env {
	struct Trapframe env_tf;	// Saved registers when switching context
	struct Env *env_link;		// Next free Env
	envid_t env_id;			// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	enum EnvType env_type;		// Indicates special system environments
	unsigned env_status;		// Status of the environment
	uint32_t env_runs;		// Number of times environment has run

	// Address space
	pde_t *env_pgdir;		// Kernel virtual address of page dir
};
```

### Initialization
The processes are organized in a array so that we can easily find a process by its id.
They are also linked in a list that contains all the free processes that can be allocated.
The array and the linked list are initialized in the function `env_init` in the file `kern/env.c`.
```c
void
env_init(void)
{
	// Set up envs array
	// LAB 3: Your code here.
    for(int i = 0; i < NENV - 1; i++) {
        envs[i].env_id = 0;
        envs[i].env_status = ENV_FREE;
        envs[i].env_link = &envs[i+1];
    }
    envs[NENV - 1].env_id = 0;
    envs[NENV - 1].env_status = ENV_FREE;
    envs[NENV - 1].env_link = NULL;
    env_free_list = &envs[0];

	// Per-CPU part of the initialization
	env_init_percpu();
}
```

A single process can be accessed using its env_id.
```c
e = &envs[ENVX(envid)];
```

### Allocation
To create a new process, we need to initialize the fields of the struct `Env`.
```c
int
env_alloc(struct Env **newenv_store, envid_t parent_id)
```
1. Allocate a new process from the free list.
```c
if (!(e = env_free_list)) {
        panic("env_alloc: no free envs");
        return -E_NO_FREE_ENV;
    }
```
2. Allocate and set up a page directory for the new environment.
```c
	if ((r = env_setup_vm(e)) < 0)
		return r;
```
In JOS, the kernel memory is mapped in the page directory of each process
so that the kernel can be accessed from the user space.
```c
memcpy(e->env_pgdir, kern_pgdir, PGSIZE);
```
3. Generate an environment ID for the new environment.
```c
    // Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);
```
4. Set the appropriate status for the new environment.
```c
    // Set the basic status variables.
	e->env_parent_id = parent_id;
	e->env_type = ENV_TYPE_USER;
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;
```
5. Set the trapframe for the new environment.
```c
    // Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.  When
	// we switch privilege levels, the hardware does various
	// checks involving the RPL and the Descriptor Privilege Level
	// (DPL) stored in the descriptors themselves.
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_eip later.

	// Enable interrupts while in user mode.
	// LAB 4: Your code here.
    e->env_tf.tf_eflags |= FL_IF;
```

### Freeing
To free a process, we need to free the memory that was allocated for the process and return the process to the free list.
1. Free the pages, page tables, and the page directory of the process.
```c
	// Flush all mapped pages in the user portion of the address space
	static_assert(UTOP % PTSIZE == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*) KADDR(pa);

		// unmap all PTEs in this page table
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
				page_remove(e->env_pgdir, PGADDR(pdeno, pteno, 0));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	// free the page directory
	pa = PADDR(e->env_pgdir);
	e->env_pgdir = 0;
	page_decref(pa2page(pa));
```
2. Return the process to the free list.
```c
	// return the environment to the free list
    e->env_status = ENV_FREE;
    e->env_link = env_free_list;
    env_free_list = e;
```

### Running
To run a process, we need to
1. Set the current process to the process that we want to run.
```c
    if(curenv && curenv->env_status == ENV_RUNNING) {
        curenv->env_status = ENV_RUNNABLE;
    }
    curenv = e;
    curenv->env_status = ENV_RUNNING;
    curenv->env_runs++;
```
2. Load the page directory of the process.
```c
    lcr3(PADDR(curenv->env_pgdir));
```
3. Pop the trapframe of the process.
```c
    env_pop_tf(&curenv->env_tf);
```
```c
//
// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
//
// This function does not return.
//
void
env_pop_tf(struct Trapframe *tf)
{
	// Record the CPU we are running on for user-space debugging
	curenv->env_cpunum = cpunum();

	asm volatile(
		"\tmovl %0,%%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret\n"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}
```

## Trap and Interrupt Handling