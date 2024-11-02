#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
    uint32_t  req, whom;
    int r;

    while(true) {
        req = ipc_recv((int32_t *) &whom, &nsipcbuf, 0);
        if (whom != ns_envid) {
            cprintf("NS OUTPUT: got IPC message from env %x not NS\n", whom);
            continue;
        }
        if (req != NSREQ_OUTPUT) {
            cprintf("NS OUTPUT: got IPC message type %d not NSREQ_OUTPUT\n", req);
            continue;
        }

        while((r = sys_transmit_packet(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) == -E_TX_FULL) {
            sys_yield();
        }

        if (r < 0) {
            panic("NS OUTPUT: sys_transmit_packet: %e", r);
        }
    }
}
