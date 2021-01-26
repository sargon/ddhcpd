#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dhcp_option.h"
#include "list.h"
#include "logger.h"
#include "tools.h"

ATTR_NONNULL_ALL dhcp_option *dhcp_option_find(dhcp_option *options,
					       uint8_t len, uint8_t code)
{
	dhcp_option *option = options;

	for (; option < options + len; option++)
		if (option->code == code)
			return option;

	return NULL;
}

ATTR_NONNULL_ALL int dhcp_option_set(dhcp_option *options, uint8_t len,
				     uint8_t code, uint8_t payload_len,
				     uint8_t *payload)
{
	DEBUG("dhcp_option_set(options, len:%i, code:%i, payload_len:%i, payload)\n",
	      len, code, payload_len);

	for (int i = len - 1; i >= 0; i--) {
		dhcp_option *option = options + i;

		if (option->code == code || option->code == 0) {
			option->code = code;
			option->len = payload_len;
			option->payload = payload;

			DEBUG("dhcp_option_set(...): set option at %i\n", i);

			return 0;
		}
	}

	DEBUG("dhcp_option_set(...): failed\n");
	return 1;
}

ATTR_NONNULL_ALL int dhcp_option_set_from_store(dhcp_option_list *store,
						dhcp_option *options,
						uint8_t len, uint8_t code)
{
	dhcp_option *option = dhcp_option_find_in_store(store, code);

	if (!option) {
		DEBUG("dhcp_option_set_from_store(...): Option %u not found in store\n",
		      code);
		return 1;
	}

	return dhcp_option_set(options, len, code, option->len,
			       option->payload);
}

ATTR_NONNULL(1)
int dhcp_option_find_parameter_request_list(dhcp_option *options, uint8_t len,
					    uint8_t **requested)
{
	dhcp_option *option = dhcp_option_find(
		options, len, DHCP_CODE_PARAMETER_REQUEST_LIST);

	if (requested)
		*requested = option ? (uint8_t *)option->payload : NULL;

	int optlen = option ? option->len : 0;

	DEBUG("dhcp_option_find_parameter_request_list(...): Length %i\n",
	      optlen);

	return optlen;
}

ATTR_NONNULL_ALL uint8_t *
dhcp_option_find_requested_address(dhcp_option *options, uint8_t len)
{
	dhcp_option *option =
		dhcp_option_find(options, len, DHCP_CODE_REQUESTED_ADDRESS);

	DEBUG("dhcp_option_find_requested_address(...): address %s\n",
	      option ? "found" : "not found");

	return option ? option->payload : NULL;
}

ATTR_NONNULL_ALL dhcp_option *
dhcp_option_find_in_store(dhcp_option_list *options, uint8_t code)
{
	DEBUG("dhcp_option_find_in_store(store, code:%i)\n", code);

	dhcp_option *option;

	list_for_each_entry (option, options, option_list) {
		if (option->code == code) {
			DEBUG("dhcp_option_find_in_store(...): Found option with code %i\n",
			      code);
			return option;
		}
	}

	return NULL;
}

ATTR_NONNULL_ALL uint32_t
dhcp_option_find_in_store_address_lease_time(dhcp_option_list *options)
{
	dhcp_option *lease_time_opt = dhcp_option_find_in_store(
		options, DHCP_CODE_ADDRESS_LEASE_TIME);

	if (lease_time_opt) {
		uint32_t buf = 0;

		if (!lease_time_opt->payload ||
		    lease_time_opt->len < sizeof(buf)) {
			WARNING("dhcp_option_find_in_store(...): requested option had insufficient or no payload data specified.");
			return 0;
		}

		memcpy(&buf, lease_time_opt->payload, sizeof(buf));
		return ntohl(buf);
	}

	return 0;
}

