#pragma once

#include <stddef.h>
#include <stdint.h>

#include "types.h"

/**
 * Search and returns an dhcp_option in a list of options.
 * Returns NULL otherwise.
 */
ATTR_NONNULL_ALL dhcp_option* find_option(dhcp_option* options, uint8_t len, uint8_t code);

/**
 * Search for the option with searched code and replace payload or search an empty
 * option, a padding, and replaces it with the new option. The option payload
 * is pointed to the new payload.
 * Return a value greater then 0 on failure or 0 otherwise.
 */
ATTR_NONNULL_ALL int set_option(dhcp_option* options, uint8_t len, uint8_t code, uint8_t payload_len, uint8_t* payload);

/**
 * Remove options.
 */
ATTR_NONNULL_ALL void remove_option(dhcp_option* options, uint8_t code);

/**
 * Search for the parameter request list option in a list of options.
 * On success the requested pointer is set and a positiv integer
 * is returned. Otherwise 0 is returned and requested is pointed to NULL.
 */
ATTR_NONNULL(1) int find_option_parameter_request_list(dhcp_option* options, uint8_t len, uint8_t** requested);

/** Search for the requested ip address option in a list of options.
 * On success the pointer to the payload is returned, which should be
 * of length 4. TODO Check this
 * Otherwise the null-pointer is returned.
 */
ATTR_NONNULL_ALL uint8_t* find_option_requested_address(dhcp_option* options, uint8_t len);

/**
 * First searches the parameter request list in the given options list.
 * Then use the option_store to fulfill those request. The result is
 * left in the fullfil list. In front of the list additional many options are reserved.
 * On failure fullfil_list is the null-pointer and 0 is returned.
 *
 * Caller must handle memory deallocation.
 */
ATTR_NONNULL_ALL int16_t fill_options(dhcp_option* options, uint8_t len, dhcp_option_list* option_store, uint8_t additional, dhcp_option** fullfil);

/**
 * Search and Retrun a option in an option store. Return null otherwise.
 */
ATTR_NONNULL_ALL dhcp_option* find_in_option_store(dhcp_option_list* options, uint8_t code);

/**
 * Search and return leasetime option
 */
ATTR_NONNULL_ALL uint32_t find_in_option_store_address_lease_time(dhcp_option_list* options);

/**
 * Is a option defined in a option store
 */
#define has_in_option_store(options, code) (find_in_option_store(options, code) != NULL)

/**
 * Search and replace a option in the store, otherwise append it to the store.
 */
ATTR_NONNULL_ALL dhcp_option* set_option_in_store(dhcp_option_list* store, dhcp_option* option);

/**
 * Search and remove a option in the store.
 */
ATTR_NONNULL_ALL void remove_option_in_store(dhcp_option_list* store, uint8_t code);

/**
 * Free option store and all contained dhcp_options.
 */
ATTR_NONNULL_ALL void free_option_store(dhcp_option_list* store);

/**
 * Print the inventory of a option store into given file descriptor.
 */
ATTR_NONNULL_ALL void dhcp_options_show(int fd, ddhcp_config* config);

/**
 * Initialize dhcp_options store in the configuration.
 */
ATTR_NONNULL_ALL int dhcp_options_init(ddhcp_config* config);

/**
 * Search for option in store and if found store it in the options list.
 */
ATTR_NONNULL_ALL int set_option_from_store(dhcp_option_list* store, dhcp_option* options, uint8_t len, uint8_t code);
