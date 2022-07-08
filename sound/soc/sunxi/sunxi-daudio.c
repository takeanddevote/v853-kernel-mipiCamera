/* sound\soc\sunxi\sunxi-daudio.c
 * (C) Copyright 2015-2018
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * wolfgang huang <huangjinhui@allwinnertech.com>
 * yumingfeng <yumingfeng@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/dma/sunxi-dma.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include "sunxi-daudio.h"
#include "sunxi-snddaudio.h"
#include "sunxi-pcm.h"
#ifdef CONFIG_SND_SUNXI_MAD
#include "sunxi-mad.h"
#endif
#ifdef CONFIG_SUNXI_MPP_AIO
#include "sunxi-aio/mpp-aio.h"
extern struct sunxi_audio_mpp_debugfs *mpp_audio_debugfs;
#endif
#ifdef CONFIG_SND_SUN8IW8_ADC_TRIGGER_SYNC_WITH_OTHER_CODEC
#include "sun8iw8/sun8iw8_sndcodec.h"
#endif


#define	DRV_NAME	"sunxi-daudio"

#define SUNXI_RX_SYNC_EN (0)
#define	SUNXI_DAUDIO_RATES	(SNDRV_PCM_RATE_8000_192000 \
				| SNDRV_PCM_RATE_KNOT)

#define	SUNXI_DAUDIO_EXTERNAL_TYPE	1
#define	SUNXI_DAUDIO_TDMHDMI_TYPE	2
#define SUNXI_DAUDIO_DRQDST(sunxi_daudio, x)			\
	((sunxi_daudio)->playback_dma_param.dma_drq_type_num =	\
				DRQDST_DAUDIO_##x##_TX)
#define SUNXI_DAUDIO_DRQSRC(sunxi_daudio, x)			\
	((sunxi_daudio)->capture_dma_param.dma_drq_type_num =	\
				DRQSRC_DAUDIO_##x##_RX)

struct sunxi_daudio_platform_data {
	unsigned int daudio_type;
	unsigned int external_type;

	/* should be setup at machine driver. */
	unsigned int daudio_master;
	unsigned int audio_format;
	unsigned int signal_inversion;

	unsigned int pcm_lrck_period;
	unsigned int msb_lsb_first:1;
	unsigned int sign_extend:2;
	unsigned int tx_data_mode:2;
	unsigned int rx_data_mode:2;
	unsigned int slot_width_select;
	unsigned int frame_type;
	unsigned int tdm_config;
	unsigned int tdm_num;
	unsigned int mclk_div;
};

struct daudio_label {
	unsigned int address;
	int value;
};

struct voltage_supply {
	struct regulator *daudio_regulator;
	const char *regulator_name;
};

struct sunxi_daudio_info {
	struct device *dev;
	struct resource res;
	struct regmap *regmap;
	void __iomem *membase;
	struct resource *memregion;
	struct clk *pllclk;
#ifdef DAUDIO_PLL_AUDIO_X4
	struct clk *pllclkx4;
#endif
	struct clk *moduleclk;
	struct voltage_supply vol_supply;
	struct mutex mutex;
	struct mutex global_mutex;
	struct sunxi_dma_params playback_dma_param;
	struct sunxi_dma_params capture_dma_param;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinstate;
#ifdef DAUDIO_PINCTRL_STATE_DEFAULT_B
	struct pinctrl_state *pinstate_b;
#endif
	struct pinctrl_state *pinstate_sleep;
	struct sunxi_daudio_platform_data *pdata;
	unsigned int hub_mode;
	bool playback_en;
	bool capture_en;
	unsigned int hdmi_en;
	int global_enable;
	struct snd_soc_dai *cpu_dai;
	struct daudio_label *reg_label;

#ifdef CONFIG_SND_SUNXI_MAD
	struct work_struct ws_rx_mad_reset;
	struct sunxi_mad_priv *mad_priv;
#endif
};

#ifdef DAUDIO_CLASS_DEBUG
static struct reg_label reg_labels[] = {
	REG_LABEL(SUNXI_DAUDIO_CTL),
	REG_LABEL(SUNXI_DAUDIO_FMT0),
	REG_LABEL(SUNXI_DAUDIO_FMT1),
	REG_LABEL(SUNXI_DAUDIO_INTSTA),
	REG_LABEL(SUNXI_DAUDIO_FIFOCTL),
	REG_LABEL(SUNXI_DAUDIO_FIFOSTA),
	REG_LABEL(SUNXI_DAUDIO_INTCTL),
	REG_LABEL(SUNXI_DAUDIO_CLKDIV),
	REG_LABEL(SUNXI_DAUDIO_TXCNT),
	REG_LABEL(SUNXI_DAUDIO_RXCNT),
	REG_LABEL(SUNXI_DAUDIO_CHCFG),
	REG_LABEL(SUNXI_DAUDIO_TX0CHSEL),
#ifndef CONFIG_ARCH_SUN8IW18
	REG_LABEL(SUNXI_DAUDIO_TX1CHSEL),
	REG_LABEL(SUNXI_DAUDIO_TX2CHSEL),
	REG_LABEL(SUNXI_DAUDIO_TX3CHSEL),
#endif
#if defined(SUNXI_DAUDIO_MODE_B)
	REG_LABEL(SUNXI_DAUDIO_TX0CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX0CHMAP1),
	REG_LABEL(SUNXI_DAUDIO_TX1CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX1CHMAP1),
	REG_LABEL(SUNXI_DAUDIO_TX2CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX2CHMAP1),
	REG_LABEL(SUNXI_DAUDIO_TX3CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX3CHMAP1),
	REG_LABEL(SUNXI_DAUDIO_RXCHSEL),
	REG_LABEL(SUNXI_DAUDIO_RXCHMAP0),
	REG_LABEL(SUNXI_DAUDIO_RXCHMAP1),
#if defined(CONFIG_ARCH_SUN8IW18) || defined(CONFIG_ARCH_SUN8IW19) || \
	defined(CONFIG_ARCH_SUN50IW10)
	REG_LABEL(SUNXI_DAUDIO_RXCHMAP2),
	REG_LABEL(SUNXI_DAUDIO_RXCHMAP3),
#endif
	REG_LABEL(SUNXI_DAUDIO_DEBUG),
#else
	REG_LABEL(SUNXI_DAUDIO_TX0CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX1CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX2CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_TX3CHMAP0),
	REG_LABEL(SUNXI_DAUDIO_RXCHSEL),
	REG_LABEL(SUNXI_DAUDIO_RXCHMAP),
	REG_LABEL(SUNXI_DAUDIO_DEBUG),
#endif
	REG_LABEL_END,
};
#endif

static struct sunxi_daudio_info *sunxi_daudio_global[DAUDIO_NUM_MAX];
#ifdef DAUDIO_CLASS_DEBUG
static int daudio_class_dev_cnt;
#endif

/*
 *	Some codec on electric timing need debugging
 */
int daudio_set_clk_onoff(struct snd_soc_dai *dai, u32 mask, bool enable)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

	switch (mask) {
	case SUNXI_DAUDIO_BCLK:
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_FIFOCTL,
				(0x1 << BCLK_OUT), (enable << BCLK_OUT));
		break;
	case SUNXI_DAUDIO_LRCK:
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_FIFOCTL,
				(0x1 << LRCK_OUT), (enable << LRCK_OUT));
		break;
	case SUNXI_DAUDIO_MCLK:
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CLKDIV,
				(0x1 << MCLKOUT_EN), (enable << MCLKOUT_EN));
		break;
	case SUNXI_DAUDIO_GEN:
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(0x1 << GLOBAL_EN), (enable << GLOBAL_EN));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(daudio_set_clk_onoff);

static int sunxi_daudio_get_hub_mode(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct sunxi_daudio_info *sunxi_daudio =
					snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val;

	regmap_read(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL, &reg_val);
	ucontrol->value.integer.value[0] = ((reg_val & (1<<HUB_EN)) ? 2 : 1);

	return 0;
}

static int sunxi_daudio_set_hub_mode(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct sunxi_daudio_info *sunxi_daudio =
					snd_soc_codec_get_drvdata(codec);

	switch (ucontrol->value.integer.value[0]) {
	case	0:
	case	1:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<HUB_EN), (0<<HUB_EN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (0<<CTL_TXEN));
		sunxi_daudio->hub_mode = 0;
		break;
	case	2:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<HUB_EN), (1<<HUB_EN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (1<<CTL_TXEN));
		sunxi_daudio->hub_mode = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const char *daudio_format_function[] = {"null",
			"hub_disable", "hub_enable"};

static const struct soc_enum daudio_format_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(daudio_format_function),
			daudio_format_function),
};

/* dts pcm Audio Mode Select */
static const struct snd_kcontrol_new sunxi_daudio_controls[] = {
	SOC_ENUM_EXT("sunxi daudio audio hub mode", daudio_format_enum[0],
		sunxi_daudio_get_hub_mode, sunxi_daudio_set_hub_mode),
	SOC_SINGLE("sunxi daudio loopback debug", SUNXI_DAUDIO_CTL,
		LOOP_EN, 1, 0),
};

static void sunxi_daudio_txctrl_enable(struct sunxi_daudio_info *sunxi_daudio,
					bool enable)
{
	unsigned int reg_val = 0;

	if (enable) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_INTCTL,
					(1 << TXDRQEN), (1 << TXDRQEN));

		/* HDMI audio Transmit Clock just enable at startup */
		if (sunxi_daudio->pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) {
			regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
					(1 << SDO0_EN), (1 << SDO0_EN));

			regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
					(1 << CTL_TXEN), (1 << CTL_TXEN));
		}
	} else {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_INTCTL,
					(1 << TXDRQEN), (0 << TXDRQEN));

		if (sunxi_daudio->pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) {
			/* add this to avoid the i2s pop */
			while (1) {
				regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
					(1 << FIFO_CTL_FTX), (1 << FIFO_CTL_FTX));
				regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_TXCNT, 0);

				regmap_read(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOSTA, &reg_val);
				reg_val = ((reg_val & 0xFF0000) >> 16);
				if (reg_val == 0x80) {
					break;
				}
			}
			udelay(250);

			regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
					(1 << CTL_TXEN), (0 << CTL_TXEN));

			regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
					(1 << SDO0_EN), (0 << SDO0_EN));
		}
	}
}

static void sunxi_daudio_rxctrl_enable(struct sunxi_daudio_info *sunxi_daudio,
					bool enable)
{
	if (enable) {
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<CTL_RXEN), (1<<CTL_RXEN));
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_INTCTL,
				(1<<RXDRQEN), (1<<RXDRQEN));
#ifdef CONFIG_SND_SUN8IW8_TRIGGER_SYNC_WITH_DAUDIO_CAPTURE
		sun8iw8_codec_adc_drq_enable(1);
#endif

		#if SUNXI_RX_SYNC_EN
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<RX_EN_MUX), (1<<RX_EN_MUX));
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<RX_SYNC_EN), (1<<RX_SYNC_EN));
		#endif
	} else {
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_INTCTL,
				(1<<RXDRQEN), (0<<RXDRQEN));
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<CTL_RXEN), (0<<CTL_RXEN));
#ifdef CONFIG_SND_SUN8IW8_TRIGGER_SYNC_WITH_DAUDIO_CAPTURE
		sun8iw8_codec_adc_drq_enable(0);
#endif

		#if SUNXI_RX_SYNC_EN
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<RX_EN_MUX), (0<<RX_EN_MUX));
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<RX_SYNC_EN), (0<<RX_SYNC_EN));
		#endif
	}
}

