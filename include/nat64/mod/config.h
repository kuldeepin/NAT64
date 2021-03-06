#ifndef _NF_NAT64_CONFIG_H
#define _NF_NAT64_CONFIG_H

/**
 * @file
 * The NAT64's layer/bridge towards the user. S/he can control its behavior using this.
 *
 * @author Miguel Gonzalez
 * @author Alberto Leiva  <- maintenance
 */

#include <linux/types.h>


/**
 * Initializes this module. Sets default values for the entire configuration.
 *
 * @return "true" if initialization was successful, "false" otherwise.
 */
int config_init(void);
/**
 * Terminates this module. Deletes any memory left on the heap.
 */
void config_destroy(void);


#endif /* _NF_NAT64_CONFIG_H */
