#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>


volatile uint32_t * e1000_mmio_base_addr;

#define TX_BUF_SIZE 1536  // 16-byte aligned for performance
#define NTXDESC     64

static struct tx_desc e1000_tx_queue[NTXDESC] __attribute__((aligned(16)));
static uint8_t e1000_tx_buf[NTXDESC][TX_BUF_SIZE];

#define RX_BUF_SIZE 2048
#define NRXDESC     128

static struct e1000_rx_desc e1000_rx_queue[NRXDESC] __attribute__((aligned(16)));
static uint8_t e1000_rx_buf[NRXDESC][RX_BUF_SIZE];


// LAB 6: Your driver code here
int
e1000_attach(struct pci_func *pcif)
{
    cprintf("e1000_attach: attaching e1000\n");

    pci_func_enable(pcif);

    e1000_mmio_base_addr = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    cprintf("e1000_attach: status %x\n", E1000_REG(E1000_STATUS_OFFSET));

    e1000_tx_init();
    e1000_rx_init();

    return 0;
}

static void
e1000_tx_init()
{
    // initialize tx queue
    int i;
    memset(e1000_tx_queue, 0, sizeof(e1000_tx_queue));
    for (i = 0; i < NTXDESC; i++) {
        e1000_tx_queue[i].addr = PADDR(e1000_tx_buf[i]);
    }

    // initialize transmit descriptor registers
    E1000_REG(E1000_TDBAL) = PADDR(e1000_tx_queue);
    E1000_REG(E1000_TDBAH) = 0;
    E1000_REG(E1000_TDLEN) = sizeof(e1000_tx_queue);
    E1000_REG(E1000_TDH) = 0;
    E1000_REG(E1000_TDT) = 0;

    // initialize transmit control registers
    E1000_REG(E1000_TCTL) &= ~(E1000_TCTL_CT | E1000_TCTL_COLD);
    E1000_REG(E1000_TCTL) |= E1000_TCTL_EN | E1000_TCTL_PSP |
                             (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT) |
                             (E1000_COLLISION_DISTANCE << E1000_COLD_SHIFT);
    E1000_REG(E1000_TIPG) &= ~(E1000_TIPG_IPGT_MASK | E1000_TIPG_IPGR1_MASK | E1000_TIPG_IPGR2_MASK);
    E1000_REG(E1000_TIPG) |= E1000_DEFAULT_TIPG_IPGT |
                             (E1000_DEFAULT_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT) |
                             (E1000_DEFAULT_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT);
}


static void
e1000_rx_init()
{
    // initialize rx queue
    int i;
    memset(e1000_rx_queue, 0, sizeof(e1000_rx_queue));
    for (i = 0; i < NRXDESC; i++) {
        e1000_rx_queue[i].addr = PADDR(e1000_rx_buf[i]);
    }

    // initialize receive address registers
    // by default, it comes from EEPROM
    E1000_REG(E1000_RAL) = JOS_DEFAULT_MAC_LOW;
    E1000_REG(E1000_RAH) = JOS_DEFAULT_MAC_HIGH;
    E1000_REG(E1000_RAH) |= E1000_RAH_AV;

    // initialize receive descriptor registers
    E1000_REG(E1000_RDBAL) = PADDR(e1000_rx_queue);
    E1000_REG(E1000_RDBAH) = 0;
    E1000_REG(E1000_RDLEN) = sizeof(e1000_rx_queue);
    E1000_REG(E1000_RDH) = 0;
    E1000_REG(E1000_RDT) = NRXDESC - 1;

    // initialize transmit control registers
    E1000_REG(E1000_RCTL) &= ~(E1000_RCTL_LBM | E1000_RCTL_RDMTS | E1000_RCTL_SZ | E1000_RCTL_BSEX);
    E1000_REG(E1000_RCTL) |= E1000_RCTL_EN | E1000_RCTL_SECRC;
}


int
e1000_transmit(const void *buf, size_t size)
{
    int tail = E1000_REG(E1000_TDT);

    if (size > ETH_PKT_SIZE) {
        return -E_PKT_TOO_LARGE;
    }

    if ((e1000_tx_queue[tail].cmd & E1000_TXD_CMD_RS) && !(e1000_tx_queue[tail].status & E1000_TXD_STAT_DD)) {
        return -E_TX_FULL;
    }

    e1000_tx_queue[tail].status &= ~E1000_TXD_STAT_DD;
    memcpy(e1000_tx_buf[tail], buf, size);
    e1000_tx_queue[tail].length = size;
    e1000_tx_queue[tail].cmd |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;

    E1000_REG(E1000_TDT) = (tail + 1) % NTXDESC;

    return 0;
}
