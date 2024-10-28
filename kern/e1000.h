#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>


#define VENDOR_ID_82540EM 0x8086
#define DEVICE_ID_82540EM 0x100E

int
e1000_attach(struct pci_func *pcif);

#define E1000_STATUS_OFFSET 0x00008
#define E1000_REG(offset) (*(volatile uint32_t*)(e1000_mmio_base_addr + offset / 4))

#endif  // SOL >= 6