ATTR_NONNULL_ALL dhcp_option *dhcp_option_set_in_store(dhcp_option_list *store,
						       dhcp_option *option)
{
	DEBUG("set_in_option_store(store, code:%i, len%i)\n", option->code,
	      option->len);

	dhcp_option *current = dhcp_option_find_in_store(store, option->code);

	if (current) {
		DEBUG("set_in_option_store(...): replace option\n");

		// Replacing current with new option

		if (current->payload)
			free(current->payload);

		current->len = option->len;
		current->payload = option->payload;

		return current;
	} else {
		DEBUG("set_in_option_store(...): inserting option\n");

		list_add_tail(&option->option_list, store);

		return option;
	}
}

ATTR_NONNULL_ALL void dhcp_option_free(struct dhcp_option *option)
{
	if (option->payload)
		free(option->payload);

	free(option);
}

ATTR_NONNULL_ALL void dhcp_option_remove_in_store(dhcp_option_list *store,
						  uint8_t code)
{
	dhcp_option *option = dhcp_option_find_in_store(store, code);

	if (option) {
		list_del(&option->option_list);
		dhcp_option_free(option);
	}
}

ATTR_NONNULL_ALL void dhcp_option_free_store(dhcp_option_list *store)
{
	struct list_head *pos, *q;

	list_for_each_safe (pos, q, store) {
		dhcp_option *option = list_entry(pos, dhcp_option, option_list);
		list_del(pos);
		dhcp_option_free(option);
	}
}

ATTR_NONNULL_ALL dhcp_option *
dhcp_option_remove_from_store(dhcp_option_list *store, uint8_t code);

ATTR_NONNULL_ALL int16_t dhcp_option_fill(dhcp_option *options, uint8_t len,
					  dhcp_option_list *option_store,
					  uint8_t additional,
					  dhcp_option **fullfil)
{
	uint8_t num_found_options = 0;

	uint8_t *requested = NULL;
	int max_options = dhcp_option_find_parameter_request_list(options, len,
								  &requested);

	*fullfil = (dhcp_option *)calloc(sizeof(dhcp_option),
					 (size_t)(max_options + additional));

	if (!*fullfil)
		return -ENOMEM;

	if (!max_options)
		return additional;

	for (uint8_t i = 0; i < max_options; i++) {
		uint8_t code = requested[i];
		// LOOP thought option_store
		dhcp_option *option =
			dhcp_option_find_in_store(option_store, code);

		if (option) {
			memcpy(*fullfil + num_found_options, option,
			       sizeof(dhcp_option));
			num_found_options++;
		}
	}

	return (int16_t)(num_found_options + additional);
}

ATTR_NONNULL_ALL void dhcp_option_show(int fd, ddhcp_config *config)
{
	struct dhcp_option *option;
	dhcp_option_list *store = &config->options;

	dprintf(fd, "DHCP Lease Time: %u\n\n",
		dhcp_option_find_in_store_address_lease_time(&config->options));
	dprintf(fd, "DHCP Disabled: %u\n", config->disable_dhcp);
	dprintf(fd, "DHCP Option Store\ncode\tlen\tpayload\n");

	list_for_each_entry (option, store, option_list) {
		dprintf(fd, "%i\t%i\t", option->code, option->len);

		for (int i = 0; i < option->len; i++)
			dprintf(fd, "%u\t", option->payload[i]);

		dprintf(fd, "\n");
	}
}

