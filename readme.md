# JOS

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