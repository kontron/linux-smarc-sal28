#ifndef _SL28CPLD_H_
#define _SL28CPLD_H_

#include <linux/regmap.h>

struct regmap *sl28cpld_node_to_regmap(struct device_node *np);
struct regmap *sl28cpld_regmap_lookup_by_phandle(struct device_node *np,
						 const char *property);

#endif /* _SL28CPLD_H_ */
