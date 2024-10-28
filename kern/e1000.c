#include <kern/e1000.h>
#include <kern/pmap.h>


volatile uint32_t * e1000_mmio_base_addr;

// LAB 6: Your driver code here
int
e1000_attach(struct pci_func *pcif)
{
    cprintf("e1000_attach: attaching e1000\n");

    pci_func_enable(pcif);

    e1000_mmio_base_addr = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    cprintf("e1000_attach: status %x\n", E1000_REG(E1000_STATUS_OFFSET));

    return 0;
}