#ifdef CONFIG_SND_SUNXI_MAD
/* should set it after sram reset. */
static int sunxi_daudio_mad_set_path(struct sunxi_daudio_info *sunxi_daudio,
				bool enable)
{
	enum mad_path_sel path_sel;

	switch (sunxi_daudio->pdata->tdm_num) {
	case 0:
		path_sel = MAD_PATH_I2S0;
		break;
	case 1:
		path_sel = MAD_PATH_I2S1;
		break;
	case 2:
		path_sel = MAD_PATH_I2S2;
		break;
	default:
		path_sel = MAD_PATH_NONE;
		dev_err(sunxi_daudio->dev, "unsupported I2S number!\n");
		return -EINVAL;
	}
	sunxi_mad_audio_source_sel(path_sel, enable);

	return 0;
}

static void sunxi_mad_debug_chan_echange(struct sunxi_daudio_info *sunxi_daudio)
{
	struct sunxi_mad_priv *mad_priv = sunxi_daudio->mad_priv;

	/*
	 * only for reset mad module by ccmu.
	 * should not be callback at dai_ops->trigger
	 * because it maybe running at interrupt!
	 */
	sunxi_mad_module_clk_enable(false);
	sunxi_mad_module_clk_enable(true);

	/* sunxi_mad_close();*/
	sunxi_mad_open();

	sunxi_mad_hw_params(mad_priv->audio_src_chan_num,
			mad_priv->sample_rate);

	sunxi_mad_audio_src_chan_num(mad_priv->audio_src_chan_num);
	sunxi_lpsd_chan_sel(mad_priv->lpsd_chan_sel);
	sunxi_mad_standby_chan_sel(mad_priv->mad_standby_chan_sel);
	sunxi_mad_dma_type(SUNXI_MAD_DMA_IO);
	sunxi_sram_ahb1_threshole_init();
	sunxi_mad_sram_init();

	/* should set it after sram reset. */
	sunxi_daudio_mad_set_path(sunxi_daudio, true);
}

static void sunxi_daudio_rxctrl_stop_mad_reset(struct work_struct *work)
{
	struct sunxi_daudio_info *sunxi_daudio =
		container_of(work, struct sunxi_daudio_info, ws_rx_mad_reset);

	/* should be flush fifo */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(1 << FIFO_CTL_FRX), (1 << FIFO_CTL_FRX));
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);

	sunxi_mad_debug_chan_echange(sunxi_daudio);

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(0x1 << DAUDIO_MAD_DATA_EN),
			(0x1 << DAUDIO_MAD_DATA_EN));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(1<<CTL_RXEN), (1<<CTL_RXEN));
}

static void sunxi_daudio_rxctrl_mad_enable(struct sunxi_daudio_info *sunxi_daudio,
					bool enable)
{
	if (enable) {
		/* should set it after sram reset. */
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x1 << DAUDIO_MAD_DATA_EN));
		sunxi_mad_dma_enable(true);

		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<CTL_RXEN), (1<<CTL_RXEN));
	} else {
		/* disable daudio */
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<CTL_RXEN), (0<<CTL_RXEN));
		sunxi_mad_dma_enable(false);

		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x0 << DAUDIO_MAD_DATA_EN));

		/* mad reset and daudio rxctrl enable */
		schedule_work(&(sunxi_daudio->ws_rx_mad_reset));
	}
}

static int sunxi_daudio_mad_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sunxi_snddaudio_priv *snddaudio_priv =
					snd_soc_card_get_drvdata(card);
	struct sunxi_mad_priv *mad_priv = &(snddaudio_priv->mad_priv);

	if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) &&
		(snddaudio_priv->mad_priv.mad_bind == 1)) {
		/* for reset sram point */
#ifdef MAD_CLK_ALWAYS_ON
		sunxi_mad_module_clk_enable(false);
#endif
		sunxi_mad_module_clk_enable(true);

		/* mad only receive the high 16bit */
		regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_RXOM_MASK << RXOM),
					(SUNXI_DAUDIO_RXOM_TUNL << RXOM));

		/* mad config */
		if (params_format(params) != SNDRV_PCM_FORMAT_S16_LE) {
			dev_err(sunxi_daudio->dev, "unsupported mad sample bits\n");
			return -EINVAL;
		}
		mad_priv->sample_rate = params_rate(params);
		mad_priv->audio_src_chan_num = params_channels(params);
		/* setup open status */
		sunxi_mad_open();

		/* setup hw_params status */
		if (sunxi_mad_hw_params(mad_priv->audio_src_chan_num,
				mad_priv->sample_rate)) {
			dev_err(sunxi_daudio->dev, "mad hw_params error.\n");
			sunxi_mad_close();
			return -EINVAL;
		}

		sunxi_mad_audio_src_chan_num(mad_priv->audio_src_chan_num);
		sunxi_lpsd_chan_sel(mad_priv->lpsd_chan_sel);
		sunxi_mad_standby_chan_sel(mad_priv->mad_standby_chan_sel);
		sunxi_sram_ahb1_threshole_init();
		/* changed it's dma addr */
		sunxi_sram_dma_config(&sunxi_daudio->capture_dma_param);
		sunxi_mad_dma_type(SUNXI_MAD_DMA_IO);
		/* sram sram reset for load setup */
		sunxi_mad_sram_init();
		/* should set it after sram reset. */
		sunxi_daudio_mad_set_path(sunxi_daudio, true);
	}

	return 0;
}
#endif

static int sunxi_daudio_global_enable(struct sunxi_daudio_info *sunxi_daudio,
					bool enable)
{
	if (enable) {
		if (sunxi_daudio->global_enable++ == 0)
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<GLOBAL_EN), (1<<GLOBAL_EN));
	} else {
		if (--sunxi_daudio->global_enable == 0)
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<GLOBAL_EN), (0<<GLOBAL_EN));
	}

	return 0;
}

static int sunxi_daudio_mclk_setting(struct sunxi_daudio_info *sunxi_daudio)
{
	unsigned int mclk_div;

	if (sunxi_daudio->pdata->mclk_div) {

		switch (sunxi_daudio->pdata->mclk_div) {
		case	1:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_1;
			break;
		case	2:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_2;
			break;
		case	4:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_3;
			break;
		case	6:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_4;
			break;
		case	8:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_5;
			break;
		case	12:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_6;
			break;
		case	16:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_7;
			break;
		case	24:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_8;
			break;
		case	32:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_9;
			break;
		case	48:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_10;
			break;
		case	64:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_11;
			break;
		case	96:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_12;
			break;
		case	128:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_13;
			break;
		case	176:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_14;
			break;
		case	192:
			mclk_div = SUNXI_DAUDIO_MCLK_DIV_15;
			break;
		default:
			dev_err(sunxi_daudio->dev, "unsupport  mclk_div\n");
			return -EINVAL;
		}
		/* setting Mclk as external codec input clk */
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CLKDIV,
			(SUNXI_DAUDIO_MCLK_DIV_MASK<<MCLK_DIV),
			(mclk_div<<MCLK_DIV));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CLKDIV,
				(1<<MCLKOUT_EN), (1<<MCLKOUT_EN));
	} else {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CLKDIV,
				(1<<MCLKOUT_EN), (0<<MCLKOUT_EN));
	}

	return 0;
}

static int sunxi_daudio_init_fmt(struct sunxi_daudio_info *sunxi_daudio,
				unsigned int fmt)
{
	unsigned int offset, mode;
	unsigned int lrck_polarity, brck_polarity;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case	SND_SOC_DAIFMT_CBM_CFM:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(SUNXI_DAUDIO_LRCK_OUT_MASK<<LRCK_OUT),
				(SUNXI_DAUDIO_LRCK_OUT_DISABLE<<LRCK_OUT));
		break;
	case	SND_SOC_DAIFMT_CBS_CFS:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(SUNXI_DAUDIO_LRCK_OUT_MASK<<LRCK_OUT),
				(SUNXI_DAUDIO_LRCK_OUT_ENABLE<<LRCK_OUT));
		break;
	default:
		dev_err(sunxi_daudio->dev, "unknown maser/slave format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case	SND_SOC_DAIFMT_I2S:
		offset = SUNXI_DAUDIO_TX_OFFSET_1;
		mode = SUNXI_DAUDIO_MODE_CTL_I2S;
		break;
	case	SND_SOC_DAIFMT_RIGHT_J:
		offset = SUNXI_DAUDIO_TX_OFFSET_0;
		mode = SUNXI_DAUDIO_MODE_CTL_RIGHT;
		break;
	case	SND_SOC_DAIFMT_LEFT_J:
		offset = SUNXI_DAUDIO_TX_OFFSET_0;
		mode = SUNXI_DAUDIO_MODE_CTL_LEFT;
		break;
	case	SND_SOC_DAIFMT_DSP_A:
		offset = SUNXI_DAUDIO_TX_OFFSET_1;
		mode = SUNXI_DAUDIO_MODE_CTL_PCM;
		break;
	case	SND_SOC_DAIFMT_DSP_B:
		offset = SUNXI_DAUDIO_TX_OFFSET_0;
		mode = SUNXI_DAUDIO_MODE_CTL_PCM;
		break;
	default:
		dev_err(sunxi_daudio->dev, "format setting failed\n");
		return -EINVAL;
	}

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(SUNXI_DAUDIO_MODE_CTL_MASK<<MODE_SEL),
			(mode<<MODE_SEL));

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_TX0CHSEL,
			(SUNXI_DAUDIO_TX_OFFSET_MASK<<TX_OFFSET),
			(offset<<TX_OFFSET));

	if (sunxi_daudio->hdmi_en) {
#ifndef CONFIG_ARCH_SUN8IW18
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_TX1CHSEL,
			(SUNXI_DAUDIO_TX_OFFSET_MASK<<TX_OFFSET),
			(offset<<TX_OFFSET));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_TX2CHSEL,
			(SUNXI_DAUDIO_TX_OFFSET_MASK<<TX_OFFSET),
			(offset<<TX_OFFSET));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_TX3CHSEL,
			(SUNXI_DAUDIO_TX_OFFSET_MASK<<TX_OFFSET),
			(offset<<TX_OFFSET));
#endif
	}

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCHSEL,
			(SUNXI_DAUDIO_RX_OFFSET_MASK<<RX_OFFSET),
			(offset<<RX_OFFSET));

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case	SND_SOC_DAIFMT_NB_NF:
		lrck_polarity = SUNXI_DAUDIO_LRCK_POLARITY_NOR;
		brck_polarity = SUNXI_DAUDIO_BCLK_POLARITY_NOR;
		break;
	case	SND_SOC_DAIFMT_NB_IF:
		lrck_polarity = SUNXI_DAUDIO_LRCK_POLARITY_INV;
		brck_polarity = SUNXI_DAUDIO_BCLK_POLARITY_NOR;
		break;
	case	SND_SOC_DAIFMT_IB_NF:
		lrck_polarity = SUNXI_DAUDIO_LRCK_POLARITY_NOR;
		brck_polarity = SUNXI_DAUDIO_BCLK_POLARITY_INV;
		break;
	case	SND_SOC_DAIFMT_IB_IF:
		lrck_polarity = SUNXI_DAUDIO_LRCK_POLARITY_INV;
		brck_polarity = SUNXI_DAUDIO_BCLK_POLARITY_INV;
		break;
	default:
		dev_err(sunxi_daudio->dev, "invert clk setting failed\n");
		return -EINVAL;
	}

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(1<<LRCK_POLARITY), (lrck_polarity<<LRCK_POLARITY));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(1<<BRCK_POLARITY), (brck_polarity<<BRCK_POLARITY));

	return 0;
}

