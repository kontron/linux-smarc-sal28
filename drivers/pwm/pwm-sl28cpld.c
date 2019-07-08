/*
 * SMARC-sAL28 PWM driver.
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
#include <linux/pwm.h>
#include <linux/mfd/sl28cpld.h>

/*
 * PWM timer block registers.
 */
#define SL28CPLD_PWM_CTRL	0
#define   PWM_CTRL_PERIOD_MASK	0x3
#define   PWM_CTRL_ENABLE	BIT(7)
#define SL28CPLD_PWM_CYCLE	1

struct sl28cpld_pwm {
	struct pwm_chip pwm_chip;
	struct regmap *regmap;
	u32 offset;
};

static inline struct sl28cpld_pwm *to_sl28cpld_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct sl28cpld_pwm, pwm_chip);
}

struct sl28cpld_pwm_periods {
	u8 ctrl;
	unsigned long duty_cycle;
};

struct sl28cpld_pwm_config {
	unsigned long period_ns;
	u8 max_duty_cycle;
};

enum {
	PWM_MODE_250HZ = 0,
	PWM_MODE_500HZ = 1,
	PWM_MODE_1KHZ = 2,
	PWM_MODE_2KHZ = 3,
};

static struct sl28cpld_pwm_config sl28cpld_pwm_config[] = {
	{ .period_ns = 4000000, .max_duty_cycle = 0x80 }, /* 250 Hz */
	{ .period_ns = 2000000, .max_duty_cycle = 0x40 }, /* 500 Hz */
	{ .period_ns = 1000000, .max_duty_cycle = 0x20 }, /*  1 kHz */
	{ .period_ns =  500000, .max_duty_cycle = 0x10 }, /*  2 kHz */
};
#define PWM_CYCLE_MAX 0x7f

static void sl28cpld_pwm_get_state(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   struct pwm_state *state)
{
	struct sl28cpld_pwm *spc = to_sl28cpld_pwm(chip);
	static struct sl28cpld_pwm_config *config;
	unsigned int reg;
	unsigned long cycle;

	regmap_read(spc->regmap, spc->offset + SL28CPLD_PWM_CTRL, &reg);

	if (reg & PWM_CTRL_ENABLE)
		state->enabled = true;
	else
		state->enabled = false;

	config = &sl28cpld_pwm_config[reg & PWM_CTRL_PERIOD_MASK];
	state->period = config->period_ns;

	regmap_read(spc->regmap, spc->offset + SL28CPLD_PWM_CYCLE, &reg);
	cycle = reg * config->period_ns;
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(cycle, config->max_duty_cycle);
}

static int sl28cpld_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      struct pwm_state *state)
{
	struct sl28cpld_pwm *spc = to_sl28cpld_pwm(chip);
	struct sl28cpld_pwm_config *config;
	unsigned long long cycle;
	int ret;
	int i;
	u8 ctrl;

	/* update config, first search best matching period */
	for (i = 0; i < ARRAY_SIZE(sl28cpld_pwm_config); i++) {
		config = &sl28cpld_pwm_config[i];
		if (state->period == config->period_ns)
			break;
	}

	if (i == ARRAY_SIZE(sl28cpld_pwm_config))
		return -EINVAL;

	ctrl = i;
	if (state->enabled)
		ctrl |= PWM_CTRL_ENABLE;

	cycle = state->duty_cycle * config->max_duty_cycle;
	do_div(cycle, state->period);

	/*
	 * The hardware doesn't allow to set max_duty_cycle if the
	 * 250Hz mode is enabled. But since this is "all-high" output
	 * just use the 500Hz mode with the duty cycle to max value.
	 */
	if (cycle == config->max_duty_cycle) {
		ctrl &= ~PWM_CTRL_PERIOD_MASK;
		ctrl |= PWM_MODE_500HZ;
		cycle = PWM_CYCLE_MAX;
	}

	ret = regmap_write(spc->regmap,
			   spc->offset + SL28CPLD_PWM_CTRL, ctrl);
	if (ret)
		return ret;

	return regmap_write(spc->regmap,
			    spc->offset + SL28CPLD_PWM_CYCLE, (u8)cycle);
}

static const struct pwm_ops sl28cpld_pwm_ops = {
	.apply = sl28cpld_pwm_apply,
	.get_state = sl28cpld_pwm_get_state,
	.owner = THIS_MODULE,
};

static int sl28cpld_pwm_probe(struct platform_device *pdev)
{
	struct sl28cpld_pwm *pwm;
	struct device *parent;
	struct pwm_chip *chip;
	int ret;
	const __be32 *reg;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	parent = pdev->dev.parent;
	if (!parent) {
		dev_err(&pdev->dev, "No parent for sl28cpld-pwm\n");
		return -ENODEV;
	}

	pwm->regmap = sl28cpld_node_to_regmap(parent->of_node);
	if (IS_ERR(pwm->regmap)) {
		dev_err(&pdev->dev, "No regmap for parent\n");
		return PTR_ERR(pwm->regmap);
	}

	reg = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!reg) {
		dev_err(&pdev->dev, "No 'offset' missing\n");
		return -EINVAL;
	}
	pwm->offset = be32_to_cpu(*reg);

	/* initialize struct gpio_chip */
	chip = &pwm->pwm_chip;
	chip->dev = &pdev->dev;
	chip->ops = &sl28cpld_pwm_ops;
	chip->base = -1;
	chip->npwm = 1;

	ret = pwmchip_add(&pwm->pwm_chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, pwm);

	return 0;
}

static int sl28cpld_pwm_remove(struct platform_device *pdev)
{
	struct sl28cpld_pwm *pwm = platform_get_drvdata(pdev);

	return pwmchip_remove(&pwm->pwm_chip);
}

static const struct of_device_id sl28cpld_pwm_match[] = {
	{ .compatible = "kontron,sl28cpld-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_pwm_match);

static struct platform_driver sl28cpld_pwm_driver = {
	.driver = {
		.name = "sl28cpld-pwm",
		.of_match_table = of_match_ptr(sl28cpld_pwm_match),
	},
	.probe		= sl28cpld_pwm_probe,
	.remove		= sl28cpld_pwm_remove,
};
module_platform_driver(sl28cpld_pwm_driver);

MODULE_AUTHOR("Michael Walle <michael.walle@kontron.com>");
MODULE_DESCRIPTION("sl28 CPLD PWM Driver");
MODULE_LICENSE("GPL");
