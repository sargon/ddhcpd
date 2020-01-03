Hooks in the ddhcp daemon
=========================

The ddhcp daemon allows to register a hook script at startup. This script
is than called on specific events and allows to integrate ddhcpd into
an runtime environment. Further we are going to describe the events
and the cmdline parameters ddhcpd is calling the hook scripts.

Action: Lease and Release
-------------------------

    hook-script [lease|release] <ip-address> <mac-address>

In case ddhcpd is leasing an address to a client it calls the hook script
with the leased address and the mac address of the served client. In this
hook the given information can be used to feed secondary daemon with it, 
e.g. tell the distributed arp table of batman-adv the new entry.

Action: End of learning phase
-----------------------------

    hook-script endlearning

In the end of the learning phase the hook script gets called. This could
for example be used to set firewall rules.