static int sunxi_daudio_init(struct sunxi_daudio_info *sunxi_daudio)
{
	struct sunxi_daudio_platform_data *pdat = sunxi_daudio->pdata;

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(1 << LRCK_WIDTH),
			(pdat->frame_type << LRCK_WIDTH));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(SUNXI_DAUDIO_LRCK_PERIOD_MASK) << LRCK_PERIOD,
			((pdat->pcm_lrck_period - 1) << LRCK_PERIOD));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(SUNXI_DAUDIO_SLOT_WIDTH_MASK << SLOT_WIDTH),
			(((pdat->slot_width_select >> 2) - 1) << SLOT_WIDTH));

	/*
	 * MSB on the transmit format, always be first.
	 * default using Linear-PCM, without no companding.
	 * A-law<Eourpean standard> or U-law<US-Japan> not working ok.
	 */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x1 <<  TX_MLS),
			(pdat->msb_lsb_first << TX_MLS));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x1 <<  RX_MLS),
			(pdat->msb_lsb_first << RX_MLS));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x3 <<  SEXT),
			(pdat->sign_extend << SEXT));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x3 <<  TX_PDM),
			(pdat->tx_data_mode << TX_PDM));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x3 <<  RX_PDM),
			(pdat->rx_data_mode << RX_PDM));

	/* maybe set the i2s fmt for hdmi at init */
	if (pdat->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE)
		sunxi_daudio_init_fmt(sunxi_daudio, (pdat->audio_format
			| (pdat->signal_inversion << SND_SOC_DAIFMT_SIG_SHIFT)
			| (pdat->daudio_master << SND_SOC_DAIFMT_MASTER_SHIFT)));

	return sunxi_daudio_mclk_setting(sunxi_daudio);
}

#ifdef CONFIG_SUNXI_MPP_AIO
static int sunxi_daudio_mpp_aio_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int bitwidth = 0;

	if (mpp_audio_debugfs) {
		mpp_aio_info_set(mpp_audio_debugfs->ao_devenable, 32, 1);
		mpp_aio_info_set(mpp_audio_debugfs->ao_cardtype, 64, 1);
		mpp_aio_info_set(mpp_audio_debugfs->ao_samplerate, 32,
				params_rate(params));

		switch (params_format(params)) {
		case	SNDRV_PCM_FORMAT_S16_LE:
			bitwidth = 16;
			break;
		case	SNDRV_PCM_FORMAT_S20_3LE:
			bitwidth = 20;
			break;
		case	SNDRV_PCM_FORMAT_S24_LE:
			bitwidth = 24;
			break;
		case	SNDRV_PCM_FORMAT_S32_LE:
			bitwidth = 32;
			break;
		default:
			bitwidth = 16;
			break;
		}
		mpp_aio_info_set(mpp_audio_debugfs->ao_bitwidth, 32, bitwidth);
	}
	return 0;
}
#endif

static int sunxi_daudio_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sndhdmi_priv *sndhdmi_priv = snd_soc_card_get_drvdata(card);

	switch (params_format(params)) {
	case	SNDRV_PCM_FORMAT_S16_LE:
		/*
		 * Special procesing for hdmi, HDMI card name is
		 * "sndhdmi" or sndhdmiraw. if card not HDMI,
		 * strstr func just return NULL, jump to right section.
		 * Not HDMI card, sunxi_hdmi maybe a NULL pointer.
		 */
		if (sunxi_daudio->pdata->daudio_type ==
				SUNXI_DAUDIO_TDMHDMI_TYPE
				&& (sndhdmi_priv->hdmi_format > 1)) {
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_FMT0,
				(SUNXI_DAUDIO_SR_MASK<<DAUDIO_SAMPLE_RESOLUTION),
				(SUNXI_DAUDIO_SR_24BIT<<DAUDIO_SAMPLE_RESOLUTION));
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_TXIM_MASK<<TXIM),
					(SUNXI_DAUDIO_TXIM_VALID_MSB<<TXIM));
		} else {
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_FMT0,
				(SUNXI_DAUDIO_SR_MASK<<DAUDIO_SAMPLE_RESOLUTION),
				(SUNXI_DAUDIO_SR_16BIT<<DAUDIO_SAMPLE_RESOLUTION));
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_TXIM_MASK<<TXIM),
					(SUNXI_DAUDIO_TXIM_VALID_LSB<<TXIM));
			else
				regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_RXOM_MASK<<RXOM),
					(SUNXI_DAUDIO_RXOM_EXPH<<RXOM));
		}
		break;
	case	SNDRV_PCM_FORMAT_S20_3LE:
	case	SNDRV_PCM_FORMAT_S24_LE:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
				(SUNXI_DAUDIO_SR_MASK<<DAUDIO_SAMPLE_RESOLUTION),
				(SUNXI_DAUDIO_SR_24BIT<<DAUDIO_SAMPLE_RESOLUTION));
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_TXIM_MASK<<TXIM),
					(SUNXI_DAUDIO_TXIM_VALID_LSB<<TXIM));
		else
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_RXOM_MASK<<RXOM),
					(SUNXI_DAUDIO_RXOM_EXPH<<RXOM));
		break;
	case	SNDRV_PCM_FORMAT_S32_LE:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
				(SUNXI_DAUDIO_SR_MASK<<DAUDIO_SAMPLE_RESOLUTION),
				(SUNXI_DAUDIO_SR_32BIT<<DAUDIO_SAMPLE_RESOLUTION));
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_TXIM_MASK<<TXIM),
					(SUNXI_DAUDIO_TXIM_VALID_LSB<<TXIM));
		else
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_RXOM_MASK<<RXOM),
					(SUNXI_DAUDIO_RXOM_EXPH<<RXOM));
		break;
	default:
		dev_err(sunxi_daudio->dev, "unrecognized format\n");
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CHCFG,
				(SUNXI_DAUDIO_TX_SLOT_MASK<<TX_SLOT_NUM),
				((params_channels(params)-1)<<TX_SLOT_NUM));
		if (sunxi_daudio->hdmi_en == 0) {
#ifdef SUNXI_DAUDIO_MODE_B
			regmap_write(sunxi_daudio->regmap,
				SUNXI_DAUDIO_TX0CHMAP0, SUNXI_DEFAULT_CHMAP0);
			regmap_write(sunxi_daudio->regmap,
				SUNXI_DAUDIO_TX0CHMAP1, SUNXI_DEFAULT_CHMAP1);
#else
			regmap_write(sunxi_daudio->regmap,
				SUNXI_DAUDIO_TX0CHMAP0, SUNXI_DEFAULT_CHMAP);
#endif
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_TX0CHSEL,
				(SUNXI_DAUDIO_TX_CHSEL_MASK<<TX_CHSEL),
				((params_channels(params)-1)<<TX_CHSEL));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_TX0CHSEL,
				(SUNXI_DAUDIO_TX_CHEN_MASK<<TX_CHEN),
				((1<<params_channels(params))-1)<<TX_CHEN);
		} else {
			/* HDMI multi-channel processing */
#ifdef SUNXI_HDMI_AUDIO_ENABLE
#ifdef SUNXI_DAUDIO_MODE_B
			regmap_write(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX0CHMAP1, 0x10);
			if (sndhdmi_priv->hdmi_format > 1) {
				regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX1CHMAP1, 0x32);
				regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX2CHMAP1, 0x54);
				regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX3CHMAP1, 0x76);
			} else {
				if (params_channels(params) > 2)
					regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX1CHMAP1, 0x23);
				if (params_channels(params) > 4) {
					if (params_channels(params) == 6)
						regmap_write(
							sunxi_daudio->regmap,
							SUNXI_DAUDIO_TX2CHMAP1,
							0x54);
					else
						regmap_write(
							sunxi_daudio->regmap,
							SUNXI_DAUDIO_TX2CHMAP1,
							0x76);
				}
				if (params_channels(params) > 6)
					regmap_write(sunxi_daudio->regmap,
							SUNXI_DAUDIO_TX3CHMAP1,
							0x54);
			}
#else
			regmap_write(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX0CHMAP0, 0x10);
			if (sndhdmi_priv->hdmi_format > 1) {
				/* support for HBR */
				regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX1CHMAP0, 0x32);
				regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX2CHMAP0, 0x54);
				regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX3CHMAP0, 0x76);
			} else {
				/* LPCM 5.1 & 7.1 support */
				if (params_channels(params) > 2)
					regmap_write(sunxi_daudio->regmap,
						SUNXI_DAUDIO_TX1CHMAP0, 0x23);
				if (params_channels(params) > 4) {
					if (params_channels(params) == 6)
						regmap_write(
							sunxi_daudio->regmap,
							SUNXI_DAUDIO_TX2CHMAP0,
							0x54);
					else
						regmap_write(
							sunxi_daudio->regmap,
							SUNXI_DAUDIO_TX2CHMAP0,
							0x76);
				}
				if (params_channels(params) > 6)
					regmap_write(sunxi_daudio->regmap,
							SUNXI_DAUDIO_TX3CHMAP0,
							0x54);
			}
#endif
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX0CHSEL,
					0x01 << TX_CHSEL, 0x01 << TX_CHSEL);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX0CHSEL,
					0x03 << TX_CHEN, 0x03 << TX_CHEN);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX1CHSEL,
					0x01 << TX_CHSEL, 0x01 << TX_CHSEL);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX1CHSEL,
					(0x03)<<TX_CHEN, 0x03 << TX_CHEN);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX2CHSEL,
					0x01 << TX_CHSEL, 0x01 << TX_CHSEL);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX2CHSEL,
					(0x03)<<TX_CHEN, 0x03 << TX_CHEN);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX3CHSEL,
					0x01 << TX_CHSEL, 0x01 << TX_CHSEL);
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_TX3CHSEL,
					(0x03)<<TX_CHEN, 0x03 << TX_CHEN);
#endif	/* HDMI */
		}
	} else {
#ifdef SUNXI_DAUDIO_MODE_B
#if defined(CONFIG_ARCH_SUN8IW18) || defined(CONFIG_ARCH_SUN8IW19) || \
	defined(CONFIG_ARCH_SUN50IW10)
		unsigned int SUNXI_DAUDIO_RXCHMAPX = 0;
		int index = 0;

		for (index = 0; index < 16; index++) {
			if (index >= 12)
				SUNXI_DAUDIO_RXCHMAPX = SUNXI_DAUDIO_RXCHMAP0;
			else if (index >= 8)
				SUNXI_DAUDIO_RXCHMAPX = SUNXI_DAUDIO_RXCHMAP1;
			else if (index >= 4)
				SUNXI_DAUDIO_RXCHMAPX = SUNXI_DAUDIO_RXCHMAP2;
			else
				SUNXI_DAUDIO_RXCHMAPX = SUNXI_DAUDIO_RXCHMAP3;
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_RXCHMAPX,
				DAUDIO_RXCHMAP(index),
				DAUDIO_RXCH_DEF_MAP(index));
		}
#else
		regmap_write(sunxi_daudio->regmap,
				SUNXI_DAUDIO_RXCHMAP0, SUNXI_DEFAULT_CHMAP0);
		regmap_write(sunxi_daudio->regmap,
				SUNXI_DAUDIO_RXCHMAP1, SUNXI_DEFAULT_CHMAP1);
#endif
#else
		regmap_write(sunxi_daudio->regmap,
				SUNXI_DAUDIO_RXCHMAP, SUNXI_DEFAULT_CHMAP);
#endif
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CHCFG,
				(SUNXI_DAUDIO_RX_SLOT_MASK<<RX_SLOT_NUM),
				((params_channels(params)-1)<<RX_SLOT_NUM));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCHSEL,
				(SUNXI_DAUDIO_RX_CHSEL_MASK<<RX_CHSEL),
				((params_channels(params)-1)<<RX_CHSEL));
	}
