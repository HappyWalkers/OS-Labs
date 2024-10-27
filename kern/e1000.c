#include <kern/e1000.h>
#include <kern/pmap.h>

// LAB 6: Your driver code here
int
e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);
    cprintf("e1000_attach: attaching e1000\n");
    return 0;
}