ATTR_NONNULL_ALL int dhcp_option_init(ddhcp_config *config)
{
	dhcp_option *option;
	int8_t pl = (int8_t)config->prefix_len;

	if (!has_in_option_store(&config->options, DHCP_CODE_SUBNET_MASK)) {
		// subnet mask
		option = (dhcp_option *)calloc(sizeof(dhcp_option), 1);

		if (!option)
			return -ENOMEM;

		option->code = DHCP_CODE_SUBNET_MASK;
		option->len = 4;
		option->payload = (uint8_t *)calloc(sizeof(uint8_t), 4);

		if (!option->payload) {
			free(option);
			return -ENOMEM;
		}

		option->payload[0] = (uint8_t)(
			256 - (256 >> min(max((int8_t)(pl - 0), 0), 8)));
		option->payload[1] = (uint8_t)(
			256 - (256 >> min(max((int8_t)(pl - 8), 0), 8)));
		option->payload[2] = (uint8_t)(
			256 - (256 >> min(max((int8_t)(pl - 16), 0), 8)));
		option->payload[3] = (uint8_t)(
			256 - (256 >> min(max((int8_t)(pl - 24), 0), 8)));

		dhcp_option_set_in_store(&config->options, option);
	}

	if (!has_in_option_store(&config->options, DHCP_CODE_TIME_OFFSET)) {
		option = (dhcp_option *)malloc(sizeof(dhcp_option));

		if (!option)
			return -ENOMEM;

		option->code = DHCP_CODE_TIME_OFFSET;
		option->len = 4;
		option->payload = (uint8_t *)calloc(sizeof(uint8_t), 4);

		if (!option->payload) {
			free(option);
			return -ENOMEM;
		}

		option->payload[0] = 0;
		option->payload[1] = 0;
		option->payload[2] = 0;
		option->payload[3] = 0;

		dhcp_option_set_in_store(&config->options, option);
	}

	if (!has_in_option_store(&config->options,
				 DHCP_CODE_BROADCAST_ADDRESS)) {
		option = (dhcp_option *)malloc(sizeof(dhcp_option));

		if (!option)
			return -ENOMEM;

		option->code = DHCP_CODE_BROADCAST_ADDRESS;
		option->len = 4;
		option->payload = (uint8_t *)calloc(sizeof(uint8_t), 4);

		if (!option->payload) {
			free(option);
			return -ENOMEM;
		}

		option->payload[0] =
			(uint8_t)((((uint8_t *)&config->prefix.s_addr)[0]) |
				  ((1 << min(max(8 - pl, 0), 8)) - 1));
		option->payload[1] =
			(uint8_t)((((uint8_t *)&config->prefix.s_addr)[1]) |
				  ((1 << min(max(16 - pl, 0), 8)) - 1));
		option->payload[2] =
			(uint8_t)((((uint8_t *)&config->prefix.s_addr)[2]) |
				  ((1 << min(max(24 - pl, 0), 8)) - 1));
		option->payload[3] =
			(uint8_t)((((uint8_t *)&config->prefix.s_addr)[3]) |
				  ((1 << min(max(32 - pl, 0), 8)) - 1));

		dhcp_option_set_in_store(&config->options, option);
	}

	if (!has_in_option_store(&config->options,
				 DHCP_CODE_ADDRESS_LEASE_TIME)) {
		option = (dhcp_option *)calloc(sizeof(dhcp_option), 1);

		if (!option)
			return -ENOMEM;

		option->code = DHCP_CODE_ADDRESS_LEASE_TIME;
		option->len = 4;
		option->payload = (uint8_t *)calloc(sizeof(uint8_t), 4);

		if (!option->payload) {
			free(option);
			return -ENOMEM;
		}

		// 300 ms ~ 5min
		option->payload[0] = 0x00;
		option->payload[1] = 0x00;
		option->payload[2] = 0x01;
		option->payload[3] = 0x2c;

		dhcp_option_set_in_store(&config->options, option);
	}

	if (!has_in_option_store(&config->options,
				 DHCP_CODE_SERVER_IDENTIFIER)) {
		option = (dhcp_option *)malloc(sizeof(dhcp_option));

		if (!option)
			return -ENOMEM;

		option->code = DHCP_CODE_SERVER_IDENTIFIER;
		option->len = 4;
		option->payload = (uint8_t *)calloc(sizeof(uint8_t), 4);

		if (!option->payload) {
			free(option);
			return -ENOMEM;
		}

		// TODO Check interface for address
		memcpy(option->payload, &config->prefix.s_addr, 4);
		//option->payload[0] = 10;
		//option->payload[1] = 0;
		//option->payload[2] = 0;
		option->payload[3] = 1;

		dhcp_option_set_in_store(&config->options, option);
	}

	return 0;
}