#ifdef SUNXI_HDMI_AUDIO_ENABLE
	/* Special processing for HDMI hub playback to enable hdmi module */
	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		mutex_lock(&sunxi_daudio->mutex);
		if (sunxi_daudio->hub_mode) {
			sunxi_hdmi_codec_hw_params(substream, params, NULL);
			sunxi_hdmi_codec_prepare(substream, NULL);
		}
		mutex_unlock(&sunxi_daudio->mutex);
#ifdef CONFIG_SUNXI_MPP_AIO
		sunxi_daudio_mpp_aio_hw_params(substream, params, dai);
#endif
	}
#endif

#ifdef CONFIG_SND_SUNXI_MAD
	/*mad only supported 16k/48KHz samplerate when capturing*/
	return sunxi_daudio_mad_hw_params(substream, params, dai);
#else
	return 0;
#endif
}

static int sunxi_daudio_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

#ifdef USE_ASOC_FMT_SETTING
	sunxi_daudio_init_fmt(sunxi_daudio, fmt);
#else
	struct sunxi_daudio_platform_data *pdat = sunxi_daudio->pdata;

	sunxi_daudio_init_fmt(sunxi_daudio, (pdat->audio_format
		| (pdat->signal_inversion << SND_SOC_DAIFMT_SIG_SHIFT)
		| (pdat->daudio_master << SND_SOC_DAIFMT_MASTER_SHIFT)));
#endif
	return 0;
}

static int sunxi_daudio_set_sysclk(struct snd_soc_dai *dai,
			int clk_id, unsigned int freq, int dir)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

#if defined(DAUDIO_PLL_AUDIO_X4) && defined(CONFIG_ARCH_SUN50IW10)
	if (clk_set_rate(sunxi_daudio->pllclkx4, freq * 4)) {
		dev_err(sunxi_daudio->dev, "set pllclkx4 rate failed\n");

		return -EBUSY;
	}

	if (clk_set_rate(sunxi_daudio->moduleclk, freq)) {
		dev_err(sunxi_daudio->dev, "set moduleclk rate failed\n");

		return -EBUSY;
	}
#else
	if (clk_set_rate(sunxi_daudio->pllclk, freq)) {
		dev_err(sunxi_daudio->dev, "set pllclk rate failed\n");

		return -EBUSY;
	}
#endif

	return 0;
}

static int sunxi_daudio_set_clkdiv(struct snd_soc_dai *dai,
				int clk_id, int clk_div)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	unsigned int bclk_div, div_ratio;

	if (sunxi_daudio->pdata->tdm_config)
		/* I2S/TDM two channel mode */
		div_ratio = clk_div/(2 * sunxi_daudio->pdata->pcm_lrck_period);
	else
		/* PCM mode */
		div_ratio = clk_div / sunxi_daudio->pdata->pcm_lrck_period;

	switch (div_ratio) {
	case	1:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_1;
		break;
	case	2:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_2;
		break;
	case	4:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_3;
		break;
	case	6:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_4;
		break;
	case	8:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_5;
		break;
	case	12:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_6;
		break;
	case	16:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_7;
		break;
	case	24:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_8;
		break;
	case	32:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_9;
		break;
	case	48:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_10;
		break;
	case	64:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_11;
		break;
	case	96:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_12;
		break;
	case	128:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_13;
		break;
	case	176:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_14;
		break;
	case	192:
		bclk_div = SUNXI_DAUDIO_BCLK_DIV_15;
		break;
	default:
		dev_err(sunxi_daudio->dev, "unsupport clk_div\n");
		return -EINVAL;
	}
	/* setting bclk to driver external codec bit clk */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CLKDIV,
			(SUNXI_DAUDIO_BCLK_DIV_MASK<<BCLK_DIV),
			(bclk_div<<BCLK_DIV));

	return 0;
}

#ifdef CONFIG_SND_SUNXI_MAD
static int sunxi_daudio_dai_mad_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sunxi_snddaudio_priv *snddaudio_priv =
					snd_soc_card_get_drvdata(card);

	if (snddaudio_priv->mad_priv.sunxi_mad != sunxi_mad_get_mad_info())
		snddaudio_priv->mad_priv.sunxi_mad = sunxi_mad_get_mad_info();

	sunxi_daudio->mad_priv = &(snddaudio_priv->mad_priv);

	return 0;
}
#endif

static int sunxi_daudio_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

	/* FIXME: As HDMI module to play audio, it need at least 1100ms to sync.
	 * if we not wait we lost audio data to playback, or we wait for 1100ms
	 * to playback, user experience worst than you can imagine. So we need
	 * to cutdown that sync time by keeping clock signal on. we just enable
	 * it at startup and resume, cutdown it at remove and suspend time.
	 */
	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE)
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (1<<CTL_TXEN));
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_dai_set_dma_data(dai, substream,
					&sunxi_daudio->playback_dma_param);
	} else {
		snd_soc_dai_set_dma_data(dai, substream,
			&sunxi_daudio->capture_dma_param);
	}

#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_daudio_dai_mad_startup(substream, dai);
#endif

	return 0;
}

static int sunxi_daudio_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	struct sunxi_daudio_platform_data *pdata = sunxi_daudio->pdata;

	switch (cmd) {
	case	SNDRV_PCM_TRIGGER_START:
	case	SNDRV_PCM_TRIGGER_RESUME:
	case	SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/* Global enable I2S/TDM module */
			if ((pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) &&
				(!sunxi_daudio->playback_en)) {
					sunxi_daudio_global_enable(sunxi_daudio, true);
				sunxi_daudio->playback_en = 1;
			}
			sunxi_daudio_txctrl_enable(sunxi_daudio, true);
		} else {
#ifdef CONFIG_SND_SUNXI_MAD
			if (pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) {
				switch (sunxi_daudio->mad_priv->mad_bind) {
				case 0:
				default:
					sunxi_daudio_rxctrl_enable(sunxi_daudio, true);
					sunxi_daudio_global_enable(sunxi_daudio, true);
					break;
				case 1:
					sunxi_daudio_rxctrl_mad_enable(sunxi_daudio, true);
					if (sunxi_daudio->capture_en)
						break;
					sunxi_daudio_global_enable(sunxi_daudio, true);
					sunxi_daudio->capture_en = 1;
					break;
				}
			} else
				sunxi_daudio_rxctrl_enable(sunxi_daudio, true);
#else
			if (pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE)
				sunxi_daudio_global_enable(sunxi_daudio, true);

			sunxi_daudio_rxctrl_enable(sunxi_daudio, true);
#endif
		}
		break;
	case	SNDRV_PCM_TRIGGER_STOP:
	case	SNDRV_PCM_TRIGGER_SUSPEND:
	case	SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/* Global enable I2S/TDM module */
			if ((pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) &&
				(sunxi_daudio->playback_en)) {
					sunxi_daudio_global_enable(sunxi_daudio, false);
				sunxi_daudio->playback_en = 0;
			}
			sunxi_daudio_txctrl_enable(sunxi_daudio, false);
		} else {
			if (sunxi_daudio->pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) {
				sunxi_daudio_global_enable(sunxi_daudio, false);
#ifdef CONFIG_SND_SUNXI_MAD
				/* should not shutdown when capturing at mad working. */
				if (sunxi_daudio->mad_priv->mad_bind) {
					sunxi_daudio_rxctrl_mad_enable(sunxi_daudio, false);
					if (sunxi_daudio->capture_en)
						break;
				} else
#endif
				{
					sunxi_daudio_rxctrl_enable(sunxi_daudio, false);
				}
			} else
				sunxi_daudio_rxctrl_enable(sunxi_daudio, false);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_SND_SUNXI_MAD
static int sunxi_daudio_mad_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sunxi_snddaudio_priv *snddaudio_priv =
					snd_soc_card_get_drvdata(card);
	struct sunxi_mad_priv *mad_priv = &(snddaudio_priv->mad_priv);
	unsigned int reg_val = 0;

	if ((substream->runtime->status->state != SNDRV_PCM_STATE_XRUN)
		&& (mad_priv->mad_bind && sunxi_daudio->capture_en)) {
		regmap_read(sunxi_daudio->regmap, SUNXI_DAUDIO_INTSTA, &reg_val);
		pr_warn("[%s] %d SUNXI_DAUDIO_INTSTA[0x%x]: 0x%x.\n",
			__func__, __LINE__, SUNXI_DAUDIO_INTSTA, reg_val);
		return 0;
	}

	if ((substream->runtime->status->state == SNDRV_PCM_STATE_XRUN)
		&& sunxi_daudio->capture_en) {
		regmap_read(sunxi_daudio->regmap, SUNXI_DAUDIO_INTSTA, &reg_val);
		pr_warn("[%s] %d SUNXI_DAUDIO_INTSTA[0x%x]: 0x%x.\n",
			__func__, __LINE__, SUNXI_DAUDIO_INTSTA, reg_val);
		pr_warn("[%s] PCM occurs xrun.\n", __func__);

		if (mad_priv->mad_bind)
			return 0;
	}

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(1<<FIFO_CTL_FRX), (1<<FIFO_CTL_FRX));
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);

	return 0;
}
#endif

static int sunxi_daudio_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	unsigned int i;
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0 ; i < SUNXI_DAUDIO_FTX_TIMES ; i++) {
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_FIFOCTL,
				(1<<FIFO_CTL_FTX), (1<<FIFO_CTL_FTX));
			mdelay(1);
		}
		regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_TXCNT, 0);

		if (sunxi_daudio->hub_mode &&
			sunxi_daudio->pdata->daudio_type != SUNXI_DAUDIO_TDMHDMI_TYPE) {
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_CTL,
					(1 << SDO0_EN), (1 << SDO0_EN));
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_CTL,
					(1 << CTL_TXEN), (1 << CTL_TXEN));
			mutex_lock(&sunxi_daudio->global_mutex);
			sunxi_daudio_global_enable(sunxi_daudio, true);
			sunxi_daudio->playback_en = 1;
			mutex_unlock(&sunxi_daudio->global_mutex);
		}
	} else {
#ifdef CONFIG_SND_SUNXI_MAD
		sunxi_daudio_mad_prepare(substream, dai);
#else
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<FIFO_CTL_FRX), (1<<FIFO_CTL_FRX));
		regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);
#endif
	}

	return 0;
}

static int sunxi_daudio_probe(struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

	mutex_init(&sunxi_daudio->mutex);
	mutex_init(&sunxi_daudio->global_mutex);
	sunxi_daudio_init(sunxi_daudio);

	return 0;
}

#ifdef CONFIG_SND_SUNXI_MAD
static void sunxi_daudio_mad_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sunxi_snddaudio_priv *snddaudio_priv =
					snd_soc_card_get_drvdata(card);

	if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) &&
		(snddaudio_priv->mad_priv.mad_bind == 1)) {
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<CTL_RXEN), (0<<CTL_RXEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x0 << DAUDIO_MAD_DATA_EN));

		/* diable daudio src*/
		sunxi_daudio_mad_set_path(sunxi_daudio, false);

		sunxi_mad_close();

		sunxi_daudio->capture_en = 0;
		sunxi_daudio_global_enable(sunxi_daudio, false);
#ifndef MAD_CLK_ALWAYS_ON
		sunxi_mad_module_clk_enable(false);
#endif
		/* if not use mad again*/
		sunxi_daudio->capture_dma_param.dma_addr =
			sunxi_daudio->res.start + SUNXI_DAUDIO_RXFIFO;

		switch (sunxi_daudio->pdata->tdm_num) {
		case 0:
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 0);
			break;
		case 1:
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 1);
			break;
		case 2:
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 2);
			break;
		case 3:
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 3);
			break;
		default:
			pr_alert("tdm_num setting overflow\n");
			break;
		}
	}
}
#endif

