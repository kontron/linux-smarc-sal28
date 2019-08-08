/*
 * MFD core for the CPLD on a SMARC-sAL28 board.
 *
 * Copyright 2019 Kontron Europe GmbH
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sl28cpld.h>

static DEFINE_SPINLOCK(sl28cpld_list_slock);
static LIST_HEAD(sl28cpld_list);

#define SL28CPLD_VERSION 0x03
#define SL28CPLD_MAX_REGISTER 0x1f
#define SL28CPLD_REQ_VERSION 14

struct sl28cpld {
	struct device *dev;
	struct regmap *regmap;
	struct list_head list;
};

struct regmap *sl28cpld_node_to_regmap(struct device_node *np)
{
	struct sl28cpld *entry, *sl28cpld = NULL;

	spin_lock(&sl28cpld_list_slock);
	list_for_each_entry(entry, &sl28cpld_list, list)
		if (entry->dev->of_node == np) {
			sl28cpld = entry;
			break;
		}
	spin_unlock(&sl28cpld_list_slock);

	if (!sl28cpld)
		return ERR_PTR(-EPROBE_DEFER);

	return sl28cpld->regmap;
}
EXPORT_SYMBOL_GPL(sl28cpld_node_to_regmap);

struct regmap *sl28cpld_regmap_lookup_by_phandle(struct device_node *np,
						 const char *property)
{
	struct device_node *sl28cpld_np;
	struct regmap *regmap;

	sl28cpld_np = of_parse_phandle(np, property, 0);
	if (!sl28cpld_np)
		return ERR_PTR(-ENODEV);

	regmap = sl28cpld_node_to_regmap(sl28cpld_np);
	of_node_put(np);

	return regmap;
}
EXPORT_SYMBOL_GPL(sl28cpld_regmap_lookup_by_phandle);

static const struct regmap_config sl28cpld_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
	.max_register = SL28CPLD_MAX_REGISTER,
};

static int sl28cpld_probe(struct i2c_client *i2c,
			  const struct i2c_device_id *id)
{
	struct sl28cpld *sl28cpld;
	struct device *dev = &i2c->dev;
	int ret;
	unsigned int cpld_version;

	sl28cpld = devm_kzalloc(dev, sizeof(*sl28cpld), GFP_KERNEL);
	if (!sl28cpld)
		return -ENOMEM;

	sl28cpld->regmap = devm_regmap_init_i2c(i2c, &sl28cpld_regmap_config);
	if (IS_ERR(sl28cpld->regmap))
		return PTR_ERR(sl28cpld->regmap);

	ret = regmap_read(sl28cpld->regmap, SL28CPLD_VERSION, &cpld_version);
	if (ret)
		return ret;

	if (cpld_version < SL28CPLD_REQ_VERSION) {
		dev_err(dev, "CPLD not compatible, at least version %d needed\n",
				SL28CPLD_REQ_VERSION);
		return -EINVAL;
	}

	sl28cpld->dev = dev;
	i2c_set_clientdata(i2c, sl28cpld);

	spin_lock(&sl28cpld_list_slock);
	list_add_tail(&sl28cpld->list, &sl28cpld_list);
	spin_unlock(&sl28cpld_list_slock);

	dev_info(dev, "successfully probed. CPLD version %02Xh.\n",
			cpld_version);

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to populate child nodes (%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id sl28cpld_ids[] = {
	{ "sl28cpld", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, sl28cpld_ids);

static const struct of_device_id sl28cpld_of_match[] = {
	{ .compatible = "kontron,sl28cpld", },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_of_match);

static struct i2c_driver sl28cpld_driver = {
	.probe = sl28cpld_probe,
	.driver = {
		.name = "sl28cpld",
		.of_match_table = of_match_ptr(sl28cpld_of_match),
	},
	.id_table = sl28cpld_ids,
};

module_i2c_driver(sl28cpld_driver);

MODULE_AUTHOR("Michael Walle <michael.walle@kontron.com>");
MODULE_DESCRIPTION("sl28 CPLD MFD Core Driver");
MODULE_LICENSE("GPL");
