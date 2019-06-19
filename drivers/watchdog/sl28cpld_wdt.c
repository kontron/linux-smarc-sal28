/*
 * SMARC-sAL28 Watchdog driver.
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
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/mfd/sl28cpld.h>

/*
 * Watchdog timer block registers.
 */
#define SL28CPLD_WDT_CTRL	0
#define  WDT_CTRL_EN		BIT(0)
#define  WDT_CTRL_LOCK		BIT(2)
#define SL28CPLD_WDT_TIMEOUT	1
#define SL28CPLD_WDT_KICK	2
#define  WDT_KICK_VALUE		0x6b
#define SL28CPLD_WDT_COUNT	3

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int timeout = 0;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Initial watchdog timeout in seconds");

struct sl28cpld_wdt {
	struct watchdog_device wdd;
	struct regmap *regmap;
	u32 offset;
};

static int sl28cpld_wdt_ping(struct watchdog_device *wdd)
{
	struct sl28cpld_wdt *wdt = watchdog_get_drvdata(wdd);
	return regmap_write(wdt->regmap, wdt->offset + SL28CPLD_WDT_KICK,
			    WDT_KICK_VALUE);
}

static int sl28cpld_wdt_start(struct watchdog_device *wdd)
{
	struct sl28cpld_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned int val;

	val = WDT_CTRL_EN;
	if (nowayout)
		val |= WDT_CTRL_LOCK;

	return regmap_update_bits(wdt->regmap, wdt->offset + SL28CPLD_WDT_CTRL,
				  val, val);
}

static int sl28cpld_wdt_stop(struct watchdog_device *wdd)
{
	struct sl28cpld_wdt *wdt = watchdog_get_drvdata(wdd);
	return regmap_update_bits(wdt->regmap, wdt->offset + SL28CPLD_WDT_CTRL,
				  WDT_CTRL_EN, 0);
}

static unsigned int sl28cpld_wdt_status(struct watchdog_device *wdd)
{
	struct sl28cpld_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned int status;
	int ret;

	ret = regmap_read(wdt->regmap, wdt->offset + SL28CPLD_WDT_CTRL,
			  &status);
	if (ret < 0)
		return 0;

	/* is the watchdog timer running? */
	return (status & WDT_CTRL_EN) << WDOG_ACTIVE;
}

static unsigned int sl28cpld_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct sl28cpld_wdt *wdt = watchdog_get_drvdata(wdd);
	int ret;
	unsigned int val;

	ret = regmap_read(wdt->regmap, wdt->offset + SL28CPLD_WDT_COUNT, &val);
	if (ret < 0)
		return 0;

	return val;
}

static int sl28cpld_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	int ret;
	struct sl28cpld_wdt *wdt = watchdog_get_drvdata(wdd);

	ret = regmap_write(wdt->regmap, wdt->offset + SL28CPLD_WDT_TIMEOUT,
			   timeout);
	if (ret == 0)
		wdd->timeout = timeout;

	return ret;
}

static const struct watchdog_info sl28cpld_wdt_info = {
	.options = WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "SMARC-sAL28 CPLD watchdog",
};

static struct watchdog_ops sl28cpld_wdt_ops = {
	.owner = THIS_MODULE,
	.start = sl28cpld_wdt_start,
	.stop = sl28cpld_wdt_stop,
	.status = sl28cpld_wdt_status,
	.ping = sl28cpld_wdt_ping,
	.set_timeout = sl28cpld_wdt_set_timeout,
	.get_timeleft = sl28cpld_wdt_get_timeleft,
};

static int sl28cpld_wdt_locked(struct sl28cpld_wdt *wdt)
{
	unsigned int val;
	int ret;

	ret = regmap_read(wdt->regmap, wdt->offset + SL28CPLD_WDT_CTRL, &val);
	if (ret < 0)
		return ret;

	return val & WDT_CTRL_LOCK;
}

static int sl28cpld_wdt_probe(struct platform_device *pdev)
{
	struct sl28cpld_wdt *wdt;
	struct watchdog_device *wdd;
	struct device *parent;
	int ret;
	const __be32 *reg;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	parent = pdev->dev.parent;
	if (!parent) {
		dev_err(&pdev->dev, "No parent for sl28cpld-wdt\n");
		return -ENODEV;
	}

	wdt->regmap = sl28cpld_node_to_regmap(parent->of_node);
	if (IS_ERR(wdt->regmap)) {
		dev_err(&pdev->dev, "No regmap for parent\n");
		return PTR_ERR(wdt->regmap);
	}

	reg = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!reg) {
		dev_err(&pdev->dev, "No 'offset' missing\n");
		return -EINVAL;
	}
	wdt->offset = be32_to_cpu(*reg);

	/* initialize struct watchdog_device */
	wdd = &wdt->wdd;
	wdd->parent = &pdev->dev;
	wdd->info = &sl28cpld_wdt_info;
	wdd->ops = &sl28cpld_wdt_ops;
	wdd->min_timeout = 1;
	wdd->max_timeout = 255;

	watchdog_set_drvdata(wdd, wdt);

	/* if the watchdog is locked, we set nowayout to true */
	ret = sl28cpld_wdt_locked(wdt);
	if (ret < 0)
		return ret;
	if (ret)
		nowayout = true;
	watchdog_set_nowayout(wdd, nowayout);

	/*
	 * Initial timeout value, can either be set by kernel parameter or by
	 * the device tree. If both are not given the current value is used.
	 */
	watchdog_init_timeout(wdd, timeout, &pdev->dev);
	if (wdd->timeout) {
		sl28cpld_wdt_set_timeout(wdd, wdd->timeout);
	} else {
		unsigned int val;
		ret = regmap_read(wdt->regmap,
				  wdt->offset + SL28CPLD_WDT_TIMEOUT, &val);
		if (ret < 0)
			return ret;
		wdd->timeout = val;
	}

	ret = watchdog_register_device(wdd);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register watchdog device\n");
		return ret;
	}

	platform_set_drvdata(pdev, wdt);

	dev_info(&pdev->dev, "CPLD watchdog: initial timeout %d sec%s\n",
		wdd->timeout, nowayout ? ", nowayout" : "");

	return 0;
}

static int sl28cpld_wdt_remove(struct platform_device *pdev)
{
	struct sl28cpld_wdt *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&wdt->wdd);

	return 0;
}

static void sl28cpld_wdt_shutdown(struct platform_device *pdev)
{
	struct sl28cpld_wdt *wdt = platform_get_drvdata(pdev);

	sl28cpld_wdt_stop(&wdt->wdd);
}

static const struct of_device_id sl28cpld_wdt_match[] = {
	{ .compatible = "kontron,sl28cpld-wdt" },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_wdt_match);

static struct platform_driver sl28cpld_wdt_driver = {
	.probe		= sl28cpld_wdt_probe,
	.remove		= sl28cpld_wdt_remove,
	.shutdown	= sl28cpld_wdt_shutdown,
	.driver = {
		.name = "sl28cpld-wdt",
		.of_match_table = of_match_ptr(sl28cpld_wdt_match),
	},
};
module_platform_driver(sl28cpld_wdt_driver);

MODULE_AUTHOR("Michael Walle <michael.walle@kontron.com>");
MODULE_DESCRIPTION("sl28 CPLD Watchdog Driver");
MODULE_LICENSE("GPL");