static void sunxi_daudio_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

	/* Special processing for HDMI hub playback to shutdown hdmi module */
	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		mutex_lock(&sunxi_daudio->mutex);
		if (sunxi_daudio->hub_mode)
			sunxi_hdmi_codec_shutdown(substream, NULL);
		mutex_unlock(&sunxi_daudio->mutex);
	} else if ((substream->stream == SNDRV_PCM_STREAM_PLAYBACK) &&
		(sunxi_daudio->hub_mode)) {
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1 << SDO0_EN), (0 << SDO0_EN));
		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1 << CTL_TXEN), (0 << CTL_TXEN));
		if (sunxi_daudio->playback_en) {
			mutex_lock(&sunxi_daudio->global_mutex);
			sunxi_daudio_global_enable(sunxi_daudio, false);
			mutex_unlock(&sunxi_daudio->global_mutex);
			sunxi_daudio->playback_en = 0;
		}
		return;
	}
#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_daudio_mad_shutdown(substream, dai);
#endif
}

static int sunxi_daudio_remove(struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);

	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE)
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (0<<CTL_TXEN));

	return 0;
}

#ifdef DAUDIO_STANDBY_DEBUG
static int daudio_standby_debug;

static int sunxi_daudio_debug_suspend(struct sunxi_daudio_info *sunxi_daudio)
{
	int ret = 0;
	int i = 0;

	pr_debug("[daudio] suspend .%s start\n", dev_name(sunxi_daudio->dev));

	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		/* Global disable I2S/TDM module */
		mutex_lock(&sunxi_daudio->global_mutex);
		sunxi_daudio_global_enable(sunxi_daudio, false);
		mutex_unlock(&sunxi_daudio->global_mutex);
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (0<<CTL_TXEN));
	}

	sunxi_daudio->reg_label = kzalloc(sizeof(struct daudio_label) * SUNXI_DAUDIO_REG_NUM_MAX,
						GFP_KERNEL);
	/* for save daudio reg */
	for (i = 0; i < SUNXI_DAUDIO_REG_NUM_MAX; i++)
		regmap_read(sunxi_daudio->regmap, i << 2,
				&((sunxi_daudio->reg_label+i)->value));

	if (sunxi_daudio->moduleclk != NULL)
		clk_disable_unprepare(sunxi_daudio->moduleclk);
#ifdef DAUDIO_PLL_AUDIO_X4
	if (sunxi_daudio->pllclkx4 != NULL)
		clk_disable_unprepare(sunxi_daudio->pllclkx4);
#endif
	if (sunxi_daudio->pllclk != NULL)
		clk_disable_unprepare(sunxi_daudio->pllclk);

	if (sunxi_daudio->pdata->external_type) {
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
				sunxi_daudio->pinstate_sleep);
		if (ret) {
			pr_warn("[daudio]select pin sleep state failed\n");
			return ret;
		}
		devm_pinctrl_put(sunxi_daudio->pinctrl);
	}

	if (sunxi_daudio->vol_supply.daudio_regulator)
		regulator_disable(sunxi_daudio->vol_supply.daudio_regulator);

	pr_debug("[daudio] suspend .%s end\n", dev_name(sunxi_daudio->dev));

	return 0;
}

static int sunxi_daudio_debug_resume(struct sunxi_daudio_info *sunxi_daudio)
{
	int ret = 0;
	int i = 0;

	pr_debug("[%s] resume .%s start\n", __func__,
			dev_name(sunxi_daudio->dev));

	if (sunxi_daudio->vol_supply.daudio_regulator) {
		ret = regulator_enable(sunxi_daudio->vol_supply.daudio_regulator);
		if (ret) {
			dev_err(sunxi_daudio->dev,
				"resume enable duaido[%d] vcc-pin failed\n",
				sunxi_daudio->pdata->tdm_num);
				goto err_resume_out;
		}
	}

	if (clk_prepare_enable(sunxi_daudio->pllclk)) {
		dev_err(sunxi_daudio->dev, "pllclk resume failed\n");
		ret = -EBUSY;
		goto err_resume_out;
	}
#ifdef DAUDIO_PLL_AUDIO_X4
	if (clk_prepare_enable(sunxi_daudio->pllclkx4)) {
		dev_err(sunxi_daudio->dev, "pllclkx4 resume failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
	if (clk_prepare_enable(sunxi_daudio->moduleclk)) {
		dev_err(sunxi_daudio->dev, "moduleclk resume failed\n");
		ret = -EBUSY;
		goto err_pllclkx4_disable;
	}
#else
	if (clk_prepare_enable(sunxi_daudio->moduleclk)) {
		dev_err(sunxi_daudio->dev, "moduleclk resume failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
#endif

	if (sunxi_daudio->pdata->external_type) {
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
					sunxi_daudio->pinstate);
		if (ret) {
			dev_warn(sunxi_daudio->dev,
				"daudio set pinctrl default state fail\n");
		}
#ifdef DAUDIO_PINCTRL_STATE_DEFAULT_B
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
					sunxi_daudio->pinstate_b);
		if (ret) {
			dev_warn(sunxi_daudio->dev,
				"daudio set pinctrl pins_b state fail\n");
		}
#endif
	}

	sunxi_daudio_init(sunxi_daudio);

	/* for echo the save reg */
	for (i = 0; i < SUNXI_DAUDIO_REG_NUM_MAX; i++)
		regmap_write(sunxi_daudio->regmap, i << 2,
				(sunxi_daudio->reg_label+i)->value);
	kfree(sunxi_daudio->reg_label);
	/* for clear fifo */
	for (i = 0 ; i < SUNXI_DAUDIO_FTX_TIMES ; i++) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<FIFO_CTL_FTX), (1<<FIFO_CTL_FTX));
		mdelay(1);
	}
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_TXCNT, 0);
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<FIFO_CTL_FRX), (1<<FIFO_CTL_FRX));
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);

	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<SDO0_EN), (1<<SDO0_EN));
		if (sunxi_daudio->hdmi_en) {
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO1_EN), (1<<SDO1_EN));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO2_EN), (1<<SDO2_EN));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO3_EN), (1<<SDO3_EN));
		}
		/* Global enable I2S/TDM module */
		mutex_lock(&sunxi_daudio->global_mutex);
		sunxi_daudio_global_enable(sunxi_daudio, true);
		mutex_unlock(&sunxi_daudio->global_mutex);
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (1<<CTL_TXEN));
	}

	pr_debug("[%s] resume .%s end\n", __func__,
			dev_name(sunxi_daudio->dev));
	return 0;

#ifdef DAUDIO_PLL_AUDIO_X4
err_pllclkx4_disable:
	clk_disable_unprepare(sunxi_daudio->pllclkx4);
#endif
err_pllclk_disable:
	clk_disable_unprepare(sunxi_daudio->pllclk);
err_resume_out:
	return ret;
}
#endif

static int sunxi_daudio_suspend(struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	int ret = 0;
	int i = 0;

	pr_debug("[daudio] suspend .%s start\n", dev_name(sunxi_daudio->dev));

#ifdef CONFIG_SND_SUNXI_MAD
	if (sunxi_daudio->mad_priv && sunxi_daudio->mad_priv->mad_bind
		&& (sunxi_daudio->capture_en == 1)) {
		/*
		 * must ensure that the sunxi_daudio_rxctrl_stop_mad_reset
		 * was calling has finished. Because mad need the data source.
		 */
		sunxi_mad_suspend_external();
		pr_debug("mad suspend succeed %s\n", __func__);
		return 0;
	}
#endif

	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		/* Global disable I2S/TDM module */
		mutex_lock(&sunxi_daudio->global_mutex);
		sunxi_daudio_global_enable(sunxi_daudio, 0);
		mutex_unlock(&sunxi_daudio->global_mutex);
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (0<<CTL_TXEN));
	}

	sunxi_daudio->reg_label = kzalloc(sizeof(struct daudio_label) * SUNXI_DAUDIO_REG_NUM_MAX,
						GFP_KERNEL);
	/* for save daudio reg */
	for (i = 0; i < SUNXI_DAUDIO_REG_NUM_MAX; i++)
		regmap_read(sunxi_daudio->regmap, i << 2,
				&((sunxi_daudio->reg_label+i)->value));

	if (sunxi_daudio->moduleclk != NULL)
		clk_disable_unprepare(sunxi_daudio->moduleclk);
#ifdef DAUDIO_PLL_AUDIO_X4
	if (sunxi_daudio->pllclkx4 != NULL)
		clk_disable_unprepare(sunxi_daudio->pllclkx4);
#endif
	if (sunxi_daudio->pllclk != NULL)
		clk_disable_unprepare(sunxi_daudio->pllclk);

	if (sunxi_daudio->pdata->external_type) {
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
				sunxi_daudio->pinstate_sleep);
		if (ret) {
			pr_warn("[daudio]select pin sleep state failed\n");
			return ret;
		}
	}

	if (sunxi_daudio->vol_supply.daudio_regulator)
		regulator_disable(sunxi_daudio->vol_supply.daudio_regulator);

	pr_debug("[daudio] suspend .%s end \n", dev_name(sunxi_daudio->dev));
	return 0;
}

static int sunxi_daudio_resume(struct snd_soc_dai *dai)
{
	struct sunxi_daudio_info *sunxi_daudio = snd_soc_dai_get_drvdata(dai);
	int ret;
	int i = 0;

	pr_debug("[%s] resume .%s start\n", __func__,
			dev_name(sunxi_daudio->dev));

#ifdef CONFIG_SND_SUNXI_MAD
	if (sunxi_daudio->mad_priv && sunxi_daudio->mad_priv->mad_bind
		&& (sunxi_daudio->capture_en == 1)) {
		sunxi_mad_resume_external();
		pr_debug("mad resume succeed %s\n", __func__);
		return 0;
	}
#endif
	if (sunxi_daudio->vol_supply.daudio_regulator) {
		ret = regulator_enable(sunxi_daudio->vol_supply.daudio_regulator);
		if (ret) {
			dev_err(sunxi_daudio->dev,
				"resume enable duaido vcc-pin failed\n");
				goto err_resume_out;
		}
	}
	if (clk_prepare_enable(sunxi_daudio->pllclk)) {
		dev_err(sunxi_daudio->dev, "pllclk resume failed\n");
		ret = -EBUSY;
		goto err_resume_out;
	}
#ifdef DAUDIO_PLL_AUDIO_X4
	if (clk_prepare_enable(sunxi_daudio->pllclkx4)) {
		dev_err(sunxi_daudio->dev, "pllclkx4 resume failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
	if (clk_prepare_enable(sunxi_daudio->moduleclk)) {
		dev_err(sunxi_daudio->dev, "moduleclk resume failed\n");
		ret = -EBUSY;
		goto err_pllclkx4_disable;
	}
#else
	if (clk_prepare_enable(sunxi_daudio->moduleclk)) {
		dev_err(sunxi_daudio->dev, "moduleclk resume failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
#endif

	if (sunxi_daudio->pdata->external_type) {
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
					sunxi_daudio->pinstate);
		if (ret) {
			dev_warn(sunxi_daudio->dev,
				"daudio set pinctrl default state fail\n");
		}
#ifdef DAUDIO_PINCTRL_STATE_DEFAULT_B
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
					sunxi_daudio->pinstate_b);
		if (ret) {
			dev_warn(sunxi_daudio->dev,
				"daudio set pinctrl pins_b state fail\n");
		}
#endif
	}

	sunxi_daudio_init(sunxi_daudio);

	/* for echo the save reg */
	for (i = 0; i < SUNXI_DAUDIO_REG_NUM_MAX; i++)
		regmap_write(sunxi_daudio->regmap, i << 2,
				(sunxi_daudio->reg_label+i)->value);
	kfree(sunxi_daudio->reg_label);

	/* for clear fifo */
	for (i = 0 ; i < SUNXI_DAUDIO_FTX_TIMES ; i++) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<FIFO_CTL_FTX), (1<<FIFO_CTL_FTX));
		mdelay(1);
	}

	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_TXCNT, 0);
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(1<<FIFO_CTL_FRX), (1<<FIFO_CTL_FRX));
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);

	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<SDO0_EN), (1<<SDO0_EN));
		if (sunxi_daudio->hdmi_en) {
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO1_EN), (1<<SDO1_EN));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO2_EN), (1<<SDO2_EN));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO3_EN), (1<<SDO3_EN));
		}
		/* Global enable I2S/TDM module */
		mutex_lock(&sunxi_daudio->global_mutex);
		sunxi_daudio_global_enable(sunxi_daudio, true);
		mutex_unlock(&sunxi_daudio->global_mutex);
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<CTL_TXEN), (1<<CTL_TXEN));
	}

	pr_debug("[%s] resume .%s end\n", __func__,
			dev_name(sunxi_daudio->dev));

	return 0;

