/*
 * SMARC-sAL28 fan hardware monitoring driver.
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
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/mfd/sl28cpld.h>

struct sl28cpld_fan {
	struct regmap *regmap;
	u32 offset;
};

static umode_t sl28cpld_fan_is_visible(const void *data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	return 0444;
}

static int sl28cpld_fan_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *input)
{
	struct sl28cpld_fan *fan = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_fan_input:
		ret = regmap_read(fan->regmap, fan->offset, &value);
		break;
	default:
		return -EOPNOTSUPP;
	}

	*input = value;
	return 0;
}

static const u32 sl28cpld_fan_fan_config[] = {
	HWMON_F_INPUT,
	0
};

static const struct hwmon_channel_info sl28cpld_fan_fan = {
	.type = hwmon_fan,
	.config = sl28cpld_fan_fan_config,
};

static const struct hwmon_channel_info *sl28cpld_fan_info[] = {
	&sl28cpld_fan_fan,
	NULL
};

static const struct hwmon_ops sl28cpld_fan_ops = {
	.is_visible = sl28cpld_fan_is_visible,
	.read = sl28cpld_fan_read,
};

static const struct hwmon_chip_info sl28cpld_fan_chip_info = {
	.ops = &sl28cpld_fan_ops,
	.info = sl28cpld_fan_info,
};

static int sl28cpld_fan_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	struct sl28cpld_fan *fan;
	struct device *parent;
	const __be32 *reg;

	fan = devm_kzalloc(&pdev->dev, sizeof(*fan), GFP_KERNEL);
	if (!fan)
		return -ENOMEM;

	parent = pdev->dev.parent;
	if (!parent) {
		dev_err(&pdev->dev, "No parent for sl28cpld-fan\n");
		return -ENODEV;
	}

	fan->regmap = sl28cpld_node_to_regmap(parent->of_node);
	if (IS_ERR(fan->regmap)) {
		dev_err(&pdev->dev, "No regmap for parent\n");
		return PTR_ERR(fan->regmap);
	}

	reg = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!reg) {
		dev_err(&pdev->dev, "No 'offset' missing\n");
		return -EINVAL;
	}
	fan->offset = be32_to_cpu(*reg);

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "sl28cpld_fan",
							 fan,
							 &sl28cpld_fan_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev)) {
		dev_err(&pdev->dev, "Failed to register as hwmon device");
		return PTR_ERR(hwmon_dev);
	}

	return 0;
}

static const struct of_device_id sl28cpld_fan_match[] = {
	{ .compatible = "kontron,sl28cpld-fan", },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_fan_match);

static struct platform_driver sl28cpld_fan_driver = {
	.driver = {
		.name = "sl28cpld-fan",
		.of_match_table = of_match_ptr(sl28cpld_fan_match),
	},
	.probe		= sl28cpld_fan_probe,
};
module_platform_driver(sl28cpld_fan_driver);

MODULE_AUTHOR("Michael Walle <michael.walle@kontron.com>");
MODULE_DESCRIPTION("sl28 CPLD Fan Driver");
MODULE_LICENSE("GPL");
