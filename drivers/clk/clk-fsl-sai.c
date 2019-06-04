// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale SAI BCLK as a generic clock driver
 *
 * Copyright 2019 Kontron Europe GmbH
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#define I2S_CSR	0x00
#define I2S_CR2	0x08
#define CSR_BCE_BIT	28
#define CR2_BCD	BIT(24)
#define CR2_DIV_SHIFT	0
#define CR2_DIV_WIDTH	8

static DEFINE_SPINLOCK(clklock);

struct fsl_sai_clk {
    struct clk_divider div;
    struct clk_gate gate;
};

static void __init fsl_sai_clk_setup(struct device_node *node)
{
	unsigned int num_parents;
	struct clk_hw *hw;
	const char *clk_name = node->name;
	const char *parent_name;
	void __iomem *base = NULL;
	struct fsl_sai_clk *sai_clk;

	num_parents = of_clk_get_parent_count(node);
	if (!num_parents) {
		pr_err("%s: no parent found", clk_name);
		return;
	}

	sai_clk = kzalloc(sizeof(*sai_clk), GFP_KERNEL);
	if (!sai_clk)
	    return;

	base = of_iomap(node, 0);
	if (base == NULL) {
		pr_err("%s: failed to map register space", clk_name);
		return;
	}

	parent_name = of_clk_get_parent_name(node, 0);

	sai_clk->gate.reg = base + I2S_CSR;
	sai_clk->gate.bit_idx = CSR_BCE_BIT;
	sai_clk->gate.lock = &clklock;

	sai_clk->div.reg = base + I2S_CR2;
	sai_clk->div.shift = CR2_DIV_SHIFT;
	sai_clk->div.width = CR2_DIV_WIDTH;
	sai_clk->div.lock = &clklock;

	/* set clock direction, we are the BCLK master */
	clk_writel(CR2_BCD, base + I2S_CR2);

	hw = clk_hw_register_composite(NULL, clk_name, &parent_name, 1,
				       NULL, NULL,
				       &sai_clk->div.hw, &clk_divider_ops,
				       &sai_clk->gate.hw, &clk_gate_ops,
				       CLK_SET_RATE_GATE);

	if (!IS_ERR(hw))
		of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw);
}

CLK_OF_DECLARE(fsl_sai_clk, "fsl,vf610-sai-clock", fsl_sai_clk_setup);
