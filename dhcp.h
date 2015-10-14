/**
 * DHCP Structures
 */

#include <stdint.h>

enum dhcp_lease_state {
  FREE,
  OFFERED,
  LEASED,
};

// DHCP lease 
struct dhcp_lease {
  uint32_t client_id;
  enum dhcp_lease_state state;
  uint32_t lease_end;
};

struct dhcp_lease_block {
  uint32_t subnet_begin;
  uint32_t subnet_len;  
  struct dhcp_lease* addresses;
};

int DHCP_OFFER_TIMEOUT = 600;
int DHCP_LEASE_TIME    = 3600;

/**
 * dhcp_new_lease_block
 * Create a new lease block. Return 0 on success.
 */ 
int dhcp_new_lease_block(struct dhcp_lease_block** lease_block,uint32_t subnet_begin,uint32_t subnet_len);

/**
 * dhcp_free_lease_block
 * Free a allocated lease block.
 */
void dhcp_free_lease_block(struct dhcp_lease_block** lease_block);

/**
 * DHCP Discover
 * Performs a search for a available, not already offered address in the 
 * available block. When the block has no further available addresses 0 is returned,
 * otherwise the then reserved address. Reservation will set a lease_end on the
 * address, so an dhcp_request COULD make the pre-reservation to an real lease.
 */ 
int32_t dhcp_discover(struct dhcp_lease_block** lease_block, uint32_t client_id);

/** 
 * DHCP Request
 * Performs on base of de
 */
int dhcp_request(struct dhcp_lease_block** lease_block,uint32_t client_id,uint32_t offer);