#ifdef DAUDIO_PLL_AUDIO_X4
err_pllclkx4_disable:
	clk_disable_unprepare(sunxi_daudio->pllclkx4);
#endif
err_pllclk_disable:
	clk_disable_unprepare(sunxi_daudio->pllclk);
err_resume_out:
	if (sunxi_daudio->vol_supply.daudio_regulator)
		regulator_disable(sunxi_daudio->vol_supply.daudio_regulator);
	return ret;
}

#ifdef DAUDIO_CLASS_DEBUG
#ifdef DAUDIO_STANDBY_DEBUG
/*
 * ex:
 * param 1: 0 daudio0; 1 daudio1;...
 * param 2: 0 suspend; 1 resume;
 * param 3: 0 disabled; 1 enabled;
 *
 * write:
 * echo 0,0x1,0x1 > daudio_standby
 * echo 1,0x1,0x1 > daudio_standby
 */
static ssize_t daudio_class_standby_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int i;
	ssize_t count = 0;
	int daudio_num = daudio_standby_debug >> 4;
	int standby_mode = (daudio_standby_debug >> 1) & 0x1;
	int enable = daudio_standby_debug & 0x1;

	if (daudio_standby_debug == 0) {
		count += sprintf(buf, "Example(:daudio_num->%d):\n", DAUDIO_NUM_MAX);
		count += sprintf(buf + count, "echo 0,0x1,0x1 > daudio_standby\n\n");
		count += sprintf(buf + count, "param 1: 0 Daudio0; 1 Daudio1;...\n");
		count += sprintf(buf + count, "param 2: 0 Suspend; 1 Resume;\n");
		count += sprintf(buf + count, "param 3: 0 disabled; 1 enabled;\n");
		count += sprintf(buf + count, "Daudio[1], Standby->[resume], Enable\n\n");
		return count;
	}
	pr_err("[%s], Daudio[%d], Standby->[%s], %s\n", __func__,
			daudio_num,
			standby_mode ? "resume":"suspend",
			enable ? "Enable":"Disable");

	for (i = 0; i < DAUDIO_NUM_MAX; i++) {
		if (sunxi_daudio_global[i] && sunxi_daudio_global[i]->pdata) {
			if ((sunxi_daudio_global[i]->pdata->tdm_num ==
					(daudio_standby_debug >> 4)) &&
				(daudio_standby_debug & 0x1)) {
				if ((daudio_standby_debug >> 1) & 0x1) {
					sunxi_daudio_debug_resume(
						sunxi_daudio_global[i]);
				} else {
					sunxi_daudio_debug_suspend(
						sunxi_daudio_global[i]);
				}
			}
		}
	}

	daudio_standby_debug = 0;

	return 0;
}

static ssize_t daudio_class_standby_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int input_reg_group = 0;
	int standby_mode = 0;
	int input_bool_val = 0;

	ret = sscanf(buf, "%d,0x%x,0x%x", &input_reg_group,
		     &standby_mode, &input_bool_val);

	if ((input_reg_group >= DAUDIO_NUM_MAX) || (input_reg_group < 0)) {
		pr_err("not exist daudio%d\n", input_reg_group);
		return count;
	}

	if ((standby_mode > 1) || (standby_mode < 0) ||
			(input_bool_val > 1) || (input_bool_val < 0)) {
		pr_err("is bool vaule!!\n");
		return count;
	}

	daudio_standby_debug = (input_reg_group << 4) & 0xf0;
	daudio_standby_debug |= (standby_mode << 1) & 0x2;
	daudio_standby_debug |= input_bool_val & 0x1;
	pr_err("ret:%d, Daudio[%d], Standby->[%s], %s\n",
	       ret, input_reg_group, standby_mode ? "resume":"suspend",
		   input_bool_val ? "Enable":"Disable");

	return count;
}
#endif

/*
 * ex:
 * param 1: 0 read;1 write
 * param 2: 0 daudio0; 1 daudio1;
 * param 3: address;
 * param 4: write value;
 * read:
 *	echo 0,0 > daudio_reg_debug
 *	echo 0,1 > daudio_reg_debug
 *	echo 0,0,0x4 > daudio_reg_debug
 *	echo 0,1,0x0a > daudio_reg_debug
 * write:
 *	echo 1,0,0x00,0xa > daudio_reg_debug
 *	echo 1,0,0x00,0xff > daudio_reg_debug
 */
static ssize_t daudio_class_debug_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int rw_flag;
	int reg_val_read;
	unsigned int input_reg_val = 0;
	int input_reg_group = 0;
	unsigned int input_reg_offset = 0;
	int i = 0;

	ret = sscanf(buf, "%d,%d,0x%x,0x%x", &rw_flag, &input_reg_group,
			&input_reg_offset, &input_reg_val);
	if (!(rw_flag == 1 || rw_flag == 0)) {
		pr_err("not rw_flag\n");
		return count;
	}
	if ((input_reg_group >= DAUDIO_NUM_MAX) ||
		!sunxi_daudio_global[input_reg_group]) {
		pr_err("not exist daudio[%d] driver or device\n",
						input_reg_group);
		pr_err("Daudio device list:\n\n");
		for (i = 0; i < DAUDIO_NUM_MAX; i++) {
			if (sunxi_daudio_global[i] != NULL)
				pr_err("    Daudio[%d]\n", i);
		}
		return count;
	}

	pr_err("Dump daudio reg:\n");
	if (rw_flag) {
		if (ret == 4) {
			writel(input_reg_val,
				sunxi_daudio_global[input_reg_group]->membase +
					input_reg_offset);
			reg_val_read = readl(
				sunxi_daudio_global[input_reg_group]->membase +
					input_reg_offset);
			pr_err("\n\n Daudio[%d] Reg[0x%x] : 0x%x\n\n",
				input_reg_group, input_reg_offset,
				reg_val_read);
		} else {
			pr_err("\nnum of params invalid\n");
		}
	} else {
		switch (ret) {
		case 2:
			while (reg_labels[i].name != NULL) {
				pr_err("%s 0x%p: 0x%x\n", reg_labels[i].name,
				(sunxi_daudio_global[input_reg_group]->membase +
							reg_labels[i].address),
				readl(sunxi_daudio_global[input_reg_group]->membase +
							reg_labels[i].address));
				i++;
			}
			break;
		case 3:
			reg_val_read = readl(
				sunxi_daudio_global[input_reg_group]->membase +
					input_reg_offset);
			pr_err("\n\n Daudio[%d] Reg[0x%x] : 0x%x\n\n",
				input_reg_group, input_reg_offset,
				reg_val_read);
			break;
		default:
			pr_err("\nnum of params invalid\n");
			break;
		}
	}

	return count;
}

static ssize_t daudio_class_debug_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	int count = 0;
	int i = 0;

	count += sprintf(buf, "Example(:daudio_num->%d):\n", DAUDIO_NUM_MAX);
	count += sprintf(buf + count, "param 1: 0 read; 1 write;\n");
	count += sprintf(buf + count, "param 2: 0 daudio0; 1 dauido1...;\n");
	count += sprintf(buf + count, "param 3: address\n");
	count += sprintf(buf + count, "param 4: reg value\n\n");
	count += sprintf(buf + count, "echo 0,0 > daudio_reg_debug\n");
	count += sprintf(buf + count, "echo 0,1,0x4 > daudio_reg_debug\n");
	count += sprintf(buf + count, "echo 1,0,0x4,0x1 > daudio_reg_debug\n");
	count += sprintf(buf + count, "echo 1,1,0x14,0xa > daudio_reg_debug\n\n");
	count += sprintf(buf + count, "daudio device list:\n\n");

	for (i = 0; i < DAUDIO_NUM_MAX; i++) {
		if (sunxi_daudio_global[i] != NULL)
			count += sprintf(buf + count, "    Daudio[%d]\n", i);
	}
	return count;
}

static struct class_attribute daudio_class_attrs[] = {
#ifdef DAUDIO_STANDBY_DEBUG
	__ATTR(daudio_standby, S_IRUGO|S_IWUSR, daudio_class_standby_show, daudio_class_standby_store),
#endif
	__ATTR(daudio_reg_debug, S_IRUGO|S_IWUSR, daudio_class_debug_show, daudio_class_debug_store),
	__ATTR_NULL
};
static struct class daudio_class = {
	.name = "daudio",
	.class_attrs = daudio_class_attrs,
};
#endif

static struct snd_soc_dai_ops sunxi_daudio_dai_ops = {
	.hw_params = sunxi_daudio_hw_params,
	.set_sysclk = sunxi_daudio_set_sysclk,
	.set_clkdiv = sunxi_daudio_set_clkdiv,
	.set_fmt = sunxi_daudio_set_fmt,
	.startup = sunxi_daudio_dai_startup,
	.trigger = sunxi_daudio_trigger,
	.prepare = sunxi_daudio_prepare,
	.shutdown = sunxi_daudio_shutdown,
};

static struct snd_soc_dai_driver sunxi_daudio_dai = {
	.probe = sunxi_daudio_probe,
	.suspend = sunxi_daudio_suspend,
	.resume = sunxi_daudio_resume,
	.remove = sunxi_daudio_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
		.rates = SUNXI_DAUDIO_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE
			| SNDRV_PCM_FMTBIT_S20_3LE
			| SNDRV_PCM_FMTBIT_S24_LE
			| SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
		.rates = SUNXI_DAUDIO_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE
			| SNDRV_PCM_FMTBIT_S20_3LE
			| SNDRV_PCM_FMTBIT_S24_LE
			| SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &sunxi_daudio_dai_ops,
};

static const struct snd_soc_component_driver sunxi_daudio_component = {
	.name		= DRV_NAME,
	.controls	= sunxi_daudio_controls,
	.num_controls	= ARRAY_SIZE(sunxi_daudio_controls),
};

static struct sunxi_daudio_platform_data sunxi_daudio = {
	.daudio_type = SUNXI_DAUDIO_EXTERNAL_TYPE,
	.external_type = 1,
};

