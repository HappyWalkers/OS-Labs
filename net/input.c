#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
    uint8_t buf[2048];
    int r, i;
    while(true) {
        while((r = sys_receive_packet(buf, sizeof(buf))) == -E_RX_EMPTY) {
            sys_yield();
        }
        if (r < 0) {
            panic("NS INPUT: sys_receive_packet: %e", r);
        }

        nsipcbuf.pkt.jp_len = r;
        memmove(nsipcbuf.pkt.jp_data, buf, r);

        ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P|PTE_W|PTE_U);

        // wait for the network server to process the packet
        sys_yield();
        sys_yield();
        sys_yield();
        sys_yield();
        sys_yield();
        sys_yield();
    }
}
