The DDHCP algorithm
===================

In this document we describe the concept behind the algorithm driving the ddhcpd.
We are going to begin with a description of the managed data structure and continue 
with communication between nodes of the decentralised dhcp server.

Data structure, state machines and general terms
------------------------------------------------

A dhcp server is basically a resource manager, which manage and distribute 
a given ip-range. So given a ip-range we divide this range into equal size ranges,
which we further call _blocks_ and the size of those blocks is given by the block
size. For easy of handling ddhcpd requires the block size to be a power of two.
The nodes in a ddhcp network distributes those block between each other.
Each block is identified by a number, which we call the block index.

Quick start: A ddhcp server network is a part of a distributed database,
which distributes resources to clients (ip addresses) from acquired resources (blocks).
For each block we note a state. A block is either _free_ (not owned by any node), 
_tentative_ (a node raised interest), _claimed_ (owned by a block) or _blocked_
 (not ownable, but in the managed ip-range). In practice a node manage additional
states: _claiming_ (tentative because the node itself raised interest),
and _our_ (owned by the node itself).

Further we define transitions between states by either actions (messages)
or timeouts. Each block start in the state _free_, a node may raise interest
which transition the state of the block into _tentative_ and then may be _claimed_
by that node.
When a block in _tentative_ state is not _claimed_ it transit to the _free_ state after _tentation-timeout_ time. 
When a block in _claimed_ state and the claim is not refreshed it transit into the _free_ state, after _block-timeout_ time.

The above defines a state machine, with _free_ being the starting point.
Hence the data structure each node has to manage is defined by this state machine
and timers on the machine.

Starting a node: The learning phase
-----------------------------------

In the beginning, after a node is started, it has no knowledge about state of blocks.
So it needs to listen and learn. As described above, a claimed block must be 
refreshed, hence if the node listen for block-timeout time then each claimed
block should have been reclaimed, and all none claimed blocks are free. By
the definition of the state machine.

We call this listen time the _learning phase_. The implementation allows to
deactivate this phase, to kick start a node, which is no problem because
we have all kinds of tie-solvers in the protocol. But until now this is not a
good idea. But there are some ideas down the pipe, which are going to relax 
the situation.

Messages
--------

To organize the state of blocks we have two types of messages:
The _Inquire_ message transition a block into the _tentative_ state and
the _Claim_ message transition a block into the _claimed_ state.
Both messages can be retransmitted to refresh the timeout of that state
and may include more than one block.

Tie-Breaker
-----------

Each node has an unique identifier, we use the given order on the identifier space
as a tie-breaker.
