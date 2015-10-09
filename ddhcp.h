/** 
 *  DDHCP Structs
 */

enum ddhcp_block_state { 
  FREE,
  TENTATIVE,
  CLAIMED,
  OURS,
  BLOCKED
};

struct ddhcp_block {
  u32 index;
  ddhcp_block_state state;
  u32 subnet;
  u8  subnet_len;
  u32 address;
  u32 valid_until;
  void* leases;
};
