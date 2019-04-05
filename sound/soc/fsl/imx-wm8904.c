/*
 * imx_wm8904 - i.MX ASoC driver for boards with WM8904 codec.
 *
 * Copyright (C) 2019 Kontron Europe GmbH
 *
 * GPLv2 or later
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/control.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mfd/syscon.h>
#include "../codecs/wm8904.h"
#include "fsl_sai.h"

struct imx_wm8904_data {
	struct clk *mclk;
};

static int imx_wm8904_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_wm8904_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, WM8904_FLL_MCLK, WM8904_FLL_MCLK,
				  clk_get_rate(data->mclk),
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Failed to set wm8904 codec PLL.\n");
		return ret;
	}

	/*
	 * As here wm8904 use FLL output as its system clock
	 * so calling set_sysclk won't care freq parameter
	 * then we pass 0
	 */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8904_CLK_FLL,
				     0, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Failed to set wm8904 SYSCLK.\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops imx_wm8904_ops = {
	.hw_params = imx_wm8904_hw_params,
};

static const struct snd_soc_dapm_widget imx_wm8904_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_SPK("Line Out Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static struct snd_soc_dai_link imx_wm8904_dai[] = {
	{
		.name		= "HiFi Tx",
		.stream_name	= "HiFi Playback",
		.codec_dai_name	= "wm8904-hifi",
		.dai_fmt	= SND_SOC_DAIFMT_I2S
				  | SND_SOC_DAIFMT_NB_NF
				  | SND_SOC_DAIFMT_CBM_CFM,
		.ops		= &imx_wm8904_ops,
		.playback_only	= true,
	}, {
		.name		= "HiFi Rx",
		.stream_name	= "HiFi Capture",
		.codec_dai_name	= "wm8904-hifi",
		.dai_fmt	= SND_SOC_DAIFMT_I2S
				  | SND_SOC_DAIFMT_NB_NF
				  | SND_SOC_DAIFMT_CBM_CFM,
		.ops		= &imx_wm8904_ops,
		.capture_only	= true,
	},
};

static struct snd_soc_card imx_wm8904 = {
	.owner		= THIS_MODULE,
	.dai_link	= imx_wm8904_dai,
	.num_links	= ARRAY_SIZE(imx_wm8904_dai),
	.dapm_widgets	= imx_wm8904_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(imx_wm8904_dapm_widgets),
};

static int imx_wm8904_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *sai_np[2], *codec_np;
	struct snd_soc_card *card = &imx_wm8904;
	struct i2c_client *codec_dev;
	struct imx_wm8904_data *data;
	int ret = -EINVAL;
	int i;

	sai_np[0] = of_parse_phandle(np, "sai-controllers", 0);
	sai_np[1] = of_parse_phandle(np, "sai-controllers", 1);
	codec_np = of_parse_phandle(np, "audio-codec", 0);
	if (!sai_np[0] || !sai_np[1] || !codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		goto err;
	}

	for (i = 0; i < 2; i++) {
		imx_wm8904_dai[i].codec_name = NULL;
		imx_wm8904_dai[i].codec_of_node = codec_np;
		imx_wm8904_dai[i].cpu_dai_name = NULL;
		imx_wm8904_dai[i].cpu_of_node = sai_np[i];
		imx_wm8904_dai[i].platform_name = NULL;
		imx_wm8904_dai[i].platform_of_node = sai_np[i];
	}

	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "Failed to find codec platform device.\n");
		goto err;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	data->mclk = devm_clk_get(&codec_dev->dev, "mclk");
	if (IS_ERR(data->mclk)) {
		ret = PTR_ERR(data->mclk);
		dev_err(&pdev->dev, "Failed to get MCLK (%d).\n", ret);
		goto err;
	}

	card->dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		goto err;

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret)
		goto err;

	snd_soc_card_set_drvdata(card, data);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register sound card (%d)\n", ret);
		goto err;
	}

	return 0;

err:
	of_node_put(sai_np[0]);
	of_node_put(sai_np[1]);
	of_node_put(codec_np);

	return ret;
}

static const struct of_device_id imx_wm8904_dt_ids[] = {
	{.compatible = "fsl,imx-audio-wm8904",},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, imx_wm8904_dt_ids);

static struct platform_driver imx_wm8904_driver = {
	.driver = {
		   .name = "imx-wm8904",
		   .pm = &snd_soc_pm_ops,
		   .of_match_table = imx_wm8904_dt_ids,
		   },
	.probe = imx_wm8904_probe,
};

module_platform_driver(imx_wm8904_driver);

MODULE_AUTHOR("Yadviga Grigoryeva <yadviga@dev.rtsoft.ru>");
MODULE_AUTHOR("Michael Walle <michael.walle@kontron.com>");
MODULE_DESCRIPTION("Freescale i.MX WM8904 ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-wm8904");
