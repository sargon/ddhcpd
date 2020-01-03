The DDHCP algorithm
===================

In this document we describe the concept behind the algorithm driving the ddhcpd.
We are going to begin with a description of the managed data structure and go on
with the communication between nodes of the decentralised dhcp server.

Data structure, state machines and general terms
------------------------------------------------

A dhcp server is basically a resource manager, which manages and distribute 
a given ip-range. So given a ip-range we divide this range into equal size ranges,
which we further call _blocks_ and the size of those block is given by the block
size. For easy of handling ddhcpd requires the block size to be a power of two.
The nodes in a ddhcp networks distribute those block between each other.

Quick start: So a ddhcp server network is a distributed resource manager, which
distributes resources to clients (ip addresses) from its acquired resources (blocks).
For each block we note a state. A block is either _free_ (not owned by any node), 
_tentative_ (a node raised interest), _claimed_ (owned by a block) or _blocked_
 (not own able, but in the managed range). In practice nodes know additional
states: _claiming_ (tentative because the node itself raised interest),
and _our_ (owned by the node itself).

Further state can transition into other states by either actions (or messages)
or timeouts. Each block starts in the state _free_ a node may raise interest
which transition the block into _tentative_ and from that could be _claimed_.
When a block is not _claimed_ it will be free after the _tentation-timeout_.
When a block is _claimed_ is the claim is not refreshed it is going to be _free_
again, after the _block-timeout_ time.

The above defines a state machine, with _free_ being the starting point.
Hence the data structure each node has to manage is defined by this state machine
and timers on the machine.

Starting a node: The learning phase
-----------------------------------

In the beginning when a node starts it has no knowledge about the network.
So it needs to listen and learn. When a block is claimed this claim must be 
refreshed, hence if the node listen for block-timeout time then each claimed
block should have been reclaimed, and all none claimed blocks are free. By
the definition of the state machine. 

We call this listen time the _learning phase_. The implementation allows to
deactivate this phase, to kick start a node, which is no problem because
we have all kinds of tie-solvers in the protocol. But until now this is not a
good idea. But there are some ideas down the pipe, which are going to relax 
the situation.
