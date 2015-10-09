/**
 * DHCP Structures
 */

enum dhcp_lease_state {
  FREE,
  OFFERED,
  RESERVED,
};

// DHCP lease 
struct dhcp_lease {
  u32 client_id;
  dhcp_lease_state state;
  u32 lease_end;
}

struct dhcp_lease_block {

};