/*
 * For HDMI default:
 * daudio_master:
 *;	1: SND_SOC_DAIFMT_CBM_CFM(codec clk & FRM master)
 *;	4: SND_SOC_DAIFMT_CBS_CFS(codec clk & FRM slave)
 *;audio_format:
 *;	1:SND_SOC_DAIFMT_I2S(standard i2s format). use
 *;	2:SND_SOC_DAIFMT_RIGHT_J(right justfied format).
 *;	3:SND_SOC_DAIFMT_LEFT_J(left justfied format)
 *;	4:SND_SOC_DAIFMT_DSP_A
 *	(pcm. MSB is available on 2nd BCLK rising edge after LRC rising edge)
 *;	5:SND_SOC_DAIFMT_DSP_B
 *	(pcm. MSB is available on 1nd BCLK rising edge after LRC rising edge)
 *;signal_inversion:
 *;	1:SND_SOC_DAIFMT_NB_NF(normal bit clock + frame)  use
 *;	2:SND_SOC_DAIFMT_NB_IF(normal BCLK + inv FRM)
 *;	3:SND_SOC_DAIFMT_IB_NF(invert BCLK + nor FRM)  use
 *;	4:SND_SOC_DAIFMT_IB_IF(invert BCLK + FRM)
 *
 *.audio_format = 1,
 *.signal_inversion = 1,
 *.daudio_master = 4,
 */
static struct sunxi_daudio_platform_data sunxi_tdmhdmi = {
	.daudio_type = SUNXI_DAUDIO_TDMHDMI_TYPE,
	.external_type = 0,
	.audio_format = 1,
	.signal_inversion = 1,
	.daudio_master = 4,
	.pcm_lrck_period = 32,
	.slot_width_select = 32,
	.tdm_config = 1,
	.mclk_div = 0,
};

static const struct of_device_id sunxi_daudio_of_match[] = {
	{
		.compatible = "allwinner,sunxi-daudio",
		.data = &sunxi_daudio,
	},
	{
		.compatible = "allwinner,sunxi-tdmhdmi",
		.data = &sunxi_tdmhdmi,
	},
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_daudio_of_match);

static struct regmap_config sunxi_daudio_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SUNXI_DAUDIO_DEBUG,
	.cache_type = REGCACHE_NONE,
};

static int sunxi_daudio_dev_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct sunxi_daudio_info *sunxi_daudio;
	struct device_node *np = pdev->dev.of_node;
	unsigned int temp_val;
	int ret;

	match = of_match_device(sunxi_daudio_of_match, &pdev->dev);
	if (match) {
		sunxi_daudio = devm_kzalloc(&pdev->dev,
					sizeof(struct sunxi_daudio_info),
					GFP_KERNEL);
		if (!sunxi_daudio) {
			dev_err(&pdev->dev, "alloc sunxi_daudio failed\n");
			ret = -ENOMEM;
			goto err_node_put;
		}
		dev_set_drvdata(&pdev->dev, sunxi_daudio);

		sunxi_daudio->dev = &pdev->dev;
		sunxi_daudio->pdata = devm_kzalloc(&pdev->dev,
				sizeof(struct sunxi_daudio_platform_data),
				GFP_KERNEL);
		if (!sunxi_daudio->pdata) {
			dev_err(&pdev->dev,
				"alloc sunxi daudio platform data failed\n");
			ret = -ENOMEM;
			goto err_devm_kfree;
		}
		memcpy(sunxi_daudio->pdata, match->data,
			sizeof(struct sunxi_daudio_platform_data));
	} else {
		dev_err(&pdev->dev, "node match failed\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(np, 0, &(sunxi_daudio->res));
	if (ret) {
		dev_err(&pdev->dev, "parse device node resource failed\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	}

	/* init some params */
	sunxi_daudio->hub_mode = 0;
	sunxi_daudio->playback_en = 0;
	sunxi_daudio->capture_en = 0;

	sunxi_daudio->pllclk = of_clk_get(np, 0);
	if (IS_ERR_OR_NULL(sunxi_daudio->pllclk)) {
		dev_err(&pdev->dev, "pllclk get failed\n");
		ret = PTR_ERR(sunxi_daudio->pllclk);
		goto err_devm_kfree;
	}

#ifdef DAUDIO_PLL_AUDIO_X4
	sunxi_daudio->pllclkx4 = of_clk_get(np, 1);
	if (IS_ERR_OR_NULL(sunxi_daudio->pllclkx4)) {
		dev_err(&pdev->dev, "pllclkx4 get failed\n");
		ret = PTR_ERR(sunxi_daudio->pllclkx4);
		goto err_pllclk_put;
	}
	sunxi_daudio->moduleclk = of_clk_get(np, 2);
	if (IS_ERR_OR_NULL(sunxi_daudio->moduleclk)) {
		dev_err(&pdev->dev, "moduleclk get failed\n");
		ret = PTR_ERR(sunxi_daudio->moduleclk);
		goto err_pllclkx4_put;
	}
	if (clk_set_parent(sunxi_daudio->moduleclk, sunxi_daudio->pllclkx4)) {
		dev_err(&pdev->dev, "set parent of moduleclk to pllclkx4 fail\n");
		ret = -EBUSY;
		goto err_moduleclk_put;
	}
	if (clk_prepare_enable(sunxi_daudio->pllclk)) {
		dev_err(&pdev->dev, "pllclk enable failed\n");
		ret = -EBUSY;
		goto err_moduleclk_put;
	}
	if (clk_prepare_enable(sunxi_daudio->pllclkx4)) {
		dev_err(&pdev->dev, "pllclkx4 enable failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
	if (clk_prepare_enable(sunxi_daudio->moduleclk)) {
		dev_err(&pdev->dev, "moduleclk enable failed\n");
		ret = -EBUSY;
		goto err_pllclkx4_disable;
	}
#else
	sunxi_daudio->moduleclk = of_clk_get(np, 1);
	if (IS_ERR_OR_NULL(sunxi_daudio->moduleclk)) {
		dev_err(&pdev->dev, "moduleclk get failed\n");
		ret = PTR_ERR(sunxi_daudio->moduleclk);
		goto err_pllclk_put;
	}
	if (clk_set_parent(sunxi_daudio->moduleclk, sunxi_daudio->pllclk)) {
		dev_err(&pdev->dev, "set parent of moduleclk to pllclk fail\n");
		ret = -EBUSY;
		goto err_moduleclk_put;
	}
	if (clk_prepare_enable(sunxi_daudio->pllclk)) {
		dev_err(&pdev->dev, "pllclk enable failed\n");
		ret = -EBUSY;
		goto err_moduleclk_put;
	}
	if (clk_prepare_enable(sunxi_daudio->moduleclk)) {
		dev_err(&pdev->dev, "moduleclk enable failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
#endif
	if (sunxi_daudio->pdata->external_type) {
		sunxi_daudio->pinctrl = devm_pinctrl_get(&pdev->dev);
		if (IS_ERR_OR_NULL(sunxi_daudio->pinctrl)) {
			dev_err(&pdev->dev, "pinctrl get failed\n");
			ret = -EINVAL;
			goto err_moduleclk_disable;
		}
		sunxi_daudio->pinstate = pinctrl_lookup_state(
				sunxi_daudio->pinctrl, PINCTRL_STATE_DEFAULT);
		if (IS_ERR_OR_NULL(sunxi_daudio->pinstate)) {
			dev_err(&pdev->dev, "pinctrl default state get fail\n");
			ret = -EINVAL;
			goto err_pinctrl_put;
		}
#ifdef DAUDIO_PINCTRL_STATE_DEFAULT_B
		sunxi_daudio->pinstate_b = pinctrl_lookup_state(
				sunxi_daudio->pinctrl,
				DAUDIO_PINCTRL_STATE_DEFAULT_B);
		if (IS_ERR_OR_NULL(sunxi_daudio->pinstate_b)) {
			dev_err(&pdev->dev, "pinctrl default state get fail\n");
			ret = -EINVAL;
			goto err_pinctrl_put;
		}
#endif
		sunxi_daudio->pinstate_sleep = pinctrl_lookup_state(
				sunxi_daudio->pinctrl, PINCTRL_STATE_SLEEP);
		if (IS_ERR_OR_NULL(sunxi_daudio->pinstate_sleep)) {
			dev_err(&pdev->dev, "pinctrl sleep state get failed\n");
			ret = -EINVAL;
			goto err_pinctrl_put;
		}

		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
					sunxi_daudio->pinstate);
		if (ret)
			dev_warn(sunxi_daudio->dev,
				"daudio set pinctrl default state fail\n");
#ifdef DAUDIO_PINCTRL_STATE_DEFAULT_B
		ret = pinctrl_select_state(sunxi_daudio->pinctrl,
					sunxi_daudio->pinstate_b);
		if (ret)
			dev_warn(sunxi_daudio->dev,
				"daudio set pinctrl pins_b state fail\n");
#endif
	}

	switch (sunxi_daudio->pdata->daudio_type) {
	case	SUNXI_DAUDIO_EXTERNAL_TYPE:
		ret = of_property_read_u32(np, "tdm_num", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "tdm configuration missing\n");
			/*
			 * warnning just continue,
			 * making tdm_num as default setting
			 */
			sunxi_daudio->pdata->tdm_num = 0;
		} else {
			/*
			 * FIXME, for channel number mess,
			 * so just not check channel overflow
			 */
			sunxi_daudio->pdata->tdm_num = temp_val;
		}

		sunxi_daudio->playback_dma_param.src_maxburst = 4;
		sunxi_daudio->playback_dma_param.dst_maxburst = 4;
		sunxi_daudio->capture_dma_param.dma_addr =
				sunxi_daudio->res.start + SUNXI_DAUDIO_RXFIFO;
		sunxi_daudio->capture_dma_param.src_maxburst = 4;
		sunxi_daudio->capture_dma_param.dst_maxburst = 4;
		sunxi_daudio->playback_dma_param.dma_addr =
				sunxi_daudio->res.start + SUNXI_DAUDIO_TXFIFO;

		switch (sunxi_daudio->pdata->tdm_num) {
		case 0:
			SUNXI_DAUDIO_DRQDST(sunxi_daudio, 0);
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 0);
			break;
		case 1:
			SUNXI_DAUDIO_DRQDST(sunxi_daudio, 1);
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 1);
			break;
		case 2:
			SUNXI_DAUDIO_DRQDST(sunxi_daudio, 2);
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 2);
			break;
		case 3:
			SUNXI_DAUDIO_DRQDST(sunxi_daudio, 3);
			SUNXI_DAUDIO_DRQSRC(sunxi_daudio, 3);
			break;
		default:
			dev_warn(&pdev->dev, "tdm_num setting overflow\n");
			ret = -EFAULT;
			goto err_pinctrl_put;
			break;
		}

		sunxi_daudio->vol_supply.regulator_name = NULL;
		ret = of_property_read_string(np, "daudio_regulator",
				&sunxi_daudio->vol_supply.regulator_name);
		if (ret < 0) {
			dev_warn(&pdev->dev, "regulator missing or invalid\n");
			sunxi_daudio->vol_supply.daudio_regulator = NULL;
		} else {
			sunxi_daudio->vol_supply.daudio_regulator =
				regulator_get(NULL, sunxi_daudio->vol_supply.regulator_name);
			if (IS_ERR(sunxi_daudio->vol_supply.daudio_regulator)) {
				pr_err("get duaido[%d] vcc-pin failed\n",
					sunxi_daudio->pdata->tdm_num);
				ret = -EFAULT;
				goto err_pinctrl_put;
			}
			ret = regulator_set_voltage(
					sunxi_daudio->vol_supply.daudio_regulator,
					3300000, 3300000);
			if (ret) {
				pr_err("set duaido[%d] voltage failed\n",
						sunxi_daudio->pdata->tdm_num);
				ret = -EFAULT;
				goto err_regulator_get;
			}
			ret = regulator_enable(
					sunxi_daudio->vol_supply.daudio_regulator);
			if (ret) {
				pr_err("enable duaido[%d] vcc-pin failed\n",
						sunxi_daudio->pdata->tdm_num);
				ret = -EFAULT;
				goto err_regulator_get;
			}
		}

#ifndef USE_ASOC_FMT_SETTING
		ret = of_property_read_u32(np, "daudio_master", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "daudio_master configuration missing or invalid\n");
			/*
			 * default setting SND_SOC_DAIFMT_CBS_CFS mode
			 * codec clk & FRM slave
			 */
			sunxi_daudio->pdata->daudio_master = 4;
		} else {
			sunxi_daudio->pdata->daudio_master = temp_val;
		}

		ret = of_property_read_u32(np, "audio_format", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "audio_format configuration missing or invalid\n");
			sunxi_daudio->pdata->audio_format = 1;
		} else {
			sunxi_daudio->pdata->audio_format = temp_val;
		}

		ret = of_property_read_u32(np, "signal_inversion", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "signal_inversion configuration missing or invalid\n");
			sunxi_daudio->pdata->signal_inversion = 1;
		} else {
			sunxi_daudio->pdata->signal_inversion = temp_val;
		}
#endif

		ret = of_property_read_u32(np, "pcm_lrck_period", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "pcm_lrck_period configuration missing or invalid\n");
			sunxi_daudio->pdata->pcm_lrck_period = 0;
		} else {
			sunxi_daudio->pdata->pcm_lrck_period = temp_val;
		}

		ret = of_property_read_u32(np, "msb_lsb_first", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "msb_lsb_first configuration missing or invalid\n");
			sunxi_daudio->pdata->msb_lsb_first = 0;
		} else
			sunxi_daudio->pdata->msb_lsb_first = temp_val;

		ret = of_property_read_u32(np, "slot_width_select", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "slot_width_select configuration missing or invalid\n");
			sunxi_daudio->pdata->slot_width_select = 0;
		} else {
			sunxi_daudio->pdata->slot_width_select = temp_val;
		}

		ret = of_property_read_u32(np, "frametype", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "frametype configuration missing or invalid\n");
			sunxi_daudio->pdata->frame_type = 0;
		} else {
			sunxi_daudio->pdata->frame_type = temp_val;
		}

		ret = of_property_read_u32(np, "sign_extend", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "sign_extend configuration missing or invalid\n");
			sunxi_daudio->pdata->sign_extend = 0;
		} else
			sunxi_daudio->pdata->sign_extend = temp_val;

		ret = of_property_read_u32(np, "tx_data_mode", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "tx_data_mode configuration missing or invalid\n");
			sunxi_daudio->pdata->tx_data_mode = 0;
		} else
			sunxi_daudio->pdata->tx_data_mode = temp_val;

		ret = of_property_read_u32(np, "rx_data_mode", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "rx_data_mode configuration missing or invalid\n");
			sunxi_daudio->pdata->rx_data_mode = 0;
		} else
			sunxi_daudio->pdata->rx_data_mode = temp_val;

		ret = of_property_read_u32(np, "tdm_config", &temp_val);
		if (ret < 0) {
			dev_warn(&pdev->dev, "tdm_config configuration missing or invalid\n");
			sunxi_daudio->pdata->tdm_config = 1;
		} else {
			sunxi_daudio->pdata->tdm_config = temp_val;
		}

		ret = of_property_read_u32(np, "mclk_div", &temp_val);
		if (ret < 0)
			sunxi_daudio->pdata->mclk_div = 0;
		else
			sunxi_daudio->pdata->mclk_div = temp_val;
		break;
	case	SUNXI_DAUDIO_TDMHDMI_TYPE:
