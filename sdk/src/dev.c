/*
 * Device helpers shared by the SDK modules.
 *
 * Dmitry Brant, 2026
 */
#include "sdk.h"

static bool str_eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

bool fw_dev_busy(const char *name)
{
	void **head = FW_DEV_LIST;
	for (void **link = *head; link != head; link = *link) {
		uint8_t *node = (uint8_t *)link - 0x14;
		if (str_eq(*(const char **)node, name))
			return node[0x10] & 1;
	}
	return true;
}