#ifdef SUNXI_HDMI_AUDIO_ENABLE
		sunxi_daudio->playback_dma_param.dma_addr =
				sunxi_daudio->res.start + SUNXI_DAUDIO_TXFIFO;
		sunxi_daudio->playback_dma_param.dma_drq_type_num =
					DRQDST_HDMI_TX;
		sunxi_daudio->playback_dma_param.src_maxburst = 8;
		sunxi_daudio->playback_dma_param.dst_maxburst = 8;
		sunxi_daudio->hdmi_en = 1;
#ifdef CONFIG_SUNXI_AUDIO_DEBUG
		sunxi_daudio->capture_dma_param.dma_addr =
				sunxi_daudio->res.start + SUNXI_DAUDIO_RXFIFO;
		sunxi_daudio->capture_dma_param.dma_drq_type_num =
					DRQDST_HDMI_RX;
		sunxi_daudio->capture_dma_param.src_maxburst = 8;
		sunxi_daudio->capture_dma_param.dst_maxburst = 8;
#endif
#endif
		break;
	default:
		dev_err(&pdev->dev, "missing digital audio type\n");
		ret = -EINVAL;
		goto err_moduleclk_put;
	}

	sunxi_daudio->memregion = devm_request_mem_region(&pdev->dev,
					sunxi_daudio->res.start,
					resource_size(&sunxi_daudio->res),
					DRV_NAME);
	if (!sunxi_daudio->memregion) {
		dev_err(&pdev->dev, "Memory region already claimed\n");
		ret = -EBUSY;
		goto err_regulator_disable;
	}

	sunxi_daudio->membase = devm_ioremap(&pdev->dev,
					sunxi_daudio->memregion->start,
					resource_size(sunxi_daudio->memregion));
	if (!sunxi_daudio->membase) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -EBUSY;
		goto err_devm_free_memregion;
	}

	sunxi_daudio->regmap = devm_regmap_init_mmio(&pdev->dev,
					sunxi_daudio->membase,
					&sunxi_daudio_regmap_config);
	if (IS_ERR_OR_NULL(sunxi_daudio->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(sunxi_daudio->regmap);
		goto err_iounmap;
	}

	ret = snd_soc_register_component(&pdev->dev, &sunxi_daudio_component,
					&sunxi_daudio_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "component register failed\n");
		ret = -ENOMEM;
		goto err_iounmap;
	}

	switch (sunxi_daudio->pdata->daudio_type) {
	case	SUNXI_DAUDIO_EXTERNAL_TYPE:
		ret = asoc_dma_platform_register(&pdev->dev, 0);
		if (ret) {
			dev_err(&pdev->dev, "register ASoC platform failed\n");
			ret = -ENOMEM;
			goto err_unregister_component;
		}
		break;
	case	SUNXI_DAUDIO_TDMHDMI_TYPE:
		ret = asoc_dma_platform_register(&pdev->dev,
				SND_DMAENGINE_PCM_FLAG_NO_DT);
		if (ret) {
			dev_err(&pdev->dev, "register ASoC platform failed\n");
			ret = -ENOMEM;
			goto err_unregister_component;
		}
		break;
	default:
		dev_err(&pdev->dev, "missing digital audio type\n");
		ret = -EINVAL;
		goto err_unregister_component;
	}
#ifdef DAUDIO_CLASS_DEBUG
	if (daudio_class_dev_cnt++ == 0) {
		ret = class_register(&daudio_class);
		if (ret)
			pr_warn("daudio: Failed to create debugfs directory\n");
	}
#endif

#ifdef CONFIG_SND_SUNXI_MAD
	INIT_WORK(&(sunxi_daudio->ws_rx_mad_reset),
			sunxi_daudio_rxctrl_stop_mad_reset);
#endif

	sunxi_daudio_global[sunxi_daudio->pdata->tdm_num] = sunxi_daudio;

	if (sunxi_daudio->pdata->daudio_type == SUNXI_DAUDIO_TDMHDMI_TYPE) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1<<SDO0_EN), (1<<SDO0_EN));
		if (sunxi_daudio->hdmi_en) {
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO1_EN), (1<<SDO1_EN));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO2_EN), (1<<SDO2_EN));
			regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL, (1<<SDO3_EN), (1<<SDO3_EN));
		}
		sunxi_daudio_global_enable(sunxi_daudio, true);
	}

	return 0;

err_unregister_component:
	snd_soc_unregister_component(&pdev->dev);
err_iounmap:
	devm_iounmap(&pdev->dev, sunxi_daudio->membase);
err_devm_free_memregion:
	devm_release_mem_region(&pdev->dev, sunxi_daudio->memregion->start,
				resource_size(sunxi_daudio->memregion));
err_regulator_disable:
	if (sunxi_daudio->pdata->external_type &&
		sunxi_daudio->vol_supply.daudio_regulator)
		regulator_disable(sunxi_daudio->vol_supply.daudio_regulator);
err_regulator_get:
	if (sunxi_daudio->pdata->external_type &&
		sunxi_daudio->vol_supply.daudio_regulator)
		regulator_put(sunxi_daudio->vol_supply.daudio_regulator);
err_pinctrl_put:
	if (sunxi_daudio->pdata->external_type)
		devm_pinctrl_put(sunxi_daudio->pinctrl);
err_moduleclk_disable:
	clk_disable_unprepare(sunxi_daudio->moduleclk);
#ifdef DAUDIO_PLL_AUDIO_X4
err_pllclkx4_disable:
	clk_disable_unprepare(sunxi_daudio->pllclkx4);
#endif
err_pllclk_disable:
	clk_disable_unprepare(sunxi_daudio->pllclk);
err_moduleclk_put:
	clk_put(sunxi_daudio->moduleclk);
#ifdef DAUDIO_PLL_AUDIO_X4
err_pllclkx4_put:
	clk_put(sunxi_daudio->pllclkx4);
#endif
err_pllclk_put:
	clk_put(sunxi_daudio->pllclk);
err_devm_kfree:
	devm_kfree(&pdev->dev, sunxi_daudio);
err_node_put:
	of_node_put(np);
	return ret;
}

static int __exit sunxi_daudio_dev_remove(struct platform_device *pdev)
{
	struct sunxi_daudio_info *sunxi_daudio = dev_get_drvdata(&pdev->dev);

#ifdef DAUDIO_CLASS_DEBUG
	if (--daudio_class_dev_cnt == 0)
		class_unregister(&daudio_class);
#endif

#ifdef CONFIG_SND_SUNXI_MAD
	cancel_work_sync(&sunxi_daudio->ws_rx_mad_reset);
#endif

	if (sunxi_daudio->pdata->external_type)
		devm_pinctrl_put(sunxi_daudio->pinctrl);

	if (sunxi_daudio->vol_supply.daudio_regulator) {
		regulator_disable(sunxi_daudio->vol_supply.daudio_regulator);
		regulator_put(sunxi_daudio->vol_supply.daudio_regulator);
	}

	devm_iounmap(&pdev->dev, sunxi_daudio->membase);

	devm_release_mem_region(&pdev->dev, sunxi_daudio->memregion->start,
				resource_size(sunxi_daudio->memregion));

	asoc_dma_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	clk_disable_unprepare(sunxi_daudio->moduleclk);
	clk_put(sunxi_daudio->moduleclk);
#ifdef DAUDIO_PLL_AUDIO_X4
	clk_disable_unprepare(sunxi_daudio->pllclkx4);
	clk_put(sunxi_daudio->pllclkx4);
#endif
	clk_disable_unprepare(sunxi_daudio->pllclk);
	clk_put(sunxi_daudio->pllclk);

	devm_kfree(&pdev->dev, sunxi_daudio);

	return 0;
}

static struct platform_driver sunxi_daudio_driver = {
	.probe = sunxi_daudio_dev_probe,
	.remove = __exit_p(sunxi_daudio_dev_remove),
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_daudio_of_match,
	},
};

module_platform_driver(sunxi_daudio_driver);
MODULE_AUTHOR("wolfgang huang <huangjinhui@allwinnertech.com>");
MODULE_DESCRIPTION("SUNXI DAI AUDIO ASoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-daudio");
