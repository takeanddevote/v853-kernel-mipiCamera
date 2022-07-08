/*
 * sound\soc\sunxi\sun8iw18-codec.c
 * (C) Copyright 2014-2018
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/pinctrl/consumer.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/sunxi-gpio.h>
#include <linux/dma/sunxi-dma.h>

#include "sunxi_rw_func.h"
#include "sun8iw18-codec.h"
#include "sun8iw18-sndcodec.h"

#ifdef CONFIG_SND_SUNXI_MAD
#include "sunxi-mad.h"
#endif

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
#include "sunxi-daudio.h"
#endif

#define	DRV_NAME	"sunxi-internal-codec"

/* need optimize */
static int lineout_msleep = 120;
module_param(lineout_msleep, int, 0644);
MODULE_PARM_DESC(lineout_msleep, "SUNXI codec lineout msleep for pop sound.");

static int dac_msleep = 10;
module_param(dac_msleep, int, 0644);
MODULE_PARM_DESC(dac_msleep, "SUNXI codec dac msleep for pop sound.");

static struct label reg_labels[] = {
	LABEL(SUNXI_DAC_DPC),
	LABEL(SUNXI_DAC_FIFO_CTL),
	LABEL(SUNXI_DAC_FIFO_STA),
	LABEL(SUNXI_DAC_CNT),
	LABEL(SUNXI_DAC_DG),
	LABEL(SUNXI_ADC_FIFO_CTL),
	LABEL(SUNXI_ADC_FIFO_STA),
	LABEL(SUNXI_ADC_CNT),
	LABEL(SUNXI_ADC_DG),
	LABEL(SUNXI_DAC_DAP_CTL),
	LABEL(SUNXI_ADC_DAP_CTL),

	LABEL(SUNXI_HP_CTL),
	LABEL(SUNXI_MIX_DAC_CTL),
	LABEL(SUNXI_LINEOUT_CTL0),
	LABEL(SUNXI_LINEOUT_CTL1),
	LABEL(SUNXI_MIC1_CTL),
	LABEL(SUNXI_MIC2_MIC3_CTL),

	LABEL(SUNXI_LADCMIX_SRC),
	LABEL(SUNXI_RADCMIX_SRC),
	LABEL(SUNXI_XADCMIX_SRC),
	LABEL(SUNXI_ADC_CTL),
	LABEL(SUNXI_MBIAS_CTL),
	LABEL(SUNXI_APT_REG),

	LABEL(SUNXI_OP_BIAS_CTL0),
	LABEL(SUNXI_OP_BIAS_CTL1),
	LABEL(SUNXI_ZC_VOL_CTL),
	LABEL(SUNXI_BIAS_CAL_CTRL),
	LABEL_END,
};

static const struct sample_rate sample_rate_conv[] = {
	{44100, 0},
	{48000, 0},
	{8000, 5},
	{32000, 1},
	{22050, 2},
	{24000, 2},
	{16000, 3},
	{11025, 4},
	{12000, 4},
	{192000, 6},
	{96000, 7},
};

static const DECLARE_TLV_DB_SCALE(digital_tlv, -7424, 116, 0);
static const DECLARE_TLV_DB_SCALE(adc_gain_tlv, -450, 150, 0);

static const unsigned int mic_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 7, TLV_DB_SCALE_ITEM(2400, 300, 0),
};

static const unsigned int lineout_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 1),
	1, 31, TLV_DB_SCALE_ITEM(-4350, 150, 1),
};

/*lineoutL mux select */
const char * const left_lineout_text[] = {
	"DACL", "DAC_NULL",
};

static const struct soc_enum left_lineout_enum =
SOC_ENUM_SINGLE(SUNXI_LINEOUT_CTL0, LINEOUTL_SRC,
ARRAY_SIZE(left_lineout_text), left_lineout_text);

static const struct snd_kcontrol_new left_lineout_mux =
SOC_DAPM_ENUM("Left LINEOUT Mux", left_lineout_enum);

/*lineoutR mux select */
const char * const right_lineout_text[] = {
	"DAC_NULL", "DACL",
};

static const struct soc_enum right_lineout_enum =
SOC_ENUM_SINGLE(SUNXI_LINEOUT_CTL0, LINEOUTR_SRC,
ARRAY_SIZE(right_lineout_text), right_lineout_text);

static const struct snd_kcontrol_new right_lineout_mux =
SOC_DAPM_ENUM("Right LINEOUT Mux", right_lineout_enum);

static void adcdrc_config(struct snd_soc_codec *codec)
{
	/* Left peak filter attack time */
	snd_soc_write(codec, AC_ADC_DRC_LPFHAT, (0x000B77BF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LPFLAT, 0x000B77BF & 0xFFFF);
	/* Right peak filter attack time */
	snd_soc_write(codec, AC_ADC_DRC_RPFHAT, (0x000B77BF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_RPFLAT, 0x000B77BF & 0xFFFF);
	/* Left peak filter release time */
	snd_soc_write(codec, AC_ADC_DRC_LPFHRT, (0x00FFE1F8 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LPFLRT, 0x00FFE1F8 & 0xFFFF);
	/* Right peak filter release time */
	snd_soc_write(codec, AC_ADC_DRC_RPFHRT, (0x00FFE1F8 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_RPFLRT, 0x00FFE1F8 & 0xFFFF);

	/* Left RMS filter attack time */
	snd_soc_write(codec, AC_ADC_DRC_LPFHAT, (0x00012BAF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LPFLAT, 0x00012BAF & 0xFFFF);
	/* Right RMS filter attack time */
	snd_soc_write(codec, AC_ADC_DRC_RPFHAT, (0x00012BAF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_RPFLAT, 0x00012BAF & 0xFFFF);

	/* smooth filter attack time */
	snd_soc_write(codec, AC_ADC_DRC_SFHAT, (0x00025600 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_SFLAT, 0x00025600 & 0xFFFF);
	/* gain smooth filter release time */
	snd_soc_write(codec, AC_ADC_DRC_SFHRT, (0x00000F04 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_SFLRT, 0x00000F04 & 0xFFFF);

	/* OPL */
	snd_soc_write(codec, AC_ADC_DRC_HOPL, (0xFBD8FBA7 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LOPL, 0xFBD8FBA7 & 0xFFFF);
	/* OPC */
	snd_soc_write(codec, AC_ADC_DRC_HOPC, (0xF95B2C3F >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LOPC, 0xF95B2C3F & 0xFFFF);
	/* OPE */
	snd_soc_write(codec, AC_ADC_DRC_HOPE, (0xF45F8D6E >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LOPE, 0xF45F8D6E & 0xFFFF);
	/* LT */
	snd_soc_write(codec, AC_ADC_DRC_HLT, (0x01A934F0 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LLT, 0x01A934F0 & 0xFFFF);
	/* CT */
	snd_soc_write(codec, AC_ADC_DRC_HCT, (0x06A4D3C0 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LCT, 0x06A4D3C0 & 0xFFFF);
	/* ET */
	snd_soc_write(codec, AC_ADC_DRC_HET, (0x0BA07291 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LET, 0x0BA07291 & 0xFFFF);
	/* Ki */
	snd_soc_write(codec, AC_ADC_DRC_HKI, (0x00051EB8 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LKI, 0x00051EB8 & 0xFFFF);
	/* Kc */
	snd_soc_write(codec, AC_ADC_DRC_HKC, (0x00800000 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LKC, 0x00800000 & 0xFFFF);
	/* Kn */
	snd_soc_write(codec, AC_ADC_DRC_HKN, (0x01000000 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LKN, 0x01000000 & 0xFFFF);
	/* Ke */
	snd_soc_write(codec, AC_ADC_DRC_HKE, (0x0000F45F >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LKE, 0x0000F45F & 0xFFFF);
}

static void adchpf_config(struct snd_soc_codec *codec)
{
	/* HPF */
	snd_soc_write(codec, AC_ADC_DRC_HHPFC, (0xFFFAC1 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_ADC_DRC_LHPFC, 0xFFE644 & 0xFFFF);
}

static void adcdrc_enable(struct snd_soc_codec *codec, bool on)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (on) {
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
			(0x1 << ADC_DRC0_EN | 0x1 << ADC_DRC1_EN),
			(0x1 << ADC_DRC0_EN | 0x1 << ADC_DRC1_EN));

		if (sunxi_codec->adc_dap_enable++ == 0) {
			snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
				(0x1 << ADC_DAP0_EN | 0x1 << ADC_DAP1_EN),
				(0x1 << ADC_DAP0_EN | 0x1 << ADC_DAP1_EN));
		}
	} else {
		if (--sunxi_codec->adc_dap_enable == 0) {
			snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
				(0x1 << ADC_DAP0_EN | 0x1 << ADC_DAP1_EN),
				(0x0 << ADC_DAP0_EN | 0x0 << ADC_DAP1_EN));
		}
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
			(0x1 << ADC_DRC0_EN | 0x1 << ADC_DRC1_EN),
			(0x0 << ADC_DRC0_EN | 0x0 << ADC_DRC1_EN));
	}
}

static void adchpf_enable(struct snd_soc_codec *codec, bool on)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (on) {
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
			(0x1 << ADC_HPF0_EN | 0x1 << ADC_HPF1_EN),
			(0x1 << ADC_HPF0_EN | 0x1 << ADC_HPF1_EN));

		if (sunxi_codec->adc_dap_enable++ == 0) {
			snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
				(0x1 << ADC_DAP0_EN | 0x1 << ADC_DAP1_EN),
				(0x1 << ADC_DAP0_EN | 0x1 << ADC_DAP1_EN));
		}
	} else {
		if (--sunxi_codec->adc_dap_enable == 0) {
			snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
				(0x1 << ADC_DAP0_EN | 0x1 << ADC_DAP1_EN),
				(0x0 << ADC_DAP0_EN | 0x0 << ADC_DAP1_EN));
		}
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTL,
			(0x1 << ADC_HPF0_EN | 0x1 << ADC_HPF1_EN),
			(0x0 << ADC_HPF0_EN | 0x0 << ADC_HPF1_EN));
	}
}

static void dacdrc_config(struct snd_soc_codec *codec)
{
	/* Left peak filter attack time */
	snd_soc_write(codec, AC_DAC_DRC_LPFHAT, (0x000B77BF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LPFLAT, 0x000B77BF & 0xFFFF);
	/* Right peak filter attack time */
	snd_soc_write(codec, AC_DAC_DRC_RPFHAT, (0x000B77BF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_RPFLAT, 0x000B77BF & 0xFFFF);
	/* Left peak filter release time */
	snd_soc_write(codec, AC_DAC_DRC_LPFHRT, (0x00FFE1F8 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LPFLRT, 0x00FFE1F8 & 0xFFFF);
	/* Right peak filter release time */
	snd_soc_write(codec, AC_DAC_DRC_RPFHRT, (0x00FFE1F8 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_RPFLRT, 0x00FFE1F8 & 0xFFFF);

	/* Left RMS filter attack time */
	snd_soc_write(codec, AC_DAC_DRC_LPFHAT, (0x00012BAF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LPFLAT, 0x00012BAF & 0xFFFF);
	/* Right RMS filter attack time */
	snd_soc_write(codec, AC_DAC_DRC_RPFHAT, (0x00012BAF >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_RPFLAT, 0x00012BAF & 0xFFFF);

	/* smooth filter attack time */
	snd_soc_write(codec, AC_DAC_DRC_SFHAT, (0x00025600 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_SFLAT, 0x00025600 & 0xFFFF);
	/* gain smooth filter release time */
	snd_soc_write(codec, AC_DAC_DRC_SFHRT, (0x00000F04 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_SFLRT, 0x00000F04 & 0xFFFF);

	/* OPL */
	snd_soc_write(codec, AC_DAC_DRC_HOPL, (0xFBD8FBA7 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LOPL, 0xFBD8FBA7 & 0xFFFF);
	/* OPC */
	snd_soc_write(codec, AC_DAC_DRC_HOPC, (0xF95B2C3F >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LOPC, 0xF95B2C3F & 0xFFFF);
	/* OPE */
	snd_soc_write(codec, AC_DAC_DRC_HOPE, (0xF45F8D6E >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LOPE, 0xF45F8D6E & 0xFFFF);
	/* LT */
	snd_soc_write(codec, AC_DAC_DRC_HLT, (0x01A934F0 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LLT, 0x01A934F0 & 0xFFFF);
	/* CT */
	snd_soc_write(codec, AC_DAC_DRC_HCT, (0x06A4D3C0 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LCT, 0x06A4D3C0 & 0xFFFF);
	/* ET */
	snd_soc_write(codec, AC_DAC_DRC_HET, (0x0BA07291 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LET, 0x0BA07291 & 0xFFFF);
	/* Ki */
	snd_soc_write(codec, AC_DAC_DRC_HKI, (0x00051EB8 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LKI, 0x00051EB8 & 0xFFFF);
	/* Kc */
	snd_soc_write(codec, AC_DAC_DRC_HKC, (0x00800000 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LKC, 0x00800000 & 0xFFFF);
	/* Kn */
	snd_soc_write(codec, AC_DAC_DRC_HKN, (0x01000000 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LKN, 0x01000000 & 0xFFFF);
	/* Ke */
	snd_soc_write(codec, AC_DAC_DRC_HKE, (0x0000F45F >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LKE, 0x0000F45F & 0xFFFF);
}

static void dachpf_config(struct snd_soc_codec *codec)
{
	/* HPF */
	snd_soc_write(codec, AC_DAC_DRC_HHPFC, (0xFFFAC1 >> 16) & 0xFFFF);
	snd_soc_write(codec, AC_DAC_DRC_LHPFC, 0xFFFAC1 & 0xFFFF);
}

static void dacdrc_enable(struct snd_soc_codec *codec, bool on)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (on) {
		/* detect noise when ET enable */
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_CONTROL_DRC_EN),
			(0x1 << DAC_DRC_CTL_CONTROL_DRC_EN));
		/* 0x0:RMS filter; 0x1:Peak filter */
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_SIGNAL_FUN_SEL),
			(0x1 << DAC_DRC_CTL_SIGNAL_FUN_SEL));
		/* delay function enable */
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_DEL_FUN_EN),
			(0x0 << DAC_DRC_CTL_DEL_FUN_EN));

		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_DRC_LT_EN),
			(0x1 << DAC_DRC_CTL_DRC_LT_EN));
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_DRC_ET_EN),
			(0x1 << DAC_DRC_CTL_DRC_ET_EN));

		snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
			(0x1 << DDAP_DRC_EN),
			(0x1 << DDAP_DRC_EN));

		if (sunxi_codec->dac_dap_enable++ == 0)
			snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
				(0x1 << DDAP_EN),
				(0x1 << DDAP_EN));
	} else {
		if (--sunxi_codec->dac_dap_enable == 0)
			snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
				(0x1 << DDAP_EN),
				(0x0 << DDAP_EN));

		snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
			(0x1 << DDAP_DRC_EN),
			(0x0 << DDAP_DRC_EN));

		/* detect noise when ET enable */
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_CONTROL_DRC_EN),
			(0x0 << DAC_DRC_CTL_CONTROL_DRC_EN));
		/* 0x0:RMS filter; 0x1:Peak filter */
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_SIGNAL_FUN_SEL),
			(0x1 << DAC_DRC_CTL_SIGNAL_FUN_SEL));
		/* delay function enable */
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_DEL_FUN_EN),
			(0x0 << DAC_DRC_CTL_DEL_FUN_EN));

		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_DRC_LT_EN),
			(0x0 << DAC_DRC_CTL_DRC_LT_EN));
		snd_soc_update_bits(codec, AC_DAC_DRC_CTL,
			(0x1 << DAC_DRC_CTL_DRC_ET_EN),
			(0x0 << DAC_DRC_CTL_DRC_ET_EN));
	}
}

static void dachpf_enable(struct snd_soc_codec *codec, bool on)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (on) {
		snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
			(0x1 << DDAP_HPF_EN),
			(0x1 << DDAP_HPF_EN));

		if (sunxi_codec->dac_dap_enable++ == 0)
			snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
				(0x1 << DDAP_EN),
				(0x1 << DDAP_EN));
	} else {
		if (--sunxi_codec->dac_dap_enable == 0)
			snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
				(0x1 << DDAP_EN),
				(0x0 << DDAP_EN));

		snd_soc_update_bits(codec, SUNXI_DAC_DAP_CTL,
			(0x1 << DDAP_HPF_EN),
			(0x0 << DDAP_HPF_EN));
	}
}

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
static int sunxi_codec_get_i2s_port(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = sunxi_codec->i2s_port;

	return 0;
}

static int sunxi_codec_set_i2s_port(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	switch (ucontrol->value.integer.value[0]) {
	case SUNXI_CODEC_MAP_PORT_NULL:
	case SUNXI_CODEC_MAP_PORT_I2S0:
	case SUNXI_CODEC_MAP_PORT_I2S1:
	case SUNXI_CODEC_MAP_PORT_I2S2:
		sunxi_codec->i2s_port = ucontrol->value.integer.value[0];
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* sunxi codec hub mdoe select */
static const char * const sunxi_codec_i2s_mux[] = {"I2S_NULL", "I2S0_RX",
			"I2S1_RX", "I2S2_RX"};

static const struct soc_enum sunxi_codec_i2s_port_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sunxi_codec_i2s_mux),
			sunxi_codec_i2s_mux),
};
#endif

static int sunxi_codec_get_hub_mode(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	unsigned int reg_val;

	reg_val = snd_soc_read(codec, SUNXI_DAC_DPC);

	ucontrol->value.integer.value[0] = ((reg_val & (1<<DAC_HUB_EN)) ? 2 : 1);
	return 0;
}

static int sunxi_codec_set_hub_mode(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	switch (ucontrol->value.integer.value[0]) {
	case	0:
	case	1:
		snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1 << DAC_HUB_EN), (0x0 << DAC_HUB_EN));
		break;
	case	2:
		snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1 << DAC_HUB_EN), (0x1 << DAC_HUB_EN));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* sunxi codec hub mdoe select */
static const char * const sunxi_codec_hub_function[] = {"null",
			"hub_disable", "hub_enable"};

static const struct soc_enum sunxi_codec_hub_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sunxi_codec_hub_function),
			sunxi_codec_hub_function),
};

static int sunxi_spk_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, SUNXI_LINEOUT_CTL0,
				(0x1 << LINEOUTL_EN) | (0x1 << LINEOUTR_EN),
				(0x1 << LINEOUTL_EN) | (0x1 << LINEOUTR_EN));
		if (sunxi_codec->pa_power_cfg.used) {
			gpio_set_value(sunxi_codec->pa_power_cfg.gpio, 1);
		}
		if (sunxi_codec->spk_cfg.used) {
			gpio_set_value(sunxi_codec->spk_cfg.gpio,
				sunxi_codec->spk_cfg.pa_ctl_level);
			/*
			 * time delay to wait spk pa work fine,
			 * general setting 160ms. For pop sound,
			 * maybe set less than it.
			 */
			msleep(sunxi_codec->spk_cfg.pa_msleep_time);
		}
		break;
	case	SND_SOC_DAPM_PRE_PMD:
		if (sunxi_codec->pa_power_cfg.used) {
			gpio_set_value(sunxi_codec->pa_power_cfg.gpio, 0);
		}
		if (sunxi_codec->spk_cfg.used) {
			gpio_set_value(sunxi_codec->spk_cfg.gpio,
				!(sunxi_codec->spk_cfg.pa_ctl_level));
		}

		snd_soc_update_bits(codec, SUNXI_LINEOUT_CTL0,
				(0x1 << LINEOUTL_EN) | (0x1 << LINEOUTR_EN),
				(0x0 << LINEOUTL_EN) | (0x0 << LINEOUTR_EN));
		break;
	default:
		break;
	}
	return 0;
}

static int sunxi_lineout_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, SUNXI_LINEOUT_CTL0,
				(0x1 << LINEOUTL_EN) | (0x1 << LINEOUTR_EN),
				(0x1 << LINEOUTL_EN) | (0x1 << LINEOUTR_EN));
		msleep(lineout_msleep);
		break;
	case	SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, SUNXI_LINEOUT_CTL0,
				(0x1 << LINEOUTL_EN) | (0x1 << LINEOUTR_EN),
				(0x0 << LINEOUTL_EN) | (0x0 << LINEOUTR_EN));
		break;
	default:
		break;
	}

	return 0;
}

static int sunxi_playback_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&sunxi_codec->dac_mutex);
	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		if (sunxi_codec->dac_enable++ == 0) {
			snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1 << EN_DAC), (0x1 << EN_DAC));
			/* time delay to wait digital dac work fine */
			msleep(dac_msleep);
		}
		break;
	case	SND_SOC_DAPM_POST_PMD:
		if (--sunxi_codec->dac_enable == 0)
			snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1 << EN_DAC), (0x0 << EN_DAC));
		break;
	default:
		break;
	}
	mutex_unlock(&sunxi_codec->dac_mutex);

	return 0;
}

static int sunxi_capture_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&sunxi_codec->adc_mutex);
	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		if (sunxi_codec->adc_enable++ == 0) {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
				(0x1 << EN_AD), (0x1 << EN_AD));
		}
		break;
	case	SND_SOC_DAPM_POST_PMD:
		if (--sunxi_codec->adc_enable == 0) {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
				(0x1 << EN_AD), (0x0 << EN_AD));
		}
		break;
	}
	mutex_unlock(&sunxi_codec->adc_mutex);

	return 0;
}

/* add mixer kcontrol for PA mute */
int sunxi_pashdn_get_data(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct spk_config *spk_cfg = &(sunxi_codec->spk_cfg);

	if (spk_cfg->used)
		ucontrol->value.integer.value[0] = gpio_get_value(spk_cfg->gpio);
	else
		ucontrol->value.integer.value[0] = 0;
	return 0;
}

int sunxi_pashdn_put_data(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct spk_config *spk_cfg = &(sunxi_codec->spk_cfg);

	if (spk_cfg->used) {
		int val = ucontrol->value.integer.value[0];
		gpio_set_value(spk_cfg->gpio, val);
	}

	return 0;
}

static const struct snd_kcontrol_new sunxi_codec_controls[] = {
	SOC_ENUM_EXT("codec hub mode", sunxi_codec_hub_mode_enum[0],
				sunxi_codec_get_hub_mode,
				sunxi_codec_set_hub_mode),
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	SOC_ENUM_EXT("codec I2S Port", sunxi_codec_i2s_port_enum[0],
				sunxi_codec_get_i2s_port,
				sunxi_codec_set_i2s_port),
#endif

	SOC_SINGLE_TLV("digital volume", SUNXI_DAC_DPC,
				DVOL, 0x3F, 1, digital_tlv),
	SOC_SINGLE_TLV("MIC1 gain volume", SUNXI_MIC1_CTL,
				MIC1BOOST, 0x7, 0, mic_gain_tlv),
	SOC_SINGLE_TLV("MIC2 gain volume", SUNXI_MIC2_MIC3_CTL,
				MIC2BOOST, 0x7, 0, mic_gain_tlv),
	SOC_SINGLE_TLV("MIC3 gain volume", SUNXI_MIC2_MIC3_CTL,
				MIC3BOOST, 0x7, 0, mic_gain_tlv),
	SOC_SINGLE_TLV("LINEOUT volume", SUNXI_LINEOUT_CTL1,
				LINEOUT_VOL, 0x1F, 0, lineout_tlv),
	SOC_SINGLE_TLV("ADC gain volume", SUNXI_ADC_CTL,
				ADCG, 0x7, 0, adc_gain_tlv),
	SOC_SINGLE_BOOL_EXT("Spk PA Switch", 0,
			sunxi_pashdn_get_data, sunxi_pashdn_put_data),
};

static const struct snd_kcontrol_new left_input_mixer[] = {
	SOC_DAPM_SINGLE("MIC3 Boost Switch", SUNXI_LADCMIX_SRC,
					LADC_MIC3_STAGE, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_LADCMIX_SRC,
					LADC_MIC2_STAGE, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_LADCMIX_SRC,
					LADC_MIC1_STAGE, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", SUNXI_LADCMIX_SRC,
					LADC_DACL, 1, 0),
};

static const struct snd_kcontrol_new right_input_mixer[] = {
	SOC_DAPM_SINGLE("MIC3 Boost Switch", SUNXI_RADCMIX_SRC,
					RADC_MIC3_STAGE, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_RADCMIX_SRC,
					RADC_MIC2_STAGE, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_RADCMIX_SRC,
					RADC_MIC1_STAGE, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", SUNXI_RADCMIX_SRC,
					RADC_DACL, 1, 0),
};

static const struct snd_kcontrol_new xadc_input_mixer[] = {
	SOC_DAPM_SINGLE("MIC3 Boost Switch", SUNXI_XADCMIX_SRC,
					XADC_MIC3_STAGE, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_XADCMIX_SRC,
					XADC_MIC2_STAGE, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_XADCMIX_SRC,
					XADC_MIC1_STAGE, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", SUNXI_XADCMIX_SRC,
					XADC_DACL, 1, 0),
};

static const struct snd_soc_dapm_widget sunxi_codec_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("DACL", "Playback", 0,
			SUNXI_MIX_DAC_CTL, DACALEN, 0,
			sunxi_playback_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("ADCL", "Capture", 0,
			SUNXI_ADC_CTL, ADCLEN, 0,
			sunxi_capture_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("ADCR", "Capture", 0,
			SUNXI_ADC_CTL, ADCREN, 0,
			sunxi_capture_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("ADCX", "Capture", 0,
			SUNXI_ADC_CTL, ADCXEN, 0,
			sunxi_capture_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("Left Input Mixer", SND_SOC_NOPM, 0, 0,
			left_input_mixer, ARRAY_SIZE(left_input_mixer)),
	SND_SOC_DAPM_MIXER("Right Input Mixer", SND_SOC_NOPM, 0, 0,
			right_input_mixer, ARRAY_SIZE(right_input_mixer)),
	SND_SOC_DAPM_MIXER("Xadc Input Mixer", SND_SOC_NOPM, 0, 0,
			xadc_input_mixer, ARRAY_SIZE(xadc_input_mixer)),

	SND_SOC_DAPM_MUX("Left LINEOUT Mux", SND_SOC_NOPM,
			0, 0, &left_lineout_mux),
	SND_SOC_DAPM_MUX("Right LINEOUT Mux", SND_SOC_NOPM,
			0, 0, &right_lineout_mux),

	SND_SOC_DAPM_PGA("MIC1 PGA", SUNXI_MIC1_CTL,
			MIC1AMPEN, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2 PGA", SUNXI_MIC2_MIC3_CTL,
			MIC2AMPEN, 0, NULL, 0),

	SND_SOC_DAPM_PGA("MIC3 PGA", SUNXI_MIC2_MIC3_CTL,
			MIC3AMPEN, 0, NULL, 0),

	SND_SOC_DAPM_MICBIAS("MainMic Bias", SUNXI_MBIAS_CTL,
			MMICBIASEN, 0),

	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),

	SND_SOC_DAPM_OUTPUT("LINEOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEOUTR"),

	SND_SOC_DAPM_LINE("Lineout", sunxi_lineout_event),
	SND_SOC_DAPM_SPK("External Speaker", sunxi_spk_event),
};

static const struct snd_soc_dapm_route sunxi_codec_dapm_routes[] = {
	/* input route */
	{"MIC1 PGA", NULL, "MIC1"},
	{"MIC2 PGA", NULL, "MIC2"},
	{"MIC3 PGA", NULL, "MIC3"},

	{"MIC1", NULL, "MainMic Bias"},
	{"MIC2", NULL, "MainMic Bias"},
	{"MIC3", NULL, "MainMic Bias"},

	{"Left Input Mixer", "DACL Switch", "DACL"},
	{"Left Input Mixer", "MIC3 Boost Switch", "MIC3 PGA"},
	{"Left Input Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Left Input Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"Right Input Mixer", "DACL Switch", "DACL"},
	{"Right Input Mixer", "MIC3 Boost Switch", "MIC3 PGA"},
	{"Right Input Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Right Input Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"Xadc Input Mixer", "DACL Switch", "DACL"},
	{"Xadc Input Mixer", "MIC3 Boost Switch", "MIC3 PGA"},
	{"Xadc Input Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Xadc Input Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"ADCL", NULL, "Left Input Mixer"},
	{"ADCR", NULL, "Right Input Mixer"},
	{"ADCX", NULL, "Xadc Input Mixer"},

	/* output route */
	{"Left LINEOUT Mux", "DACL", "DACL"},
	{"Right LINEOUT Mux", "DACL", "DACL"},

	{"Left LINEOUT Mux", "DAC_NULL", "DACL"},
	{"Right LINEOUT Mux", "DAC_NULL", "DACL"},

	{"LINEOUTL", NULL, "Left LINEOUT Mux"},
	{"LINEOUTR", NULL, "Right LINEOUT Mux"},
};

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
static int sunxi_codec_map_i2s_startup(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		/*
		 * pcm_open:
		 * [1] cpu_dai->ops->startup;
		 * [2] platform->ops->open;
		 * [3] codec_dai->ops->startup;
		 */
		switch (sunxi_codec->i2s_port) {
		case SUNXI_CODEC_MAP_PORT_NULL:
		default:
			break;
		case SUNXI_CODEC_MAP_PORT_I2S0:
			if (clk_prepare_enable(sunxi_codec->daudio[0].i2s_clk))
				pr_err("i2s0 module clk enable failed\n");
			break;
		case SUNXI_CODEC_MAP_PORT_I2S1:
			if (clk_prepare_enable(sunxi_codec->daudio[1].i2s_clk))
				pr_err("i2s1 module clk enable failed\n");
			break;
		case SUNXI_CODEC_MAP_PORT_I2S2:
			if (clk_prepare_enable(sunxi_codec->daudio[2].i2s_clk))
				pr_err("i2s2 module clk enable failed\n");
			break;
		}
	}

	return 0;
}

static void sunxi_codec_map_i2s_global_enable(struct sunxi_codec_info *sunxi_codec,
				bool enable)
{
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(1 << LOOP_EN), (enable << LOOP_EN));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(1 << GLOBAL_EN), (enable << GLOBAL_EN));
}

static void sunxi_codec_map_i2s_txctrl_enable(struct sunxi_codec_info *sunxi_codec,
				bool enable)
{
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	if (enable) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << SDO0_EN), (1 << SDO0_EN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << CTL_TXEN), (1 << CTL_TXEN));
		regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_INTCTL,
					(1 << TXDRQEN), (1 << TXDRQEN));
	} else {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_INTCTL,
					(1 << TXDRQEN), (0 << TXDRQEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
					(1 << CTL_TXEN), (0 << CTL_TXEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
					(1 << SDO0_EN), (0 << SDO0_EN));
	}
}

static void sunxi_codec_map_i2s_rxctrl_enable(struct sunxi_codec_info *sunxi_codec,
				bool enable)
{
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	if (enable) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << CTL_RXEN), (1 << CTL_RXEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_INTCTL,
				(1 << RXDRQEN), (1 << RXDRQEN));
	} else {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_INTCTL,
				(1 << RXDRQEN), (0 << RXDRQEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << CTL_RXEN), (0 << CTL_RXEN));
	}
}

static int sunxi_codec_map_i2s_dma_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	int ret = 0;
	struct dmaengine_pcm_runtime_data *prtd;
	struct dma_slave_config slave_config;
	int DRQDST_TYPE_ID = 0;
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	sunxi_codec->prtd = kzalloc(sizeof(struct dmaengine_pcm_runtime_data),
					GFP_KERNEL);
	if (!sunxi_codec->prtd) {
		pr_err("[%s] kzalloc prtd failed.\n", __func__);
		return -ENOMEM;
	}
	prtd = sunxi_codec->prtd;

	ret = sunxi_snd_dmaengine_pcm_open(prtd, NULL, NULL);
	if (ret) {
		pr_err("[%s] mad request dma chan failed.[%d]\n", __func__, ret);
		return ret;
	}

	slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	/* codec rx */
	slave_config.src_addr = 0x5096000 + SUNXI_ADC_RXDATA;
	slave_config.src_maxburst = 8;

	/* i2s tx */
	slave_config.dst_maxburst = 8;
	switch (sunxi_codec->i2s_port) {
	case SUNXI_CODEC_MAP_PORT_I2S0:
	default:
		DRQDST_TYPE_ID = DRQDST_DAUDIO_0_TX;
		slave_config.dst_addr = sunxi_codec->daudio[0].res.start +
					SUNXI_DAUDIO_TXFIFO;
		break;
	case SUNXI_CODEC_MAP_PORT_I2S1:
		DRQDST_TYPE_ID = DRQDST_DAUDIO_1_TX;
		slave_config.dst_addr = sunxi_codec->daudio[1].res.start +
					SUNXI_DAUDIO_TXFIFO;
		break;
	case SUNXI_CODEC_MAP_PORT_I2S2:
		DRQDST_TYPE_ID = DRQDST_DAUDIO_2_TX;
		slave_config.dst_addr = sunxi_codec->daudio[2].res.start +
					SUNXI_DAUDIO_TXFIFO;
		break;
	}
	slave_config.slave_id = sunxi_slave_id(DRQDST_TYPE_ID, DRQSRC_AUDIO_CODEC);
	slave_config.direction = DMA_DEV_TO_DEV;

	ret = dmaengine_slave_config(prtd->dma_chan, &slave_config);
	if (ret < 0) {
		pr_err("[%s] dma slave config failed with err.[%d]\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int sunxi_codec_map_i2s_dma_trigger(struct sunxi_codec_info *sunxi_codec,
					bool enable)
{
	if (enable) {
		if ((sunxi_codec->i2s_port != SUNXI_CODEC_MAP_PORT_NULL) &&
			(sunxi_codec->i2s_mapdma_trigger == 0)) {
			sunxi_snd_dmaengine_pcm_trigger(sunxi_codec->prtd,
					SNDRV_PCM_TRIGGER_START, 0,
					DMA_DEV_TO_DEV,
					sunxi_codec->buffer_bytes,
					sunxi_codec->period_bytes);
			sunxi_codec->i2s_mapdma_trigger = 1;
		}
	} else {
		if ((sunxi_codec->i2s_port != SUNXI_CODEC_MAP_PORT_NULL) &&
			(sunxi_codec->i2s_mapdma_trigger == 1)) {
			sunxi_snd_dmaengine_pcm_trigger(sunxi_codec->prtd,
					SNDRV_PCM_TRIGGER_STOP, 0,
					DMA_DEV_TO_DEV, 0, 0);
			sunxi_codec->i2s_mapdma_trigger = 0;
		}
	}
	return 0;
}

static void sunxi_codec_map_i2s_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (sunxi_codec->prtd != NULL) {
			sunxi_snd_dmaengine_pcm_close(sunxi_codec->prtd);
			sunxi_codec->prtd = NULL;
		}
		/*
		 * pcm_close:
		 * [1] cpu_dai->ops->shutdown;
		 * [2] codec_dai->ops->shutdown;
		 * [3] platform->ops->shutdown;
		 */
		switch (sunxi_codec->i2s_port) {
		case SUNXI_CODEC_MAP_PORT_NULL:
		default:
			break;
		case SUNXI_CODEC_MAP_PORT_I2S0:
			clk_disable_unprepare(sunxi_codec->daudio[0].i2s_clk);
			break;
		case SUNXI_CODEC_MAP_PORT_I2S1:
			clk_disable_unprepare(sunxi_codec->daudio[1].i2s_clk);
			break;
		case SUNXI_CODEC_MAP_PORT_I2S2:
			clk_disable_unprepare(sunxi_codec->daudio[2].i2s_clk);
			break;
		}
	}
}

#ifdef CONFIG_SND_SUNXI_MAD
static int sunxi_codec_map_i2s_mad_startup(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sunxi_sndcodec_priv *sndcodec_priv =
				snd_soc_card_get_drvdata(card);
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		sunxi_codec->mad_priv = &(sndcodec_priv->mad_priv);

	return 0;
}

static int sunxi_codec_map_i2s_mad_set_path(struct sunxi_codec_info *sunxi_codec,
					bool enable)
{
	unsigned int path_sel;

	switch (sunxi_codec->i2s_port) {
	case SUNXI_CODEC_MAP_PORT_I2S0:
		path_sel = MAD_PATH_I2S0;
		break;
	case SUNXI_CODEC_MAP_PORT_I2S1:
		path_sel = MAD_PATH_I2S1;
		break;
	case SUNXI_CODEC_MAP_PORT_I2S2:
		path_sel = MAD_PATH_I2S2;
		break;
	default:
		dev_err(sunxi_codec->dev, "unsupported I2S number!\n");
		return -EINVAL;
	}
	/*
	 * should set it after sram reset.
	 */
	return sunxi_mad_audio_source_sel(path_sel, enable);
}

static void sunxi_codec_mad_debug_control(struct sunxi_codec_info *sunxi_codec,
					bool enable)
{
	struct sunxi_mad_priv *mad_priv = sunxi_codec->mad_priv;

	mutex_lock(&sunxi_codec->mad_clk_mutex);

	if (enable) {
		if (!sunxi_codec->mad_clk_enable) {
			sunxi_mad_module_clk_enable(true);
			sunxi_codec->mad_clk_enable = true;
		}
		/* sunxi_mad_close(); */
		sunxi_mad_open();

		sunxi_mad_hw_params(mad_priv->audio_src_chan_num,
					mad_priv->sample_rate);
		sunxi_mad_audio_src_chan_num(mad_priv->audio_src_chan_num);
		sunxi_lpsd_chan_sel(mad_priv->lpsd_chan_sel);
		sunxi_mad_standby_chan_sel(mad_priv->mad_standby_chan_sel);
		sunxi_mad_dma_type(SUNXI_MAD_DMA_IO);
		sunxi_sram_ahb1_threshole_init();
		sunxi_mad_sram_init();

		sunxi_codec_map_i2s_mad_set_path(sunxi_codec, true);
	} else {
		sunxi_mad_sram_set_reset_bit();
		if (sunxi_codec->mad_clk_enable) {
			sunxi_mad_module_clk_enable(false);
			sunxi_codec->mad_clk_enable = false;
		}
	}
	mutex_unlock(&sunxi_codec->mad_clk_mutex);
}

static void sunxi_codec_mad_debug_reset(struct sunxi_codec_info *sunxi_codec)
{
	struct sunxi_mad_priv *mad_priv = sunxi_codec->mad_priv;

	/* maybe had been disable because the cpu scheduling */
	mutex_lock(&sunxi_codec->mad_clk_mutex);

	if (sunxi_codec->mad_clk_enable != true) {
		mutex_unlock(&sunxi_codec->mad_clk_mutex);
		return;
	}

	/* for reset mad module */
	sunxi_mad_sram_set_reset_bit();
	sunxi_mad_module_clk_enable(false);
	sunxi_mad_module_clk_enable(true);
	sunxi_codec->mad_clk_enable = true;

	/* sunxi_mad_close(); */
	sunxi_mad_open();

	sunxi_mad_hw_params(mad_priv->audio_src_chan_num, mad_priv->sample_rate);
	sunxi_mad_audio_src_chan_num(mad_priv->audio_src_chan_num);
	sunxi_lpsd_chan_sel(mad_priv->lpsd_chan_sel);
	sunxi_mad_standby_chan_sel(mad_priv->mad_standby_chan_sel);
	sunxi_mad_dma_type(SUNXI_MAD_DMA_IO);
	sunxi_sram_ahb1_threshole_init();
	sunxi_mad_sram_init();

	sunxi_codec_map_i2s_mad_set_path(sunxi_codec, true);

	mutex_unlock(&sunxi_codec->mad_clk_mutex);
}


static void sunxi_codec_map_i2s_rxctrl_stop_mad_reset(struct work_struct *work)
{
	struct sunxi_codec_info *sunxi_codec =
		container_of(work, struct sunxi_codec_info, ws_rx_mad_reset);
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	sunxi_codec_mad_debug_reset(sunxi_codec);

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x1 << DAUDIO_MAD_DATA_EN));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << CTL_RXEN), (1 << CTL_RXEN));

#ifdef SUNXI_MAD_SRAM_SUSPEND_RESET
	sunxi_mad_sram_set_reset_flag(SUNXI_MAD_SRAM_RESET_END);
#endif
}

#if 0
static void sunxi_codec_map_i2s_mad_reset(struct sunxi_codec_info *sunxi_codec,
				bool enable)
{
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	if (enable) {
#ifdef SUNXI_MAD_SRAM_SUSPEND_RESET
		sunxi_mad_sram_set_reset_flag(SUNXI_MAD_SRAM_RESET_START);
#endif

		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << CTL_RXEN), (0 << CTL_RXEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x0 << DAUDIO_MAD_DATA_EN));
		sunxi_mad_dma_enable(false);

		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
				(0x1 << FIFO_CTL_FRX),
				(0x1 << FIFO_CTL_FRX));
		regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);

		schedule_work(&(sunxi_codec->ws_rx_mad_reset));
	}
}
#endif

static void sunxi_codec_map_i2s_mad_control(struct sunxi_codec_info *sunxi_codec,
					bool enable)
{
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	if (enable) {
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x1 << DAUDIO_MAD_DATA_EN));
		sunxi_codec_mad_debug_control(sunxi_codec, true);
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(1 << CTL_RXEN), (1 << CTL_RXEN));
		sunxi_mad_dma_type(SUNXI_MAD_DMA_IO);
		sunxi_mad_dma_enable(true);
	} else {
		sunxi_mad_dma_enable(false);
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x0 << DAUDIO_MAD_DATA_EN));
	}
}

void sunxi_codec_mad_enter_standby(struct sunxi_codec_info *sunxi_codec)
{
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	sunxi_codec->capture_en = 1;

	sunxi_codec_mad_debug_control(sunxi_codec, false);

	regmap_update_bits(sunxi_codec->regmap, SUNXI_ADC_FIFO_CTL,
			(1<<ADC_FIFO_FLUSH), (1<<ADC_FIFO_FLUSH));
	regmap_write(sunxi_codec->regmap, SUNXI_ADC_FIFO_STA,
			(1 << ADC_RXA_INT | 1 << ADC_RXO_INT));
	regmap_write(sunxi_codec->regmap, SUNXI_ADC_CNT, 0);

	//tx fifo prepare
	regmap_update_bits(sunxi_codec->sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(1 << FIFO_CTL_FTX), (1 << FIFO_CTL_FTX));
	regmap_write(sunxi_codec->sunxi_daudio->regmap, SUNXI_DAUDIO_TXCNT, 0);

	//rx fifo preapre
	regmap_update_bits(sunxi_codec->sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(1 << FIFO_CTL_FRX), (1 << FIFO_CTL_FRX));
	regmap_write(sunxi_codec->sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);

	sunxi_codec_map_i2s_dma_trigger(sunxi_codec, true);

	sunxi_codec_map_i2s_txctrl_enable(sunxi_codec, true);
	regmap_update_bits(sunxi_codec->sunxi_daudio->regmap, SUNXI_DAUDIO_INTCTL,
			(1 << RXDRQEN), (1 << RXDRQEN));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(1 << CTL_RXEN), (1 << CTL_RXEN));

	regmap_update_bits(sunxi_codec->regmap,
			SUNXI_ADC_FIFO_CTL,
			(1<<ADC_DRQ_EN), (1<<ADC_DRQ_EN));
	sunxi_codec_map_i2s_global_enable(sunxi_codec, true);
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x1 << DAUDIO_MAD_DATA_EN));
	sunxi_codec_mad_debug_control(sunxi_codec, true);
}
EXPORT_SYMBOL_GPL(sunxi_codec_mad_enter_standby);

static void sunxi_codec_map_i2s_mad_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;
	struct sunxi_mad_priv *mad_priv = sunxi_codec->mad_priv;

	if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) &&
		(mad_priv->mad_bind == 1)) {
		if (sunxi_daudio == NULL) {
			pr_debug("[%s] sunxi_daudio is null.\n", __func__);
			return;
		}

		regmap_update_bits(sunxi_daudio->regmap,
				SUNXI_DAUDIO_CTL,
				(1<<CTL_RXEN), (0<<CTL_RXEN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
				(0x1 << DAUDIO_MAD_DATA_EN),
				(0x0 << DAUDIO_MAD_DATA_EN));

		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(1 << LOOP_EN), (0 << LOOP_EN));
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(1 << GLOBAL_EN), (0 << GLOBAL_EN));

		/*
		 * pcm_close:
		 * [1] cpu_dai->ops->shutdown;
		 * [2] codec_dai->ops->shutdown;
		 * [3] platform->ops->shutdown;
		 */
		mutex_lock(&sunxi_codec->mad_clk_mutex);
		/* diable daudio src*/
		sunxi_codec_map_i2s_mad_set_path(sunxi_codec, false);
		sunxi_codec->capture_en = 0;

		sunxi_mad_close();

#ifndef MAD_CLK_ALWAYS_ON
		/*sunxi_mad_sram_set_reset_bit();*/
		if (sunxi_codec->mad_clk_enable) {
			sunxi_mad_module_clk_enable(false);
			sunxi_codec->mad_clk_enable = false;
		}
#endif
		mutex_unlock(&sunxi_codec->mad_clk_mutex);
	}
#endif
}

static int sunxi_codec_i2s_set_clkdiv(struct snd_soc_dai *dai,
				int clk_id, int clk_div)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;
	unsigned int bclk_div, div_ratio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return -EINVAL;
	}
	sunxi_daudio->pcm_lrck_period = 64;

	/* I2S/TDM two channel mode */
	div_ratio = clk_div/(2 * sunxi_daudio->pcm_lrck_period);

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
			(SUNXI_DAUDIO_BCLK_DIV_MASK << BCLK_DIV),
			(bclk_div << BCLK_DIV));

	return 0;
}

static int sunxi_codec_map_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;
	unsigned int frame_type = 0;
	unsigned int slot_width_select = 32;
	unsigned int msb_lsb_first = 0;
	unsigned int sign_extend = 0;
	unsigned int tx_data_mode = 0;
	unsigned int rx_data_mode = 0;

	sunxi_daudio->pcm_lrck_period = 64;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return -EINVAL;
	}

	/* SND_SOC_DAIFMT_CBS_CFS */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(SUNXI_DAUDIO_LRCK_OUT_MASK << LRCK_OUT),
			(SUNXI_DAUDIO_LRCK_OUT_ENABLE << LRCK_OUT));

	/* SND_SOC_DAIFMT_I2S */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CTL,
			(SUNXI_DAUDIO_MODE_CTL_MASK << MODE_SEL),
			(SUNXI_DAUDIO_MODE_CTL_I2S << MODE_SEL));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_TX0CHSEL,
			(SUNXI_DAUDIO_TX_OFFSET_MASK << TX_OFFSET),
			(SUNXI_DAUDIO_TX_OFFSET_1 << TX_OFFSET));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCHSEL,
			(SUNXI_DAUDIO_RX_OFFSET_MASK << RX_OFFSET),
			(SUNXI_DAUDIO_TX_OFFSET_1 << RX_OFFSET));

	/* SND_SOC_DAIFMT_NB_NF */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(1 << LRCK_POLARITY),
			(SUNXI_DAUDIO_LRCK_POLARITY_NOR << LRCK_POLARITY));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(1<<BRCK_POLARITY),
			(SUNXI_DAUDIO_BCLK_POLARITY_NOR << BRCK_POLARITY));

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(1 << LRCK_WIDTH),
			(frame_type << LRCK_WIDTH));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(SUNXI_DAUDIO_LRCK_PERIOD_MASK) << LRCK_PERIOD,
			((sunxi_daudio->pcm_lrck_period - 1) << LRCK_PERIOD));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(SUNXI_DAUDIO_SLOT_WIDTH_MASK << SLOT_WIDTH),
			(((slot_width_select >> 2) - 1) << SLOT_WIDTH));

	/*
	 * MSB on the transmit format, always be first.
	 * default using Linear-PCM, without no companding.
	 * A-law<Eourpean standard> or U-law<US-Japan> not working ok.
	 */
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x1 <<  TX_MLS),
			(msb_lsb_first << TX_MLS));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x1 <<  RX_MLS),
			(msb_lsb_first << RX_MLS));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x3 <<  SEXT),
			(sign_extend << SEXT));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x3 <<  TX_PDM),
			(tx_data_mode << TX_PDM));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT1,
			(0x3 <<  RX_PDM),
			(rx_data_mode << RX_PDM));
	return 0;
}

static int sunxi_codec_map_i2s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;
#ifdef CONFIG_SND_SUNXI_MAD
	struct sunxi_mad_priv *mad_priv = sunxi_codec->mad_priv;
#endif
	unsigned int SUNXI_DAUDIO_RXCHMAPX = 0;
	int index = 0;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return -EINVAL;
	}

	/*
	 * sample rate
	 * only set 16bit sample resolution.
	 */
	switch (params_format(params)) {
	case	SNDRV_PCM_FORMAT_S16_LE:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(SUNXI_DAUDIO_SR_MASK << DAUDIO_SAMPLE_RESOLUTION),
			(SUNXI_DAUDIO_SR_16BIT << DAUDIO_SAMPLE_RESOLUTION));
		break;
	case	SNDRV_PCM_FORMAT_S32_LE:
		/* only for the compatible of tinyalsa */
	case	SNDRV_PCM_FORMAT_S24_LE:
		regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FMT0,
			(SUNXI_DAUDIO_SR_MASK << DAUDIO_SAMPLE_RESOLUTION),
			(SUNXI_DAUDIO_SR_24BIT << DAUDIO_SAMPLE_RESOLUTION));
		break;
	default:
		pr_err("[%s] params_format[%d] error!\n", __func__,
			params_format(params));
		return -EINVAL;
	}

	//TX Input Mode & RX Output Mode
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(SUNXI_DAUDIO_TXIM_MASK << TXIM),
			(SUNXI_DAUDIO_TXIM_VALID_LSB << TXIM));

	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(SUNXI_DAUDIO_RXOM_MASK << RXOM),
			(SUNXI_DAUDIO_RXOM_EXPH << RXOM));

	//TX slot
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CHCFG,
			(SUNXI_DAUDIO_TX_SLOT_MASK << TX_SLOT_NUM),
			((params_channels(params) - 1) << TX_SLOT_NUM));
	regmap_write(sunxi_daudio->regmap,
			SUNXI_DAUDIO_TX0CHMAP0, SUNXI_DEFAULT_CHMAP0);
	regmap_write(sunxi_daudio->regmap,
			SUNXI_DAUDIO_TX0CHMAP1, SUNXI_DEFAULT_CHMAP1);
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_TX0CHSEL,
			(SUNXI_DAUDIO_TX_CHSEL_MASK << TX_CHSEL),
			((params_channels(params) - 1) << TX_CHSEL));
	regmap_update_bits(sunxi_daudio->regmap,
			SUNXI_DAUDIO_TX0CHSEL,
			(SUNXI_DAUDIO_TX_CHEN_MASK << TX_CHEN),
			((1 << params_channels(params)) - 1) << TX_CHEN);

	//RX_SLOT
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
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_CHCFG,
			(SUNXI_DAUDIO_RX_SLOT_MASK << RX_SLOT_NUM),
			((params_channels(params) - 1) << RX_SLOT_NUM));
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCHSEL,
			(SUNXI_DAUDIO_RX_CHSEL_MASK << RX_CHSEL),
			((params_channels(params) - 1) << RX_CHSEL));

	/* MAD hw_params */
#ifdef CONFIG_SND_SUNXI_MAD
	/*mad only supported 16k/48KHz samplerate when capturing*/
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (mad_priv->mad_bind == 1) {
			/* mad only receive the high 16bit */
			regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_FIFOCTL,
					(SUNXI_DAUDIO_RXOM_MASK << RXOM),
					(SUNXI_DAUDIO_RXOM_TUNL << RXOM));
			/* mad config */
			if (params_format(params) != SNDRV_PCM_FORMAT_S16_LE) {
				dev_err(sunxi_codec->dev,
					"unsupported mad sample bits\n");
				return -EINVAL;
			}

			mad_priv->audio_src_chan_num = params_channels(params);
			mad_priv->sample_rate = params_rate(params);
			sunxi_mad_audio_src_chan_num(mad_priv->audio_src_chan_num);
			sunxi_lpsd_chan_sel(mad_priv->lpsd_chan_sel);
			sunxi_mad_standby_chan_sel(mad_priv->mad_standby_chan_sel);
		}
	}
#endif
	return 0;
}

static void sunxi_mad_codec_i2s_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;

	if (sunxi_daudio == NULL) {
		pr_debug("[%s] sunxi_daudio is null.\n", __func__);
		return;
	}

	//tx fifo prepare
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(1 << FIFO_CTL_FTX), (1 << FIFO_CTL_FTX));
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_TXCNT, 0);

	//rx fifo preapre
	regmap_update_bits(sunxi_daudio->regmap, SUNXI_DAUDIO_FIFOCTL,
			(1 << FIFO_CTL_FRX), (1 << FIFO_CTL_FRX));
	regmap_write(sunxi_daudio->regmap, SUNXI_DAUDIO_RXCNT, 0);
}
#endif

static void sunxi_codec_init(struct snd_soc_codec *codec)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	/* Disable DRC function for playback */
	snd_soc_write(codec, SUNXI_DAC_DAP_CTL, 0);

	/* Disable HPF(high passed filter) */
	snd_soc_update_bits(codec, SUNXI_DAC_DPC,
			(1 << HPF_EN), (0x0 << HPF_EN));

	/* Enable ADCFDT to overcome niose at the beginning */
	snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
			(7 << ADCDFEN), (7 << ADCDFEN));

	snd_soc_update_bits(codec, SUNXI_DAC_DPC,
			0x3f << DVOL,
			sunxi_codec->gain_cfg.digital_vol << DVOL);
	snd_soc_update_bits(codec, SUNXI_MIC1_CTL,
			0x7 << MIC1BOOST,
			sunxi_codec->gain_cfg.mic1gain << MIC1BOOST);

	/* must defautl init: 0x44 */
	snd_soc_write(codec, SUNXI_MIC2_MIC3_CTL, 0x44);
	/* must defautl init: 0x0 */
	snd_soc_write(codec, SUNXI_LADCMIX_SRC, 0x0);

	snd_soc_update_bits(codec, SUNXI_MIC2_MIC3_CTL,
			0x7 << MIC2BOOST,
			sunxi_codec->gain_cfg.mic2gain << MIC2BOOST);
	snd_soc_update_bits(codec, SUNXI_MIC2_MIC3_CTL,
			0x7 << MIC3BOOST,
			sunxi_codec->gain_cfg.mic3gain << MIC3BOOST);

	snd_soc_update_bits(codec, SUNXI_LINEOUT_CTL1,
			0x1f << LINEOUT_VOL,
			sunxi_codec->gain_cfg.lineout_vol << LINEOUT_VOL);

	snd_soc_update_bits(codec, SUNXI_ADC_CTL,
			0x1f << ADCG,
			sunxi_codec->gain_cfg.adcgain << ADCG);

	if (sunxi_codec->hw_cfg.adcdrc_cfg)
		adcdrc_config(codec);

	if (sunxi_codec->hw_cfg.adchpf_cfg)
		adchpf_config(codec);

	if (sunxi_codec->hw_cfg.dacdrc_cfg)
		dacdrc_config(codec);

	if (sunxi_codec->hw_cfg.dachpf_cfg)
		dachpf_config(codec);
}

static int sunxi_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int i = 0;
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	int ret = 0;
#endif

	switch (params_format(params)) {
	case	SNDRV_PCM_FORMAT_S16_LE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
				(3<<FIFO_MODE), (3<<FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
				(1<<TX_SAMPLE_BITS), (0<<TX_SAMPLE_BITS));
		} else {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
				(1<<RX_FIFO_MODE), (1<<RX_FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
				(1<<RX_SAMPLE_BITS), (0<<RX_SAMPLE_BITS));
		}
		break;
	case	SNDRV_PCM_FORMAT_S32_LE:
		/* only for the compatible of tinyalsa */
	case	SNDRV_PCM_FORMAT_S24_LE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
				(3<<FIFO_MODE), (0<<FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
				(1<<TX_SAMPLE_BITS), (1<<TX_SAMPLE_BITS));
		} else {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
				(1<<RX_FIFO_MODE), (0<<RX_FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
				(1<<RX_SAMPLE_BITS), (1<<RX_SAMPLE_BITS));
		}
		break;
	default:
		pr_err("[%s] params_format[%d] error!\n", __func__,
			params_format(params));
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(sample_rate_conv); i++) {
		if (sample_rate_conv[i].samplerate == params_rate(params)) {
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
					(0x7<<DAC_FS),
					(sample_rate_conv[i].rate_bit<<DAC_FS));
			} else {
				if (sample_rate_conv[i].samplerate > 48000)
					return -EINVAL;
				snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
					(0x7<<ADC_FS),
					(sample_rate_conv[i].rate_bit<<ADC_FS));
			}
		}
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (params_channels(params)) {
		case 1:
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
					(1<<DAC_MONO_EN), 1<<DAC_MONO_EN);
			break;
		case 2:
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
					(1<<DAC_MONO_EN), (0<<DAC_MONO_EN));
			break;
		default:
			pr_warn("[%s] cannot support the channels:%u.\n",
				__func__, params_channels(params));
			return -EINVAL;
		}
	} else {
		switch (params_channels(params)) {
		case 1:
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
					(0xf<<ADC_CHAN_SEL), (1<<ADC_CHAN_SEL));
			break;
		case 2:
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
					(0xf<<ADC_CHAN_SEL), (3<<ADC_CHAN_SEL));
			break;
		case 3:
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
					(0xf<<ADC_CHAN_SEL), (7<<ADC_CHAN_SEL));
			break;
		case 4:
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
					(0xf<<ADC_CHAN_SEL), (0xf<<ADC_CHAN_SEL));
			break;
		default:
			pr_warn("[%s] cannot support the channels:%u.\n",
				__func__, params_channels(params));
			return -EINVAL;
		}
	}

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	if (sunxi_codec->i2s_port == SUNXI_CODEC_MAP_PORT_NULL) {
		pr_warn("[%s] i2s_port: no use.\n", __func__);
		return 0;
	}

	/* daudio_init */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_warn("[%s] map_i2s not be used when playback.\n", __func__);
		return 0;
	}

	ret = sunxi_codec_map_i2s_set_fmt(dai, 0);
	if (ret < 0) {
		pr_err("[%s] sunxi_codec_map_i2s_set_fmt failed.\n", __func__);
		return ret;
	}
	ret = sunxi_codec_map_i2s_hw_params(substream, params, dai);
	if (ret < 0) {
		pr_err("[%s] sunxi_codec_map_i2s_hw_params failed.\n", __func__);
		return ret;
	}
	ret = sunxi_codec_map_i2s_dma_params(substream, params, dai);
	if (ret < 0) {
		pr_err("[%s] sunxi_codec_map_i2s_dma_params failed.\n", __func__);
		return ret;
	}
#endif

	return 0;
}

static int sunxi_codec_set_sysclk(struct snd_soc_dai *dai,
			int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (clk_set_rate(sunxi_codec->pllclk, freq)) {
		dev_err(sunxi_codec->dev, "set pllclk rate failed\n");
		return -EINVAL;
	}
	return 0;
}

static int sunxi_codec_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
	int ret = 0;
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (sunxi_codec->i2s_port == SUNXI_CODEC_MAP_PORT_NULL) {
		pr_warn("[%s] i2s_port: no use.\n", __func__);
		return 0;
	}

	if (div_id == CODEC_DIV_CAPTURE)
		ret = sunxi_codec_i2s_set_clkdiv(dai, div_id, div);
#endif
	return ret;
}

static int sunxi_codec_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (sunxi_codec->hw_cfg.dacdrc_cfg)
			dacdrc_enable(codec, 1);
		if (sunxi_codec->hw_cfg.dachpf_cfg)
			dachpf_enable(codec, 1);
	} else {
		if (sunxi_codec->hw_cfg.adcdrc_cfg)
			adcdrc_enable(codec, 1);

		if (sunxi_codec->hw_cfg.adchpf_cfg)
			adchpf_enable(codec, 1);
	}
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	sunxi_codec_map_i2s_startup(substream, dai);
#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_codec_map_i2s_mad_startup(substream, dai);
#endif
#endif
	return 0;
}

static void sunxi_codec_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (sunxi_codec->hw_cfg.dacdrc_cfg)
			dacdrc_enable(codec, 0);
		if (sunxi_codec->hw_cfg.dachpf_cfg)
			dachpf_enable(codec, 0);
	} else {
		if (sunxi_codec->hw_cfg.adcdrc_cfg)
			adcdrc_enable(codec, 0);

		if (sunxi_codec->hw_cfg.adchpf_cfg)
			adchpf_enable(codec, 0);
	}

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	sunxi_codec_map_i2s_shutdown(substream, dai);
#ifdef CONFIG_SND_SUNXI_MAD
	sunxi_codec_map_i2s_mad_shutdown(substream, dai);
#endif
#endif
}

static int sunxi_codec_digital_mute(struct snd_soc_dai *dai, int mute)
{
#if 0
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if ((mute != 0) && sunxi_codec->spk_cfg.used) {
		msleep(sunxi_codec->spk_cfg.pa_msleep_time);
	}
#endif
	return 0;
}

static int sunxi_codec_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
#ifdef CONFIG_SND_SUNXI_MAD
	struct sunxi_mad_priv *mad_priv = sunxi_codec->mad_priv;
#endif
#endif

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTL,
				(1<<FIFO_FLUSH), (1<<FIFO_FLUSH));
		snd_soc_write(codec, SUNXI_DAC_FIFO_STA,
			(1 << DAC_TXE_INT | 1 << DAC_TXU_INT | 1 << DAC_TXO_INT));
		snd_soc_write(codec, SUNXI_DAC_CNT, 0);
	} else {
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
#ifdef CONFIG_SND_SUNXI_MAD
		if (mad_priv->mad_bind && sunxi_codec->capture_en &&
			(substream->runtime->status->state != SNDRV_PCM_STATE_XRUN)) {
			snd_printd("don't need to flush fifo and clear count.\n");
			return 0;
		}
#endif
#endif

		snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTL,
			(1<<ADC_FIFO_FLUSH), (1<<ADC_FIFO_FLUSH));
		snd_soc_write(codec, SUNXI_ADC_FIFO_STA,
			(1 << ADC_RXA_INT | 1 << ADC_RXO_INT));
		snd_soc_write(codec, SUNXI_ADC_CNT, 0);
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
		sunxi_mad_codec_i2s_prepare(substream, dai);
#ifdef CONFIG_SND_SUNXI_MAD
		sunxi_codec->capture_en = 0;

		if (mad_priv->mad_bind) {
			sunxi_codec_mad_debug_control(sunxi_codec, false);
		}
#endif
#endif
	}

	return 0;
}

static int sunxi_codec_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	struct sunxi_i2s_params *sunxi_daudio = sunxi_codec->sunxi_daudio;
#ifdef CONFIG_SND_SUNXI_MAD
	struct sunxi_mad_priv *mad_priv = sunxi_codec->mad_priv;
#endif
#endif
	switch (cmd) {
	case	SNDRV_PCM_TRIGGER_START:
	case	SNDRV_PCM_TRIGGER_RESUME:
	case	SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(sunxi_codec->regmap,
					SUNXI_DAC_FIFO_CTL,
					(1<<DAC_DRQ_EN), (1<<DAC_DRQ_EN));
		else {
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
#ifdef CONFIG_SND_SUNXI_MAD
			if (mad_priv->mad_bind && sunxi_codec->capture_en) {
				pr_warn("====== setup mad dma type & enable ======");
				sunxi_mad_dma_type(SUNXI_MAD_DMA_IO);
				sunxi_mad_dma_enable(true);
				return 0;
			}
#endif
			sunxi_codec->buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
			sunxi_codec->period_bytes = snd_pcm_lib_period_bytes(substream);

			sunxi_codec_map_i2s_dma_trigger(sunxi_codec, true);

			sunxi_codec_map_i2s_txctrl_enable(sunxi_codec, true);

			if (sunxi_daudio != NULL) {
				pr_warn("====== setup i2s RXDRQ ======");
				regmap_update_bits(sunxi_daudio->regmap,
					SUNXI_DAUDIO_INTCTL,
					(1 << RXDRQEN), (1 << RXDRQEN));
			}
#endif
			regmap_update_bits(sunxi_codec->regmap,
					SUNXI_ADC_FIFO_CTL,
					(1 << ADC_DRQ_EN), (1 << ADC_DRQ_EN));
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
			sunxi_codec_map_i2s_global_enable(sunxi_codec, true);
#ifdef CONFIG_SND_SUNXI_MAD
			if (mad_priv->mad_bind) {
				sunxi_codec_map_i2s_mad_control(sunxi_codec, true);
				sunxi_codec->capture_en = 1;
			}
#endif
#endif
		}
		break;
	case	SNDRV_PCM_TRIGGER_STOP:
	case	SNDRV_PCM_TRIGGER_SUSPEND:
	case	SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(sunxi_codec->regmap,
					SUNXI_DAC_FIFO_CTL,
					(1 << DAC_DRQ_EN), (0 << DAC_DRQ_EN));
		else {
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
			sunxi_codec_map_i2s_global_enable(sunxi_codec, false);
			sunxi_codec_map_i2s_txctrl_enable(sunxi_codec, false);
			sunxi_codec_map_i2s_rxctrl_enable(sunxi_codec, false);

			sunxi_codec_map_i2s_dma_trigger(sunxi_codec, false);
#ifdef CONFIG_SND_SUNXI_MAD
			if (mad_priv->mad_bind) {
				sunxi_codec_map_i2s_mad_control(sunxi_codec, false);
				sunxi_codec->capture_en = 0;
			}
#endif
#endif
			regmap_update_bits(sunxi_codec->regmap,
					SUNXI_ADC_FIFO_CTL,
					(1 << ADC_DRQ_EN), (0 << ADC_DRQ_EN));
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dai_ops sunxi_codec_dai_ops = {
	.hw_params	= sunxi_codec_hw_params,
	.shutdown	= sunxi_codec_shutdown,
	.startup	= sunxi_codec_startup,
	.digital_mute	= sunxi_codec_digital_mute,
	.set_sysclk	= sunxi_codec_set_sysclk,
	.set_clkdiv	= sunxi_codec_set_clkdiv,
	.trigger	= sunxi_codec_trigger,
	.prepare	= sunxi_codec_prepare,
};

static struct snd_soc_dai_driver sunxi_codec_dai[] = {
	{
		.name	= "sun8iw18codec",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates	= SNDRV_PCM_RATE_8000_192000
				| SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = SNDRV_PCM_RATE_8000_48000
				| SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &sunxi_codec_dai_ops,
	},
};

static int sunxi_codec_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct snd_soc_dapm_context *dapm = &codec->component.dapm;
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	sunxi_codec->codec = codec;

	ret = snd_soc_add_codec_controls(codec, sunxi_codec_controls,
					ARRAY_SIZE(sunxi_codec_controls));
	if (ret)
		pr_err("failed to register codec controls!\n");

	snd_soc_dapm_new_controls(dapm, sunxi_codec_dapm_widgets,
			ARRAY_SIZE(sunxi_codec_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, sunxi_codec_dapm_routes,
			ARRAY_SIZE(sunxi_codec_dapm_routes));

	sunxi_codec_init(codec);
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
#ifdef CONFIG_SND_SUNXI_MAD
	INIT_WORK(&(sunxi_codec->ws_rx_mad_reset),
		sunxi_codec_map_i2s_rxctrl_stop_mad_reset);
	mutex_init(&(sunxi_codec->mad_clk_mutex));
#endif
#endif
	mutex_init(&(sunxi_codec->dac_mutex));
	mutex_init(&(sunxi_codec->adc_mutex));

	sunxi_codec->adc_enable = 0;
	sunxi_codec->dac_enable = 0;
	sunxi_codec->dac_dap_enable = 0;
	sunxi_codec->adc_dap_enable = 0;

	return 0;
}

static int sunxi_codec_remove(struct snd_soc_codec *codec)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	mutex_destroy(&(sunxi_codec->adc_mutex));
	mutex_destroy(&(sunxi_codec->dac_mutex));
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
#ifdef CONFIG_SND_SUNXI_MAD
	mutex_destroy(&(sunxi_codec->mad_clk_mutex));
	cancel_work_sync(&(sunxi_codec->ws_rx_mad_reset));
#endif
#endif

	return 0;
}

static int sunxi_gpio_iodisable(u32 gpio)
{
	char pin_name[8];
	u32 config, ret;

	sunxi_gpio_to_name(gpio, pin_name);
	config = 7 << 16;
	ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
	return ret;
}

static int save_audio_reg(struct sunxi_codec_info *sunxi_codec)
{
	int i = 0;
	int reg_group = 0;

	while (reg_labels[i].name != NULL) {
		if (reg_labels[i].address == SUNXI_HP_CTL)
			reg_group++;
		if (reg_group != 1) {
			reg_labels[i].value = readl(sunxi_codec->addr_dbase +
						reg_labels[i].address);
		} else if (reg_group == 1) {
			reg_labels[i].value = read_prcm_wvalue(
					reg_labels[i].address - SUNXI_PR_CFG,
					sunxi_codec->addr_abase);
		}
		i++;
	}

	return i;
}

static int echo_audio_reg(struct sunxi_codec_info *sunxi_codec)
{
	int i = 0;

	while (reg_labels[i].name != NULL) {
		snd_soc_write(sunxi_codec->codec, reg_labels[i].address,
				reg_labels[i].value);
		i++;
	}

	return i;
}

#ifdef CONFIG_SUNXI_AUDIO_DEBUG
void show_audio_all_reg(struct sunxi_codec_info *sunxi_codec)
{
	int i = 0;
	unsigned int reg_val;

	pr_warn("dump audio reg:\n");

	while (reg_labels[i].name != NULL) {
		reg_val = snd_soc_read(sunxi_codec->codec, reg_labels[i].address);
		pr_warn("%-20s[0x%03x]: 0x%-10x\n",
			reg_labels[i].name, reg_labels[i].address, reg_val);
		i++;
	}
}
#endif

static int sunxi_codec_suspend(struct snd_soc_codec *codec)
{
	struct sunxi_codec_info *sunxi_codec =
			snd_soc_codec_get_drvdata(codec);

	pr_debug("Enter %s\n", __func__);

	/* save the audio reg */
	save_audio_reg(sunxi_codec);

	if (sunxi_codec->pa_power_cfg.used) {
		gpio_set_value(sunxi_codec->pa_power_cfg.gpio, 0);
		sunxi_gpio_iodisable(sunxi_codec->pa_power_cfg.gpio);
	}
	if (sunxi_codec->spk_cfg.used) {
		gpio_set_value(sunxi_codec->spk_cfg.gpio,
			!(sunxi_codec->spk_cfg.pa_ctl_level));
		sunxi_gpio_iodisable(sunxi_codec->spk_cfg.gpio);
	}

	clk_disable_unprepare(sunxi_codec->moduleclk);
	clk_disable_unprepare(sunxi_codec->pllclkx4);
	clk_disable_unprepare(sunxi_codec->pllclk);

	pr_debug("End %s\n", __func__);

	return 0;
}

static int sunxi_codec_resume(struct snd_soc_codec *codec)
{
	struct sunxi_codec_info *sunxi_codec =
			snd_soc_codec_get_drvdata(codec);

	pr_debug("Enter %s\n", __func__);

	if (clk_prepare_enable(sunxi_codec->pllclk)) {
		dev_err(sunxi_codec->dev,
			"enable pllclk failed, resume exit\n");
		return -EBUSY;
	}

	if (clk_prepare_enable(sunxi_codec->pllclkx4)) {
		dev_err(sunxi_codec->dev,
			"enable pllclkx4 failed, resume exit\n");
		return -EBUSY;
	}

	if (clk_prepare_enable(sunxi_codec->moduleclk)) {
		dev_err(sunxi_codec->dev,
			"enable  moduleclk failed, resume exit\n");
		clk_disable_unprepare(sunxi_codec->pllclkx4);
		clk_disable_unprepare(sunxi_codec->pllclk);
		return -EBUSY;
	}
	/* for stable the power and clk */
	msleep(20);

	if (sunxi_codec->pa_power_cfg.used) {
		gpio_direction_output(sunxi_codec->pa_power_cfg.gpio, 1);
		gpio_set_value(sunxi_codec->pa_power_cfg.gpio, 1);
	}
	if (sunxi_codec->spk_cfg.used) {
		gpio_direction_output(sunxi_codec->spk_cfg.gpio, 1);
		gpio_set_value(sunxi_codec->spk_cfg.gpio,
				!sunxi_codec->spk_cfg.pa_ctl_level);
	}

	sunxi_codec_init(codec);
	/* echo the reg that had saved */
	echo_audio_reg(sunxi_codec);

	pr_debug("End %s\n", __func__);

	return 0;
}

static unsigned int sunxi_codec_read(struct snd_soc_codec *codec,
					unsigned int reg)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val;

	if (reg >= SUNXI_PR_CFG) {
		/* Analog part */
		reg = reg - SUNXI_PR_CFG;
		return read_prcm_wvalue(reg, sunxi_codec->addr_abase);
	} else {
		regmap_read(sunxi_codec->regmap, reg, &reg_val);
	}
	return reg_val;
}

static int sunxi_codec_write(struct snd_soc_codec *codec,
				unsigned int reg, unsigned int val)
{
	struct sunxi_codec_info *sunxi_codec = snd_soc_codec_get_drvdata(codec);

	if (reg >= SUNXI_PR_CFG) {
		/* Analog part */
		reg = reg - SUNXI_PR_CFG;
		write_prcm_wvalue(reg, val, sunxi_codec->addr_abase);
	} else {
		regmap_write(sunxi_codec->regmap, reg, val);
	}
	return 0;
};

static struct snd_soc_codec_driver soc_codec_dev_sunxi = {
	.probe = sunxi_codec_probe,
	.remove = sunxi_codec_remove,
	.suspend = sunxi_codec_suspend,
	.resume = sunxi_codec_resume,
	.read = sunxi_codec_read,
	.write = sunxi_codec_write,
	.ignore_pmdown_time = 1,
#if 0
	.controls = sunxi_codec_controls,
	.num_controls = ARRAY_SIZE(sunxi_codec_controls),
	.dapm_widgets = sunxi_codec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sunxi_codec_dapm_widgets),
	.dapm_routes = sunxi_codec_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(sunxi_codec_dapm_routes),
#endif
};

static ssize_t show_audio_reg(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct sunxi_codec_info *sunxi_codec = dev_get_drvdata(dev);
	int count = 0, i = 0;
	unsigned int reg_val;

	count += sprintf(buf, "dump audio reg:\n");

	while (reg_labels[i].name != NULL) {
		reg_val = snd_soc_read(sunxi_codec->codec, reg_labels[i].address);
		count += sprintf(buf + count, "%-20s[0x%03x]: 0x%-10x  Save:0x%x\n",
			reg_labels[i].name, reg_labels[i].address,
			reg_val, reg_labels[i].value);
		i++;
	}

	return count;
}

/* ex:
 * param 1: 0 read;1 write
 * param 2: 1 digital reg; 2 analog reg
 * param 3: reg value;
 * param 4: write value;
 *	read:
 *		echo 0,1,0x00> audio_reg
 *		echo 0,2,0x00> audio_reg
 *	write:
 *		echo 1,1,0x00,0xa > audio_reg
 *		echo 1,2,0x00,0xff > audio_reg
 */
static ssize_t store_audio_reg(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int rw_flag;
	int input_reg_val = 0;
	int input_reg_group = 0;
	int input_reg_offset = 0;
	struct sunxi_codec_info *sunxi_codec = dev_get_drvdata(dev);

	ret = sscanf(buf, "%d,%d,0x%x,0x%x", &rw_flag, &input_reg_group,
			&input_reg_offset, &input_reg_val);
	dev_info(dev, "ret:%d, reg_group:%d, reg_offset:%d, reg_val:0x%x\n",
			ret, input_reg_group, input_reg_offset, input_reg_val);

	if (!(input_reg_group == 1 || input_reg_group == 2)) {
		pr_err("not exist reg group\n");
		ret = count;
		goto out;
	}
	if (!(rw_flag == 1 || rw_flag == 0)) {
		pr_err("not rw_flag\n");
		ret = count;
		goto out;
	}
	if (input_reg_group == 1) {
		if (rw_flag) {
			regmap_write(sunxi_codec->regmap,
					input_reg_offset, input_reg_val);
		} else {
			regmap_read(sunxi_codec->regmap,
					input_reg_offset, &input_reg_val);
			dev_info(dev, "\n\n Reg[0x%x] : 0x%08x\n\n",
					input_reg_offset, input_reg_val);
		}
	} else if (input_reg_group == 2) {
		if (rw_flag) {
			write_prcm_wvalue(input_reg_offset,
			input_reg_val & 0xff, sunxi_codec->addr_abase);
		} else {
			 input_reg_val = read_prcm_wvalue(input_reg_offset,
					sunxi_codec->addr_abase);
			 dev_info(dev, "\n\n Reg[0x%02x] : 0x%02x\n\n",
					input_reg_offset, input_reg_val);
		}
	}
	ret = count;

out:
	return ret;
}

static DEVICE_ATTR(audio_reg, 0644, show_audio_reg, store_audio_reg);

static struct attribute *audio_debug_attrs[] = {
	&dev_attr_audio_reg.attr,
	NULL,
};

static struct attribute_group audio_debug_attr_group = {
	.name   = "audio_reg_debug",
	.attrs  = audio_debug_attrs,
};

static const struct regmap_config sunxi_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AC_VERSION,
	.cache_type = REGCACHE_NONE,
};

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
static const struct regmap_config sunxi_i2s_regmap_config[] = {
	{
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
		.max_register = 0x7c,
		.cache_type = REGCACHE_NONE,
	},
	{
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
		.max_register = 0x7c,
		.cache_type = REGCACHE_NONE,
	},
	{
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
		.max_register = 0x7c,
		.cache_type = REGCACHE_NONE,
	},
};
#endif

static int  sunxi_codec_dev_probe(struct platform_device *pdev)
{
	struct sunxi_codec_info *sunxi_codec;
	struct device_node *np = pdev->dev.of_node;
	struct gpio_config config_gpio;
	struct resource res;
	unsigned int temp_val;
	int ret;

	sunxi_codec = devm_kzalloc(&pdev->dev,
				sizeof(struct sunxi_codec_info), GFP_KERNEL);
	if (!sunxi_codec) {
		dev_err(&pdev->dev, "Can't allocate sunxi codec memory\n");
		ret = -ENOMEM;
		goto err_node_put;
	}
	dev_set_drvdata(&pdev->dev, sunxi_codec);
	sunxi_codec->dev = &pdev->dev;

	sunxi_codec->pllclk = of_clk_get(np, 0);
	if (IS_ERR_OR_NULL(sunxi_codec->pllclk)) {
		dev_err(&pdev->dev, "pllclk not exist or invaild\n");
		ret = PTR_ERR(sunxi_codec->pllclk);
		goto err_devm_kfree;
	}

	sunxi_codec->pllclkx4 = of_clk_get(np, 1);
	if (IS_ERR_OR_NULL(sunxi_codec->pllclkx4)) {
		dev_err(&pdev->dev, "pllclkx4 not exist or invaild\n");
		ret = PTR_ERR(sunxi_codec->pllclkx4);
		goto err_pllclk_put;
	}

	sunxi_codec->moduleclk = of_clk_get(np, 2);
	if (IS_ERR_OR_NULL(sunxi_codec->moduleclk)) {
		dev_err(&pdev->dev, "moduleclk not exist or invaild\n");
		ret = PTR_ERR(sunxi_codec->moduleclk);
		goto err_pllclk_put;
	} else {
		if (clk_set_parent(sunxi_codec->moduleclk,
				sunxi_codec->pllclkx4)) {
			dev_err(&pdev->dev,
				"set parent of moduleclk to pllclk failed\n");
			ret = -EBUSY;
			goto err_pllclkx4_put;
		}
	}
	if (clk_prepare_enable(sunxi_codec->pllclk)) {
		dev_err(&pdev->dev, "pllclk enable failed\n");
		ret = -EBUSY;
		goto err_moduleclk_put;
	}
	if (clk_prepare_enable(sunxi_codec->pllclkx4)) {
		dev_err(&pdev->dev, "pllclkx4 enable failed\n");
		ret = -EBUSY;
		goto err_pllclk_disable;
	}
	if (clk_prepare_enable(sunxi_codec->moduleclk)) {
		dev_err(&pdev->dev, "moduleclk enable failed\n");
		ret = -EBUSY;
		goto err_pllclkx4_disable;
	}

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	/* for i2s0 */
	sunxi_codec->daudio[0].i2s_clk = of_clk_get(np, 3);
	if (IS_ERR_OR_NULL(sunxi_codec->daudio[0].i2s_clk)) {
		dev_err(&pdev->dev, "i2s0 module clk not exist or invaild\n");
		ret = PTR_ERR(sunxi_codec->daudio[0].i2s_clk);
		goto err_moduleclk_disable;
	}
	if (clk_set_parent(sunxi_codec->daudio[0].i2s_clk,
				sunxi_codec->pllclkx4)) {
		dev_err(&pdev->dev,
				"set parent of i2s0 clk to pllclk failed\n");
		ret = -EBUSY;
		goto err_i2s0_clk_put;
	}
	/* for i2s1 */
	sunxi_codec->daudio[1].i2s_clk = of_clk_get(np, 4);
	if (IS_ERR_OR_NULL(sunxi_codec->daudio[1].i2s_clk)) {
		dev_err(&pdev->dev, "i2s1 module clk not exist or invaild\n");
		ret = PTR_ERR(sunxi_codec->daudio[1].i2s_clk);
		goto err_i2s0_clk_put;
	}
	if (clk_set_parent(sunxi_codec->daudio[1].i2s_clk,
				sunxi_codec->pllclkx4)) {
		dev_err(&pdev->dev,
				"set parent of i2s1 clk to pllclk failed\n");
		ret = -EBUSY;
		goto err_i2s1_clk_put;
	}

	/* for i2s2 */
	sunxi_codec->daudio[2].i2s_clk = of_clk_get(np, 5);
	if (IS_ERR_OR_NULL(sunxi_codec->daudio[2].i2s_clk)) {
		dev_err(&pdev->dev, "i2s2 module clk not exist or invaild\n");
		ret = PTR_ERR(sunxi_codec->daudio[2].i2s_clk);
		goto err_i2s1_clk_put;
	}
	if (clk_set_parent(sunxi_codec->daudio[2].i2s_clk,
				sunxi_codec->pllclkx4)) {
		dev_err(&pdev->dev,
				"set parent of i2s2 clk to pllclk failed\n");
		ret = -EBUSY;
		goto err_i2s2_clk_put;
	}
#endif
	/* codec reg_base */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "parse device node codec resource failed\n");
		ret = -EINVAL;
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
		goto err_i2s2_clk_put;
#else
		goto err_pllclkx4_disable;
#endif
	}
	memcpy(&(sunxi_codec->digital_res), &res, sizeof(struct resource));

	sunxi_codec->addr_dbase = of_iomap(np, 0);
	if (sunxi_codec->addr_dbase == NULL) {
		dev_err(&pdev->dev, "digital register iomap failed\n");
		ret = -EINVAL;
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
		goto err_i2s2_clk_put;
#else
		goto err_moduleclk_disable;
#endif
	}

	ret = of_address_to_resource(np, 1, &res);
	if (ret) {
		dev_err(&pdev->dev, "parse device node codec resource failed\n");
		ret = -EINVAL;
		goto err_dbase_iounmap;
	}
	memcpy(&(sunxi_codec->analog_res), &res, sizeof(struct resource));

	/* Analog register part, not using regmap */
	sunxi_codec->addr_abase = of_iomap(np, 1);
	if (sunxi_codec->addr_abase == NULL) {
		dev_err(&pdev->dev, "analog register iomap failed\n");
		ret = -EINVAL;
		goto err_dbase_iounmap;
	}

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	/* I2S0 reg_base */
	ret = of_address_to_resource(np, 2, &res);
	if (ret) {
		dev_err(&pdev->dev, "parse device node i2s0 resource failed\n");
		ret = -EINVAL;
		goto err_analog_iounmap;
	}
	memcpy(&(sunxi_codec->daudio[0].res), &res, sizeof(struct resource));

	sunxi_codec->daudio[0].base = of_iomap(np, 2);
	if (sunxi_codec->daudio[0].base == NULL) {
		dev_err(&pdev->dev, "i2s0 register iomap failed\n");
		ret = -EINVAL;
		goto err_analog_iounmap;
	}
	sunxi_codec->daudio[0].regmap = devm_regmap_init_mmio(&pdev->dev,
				sunxi_codec->daudio[0].base,
				&sunxi_i2s_regmap_config[0]);
	if (IS_ERR(sunxi_codec->daudio[0].regmap)) {
		dev_err(&pdev->dev, "daudio0 regmap init failed\n");
		ret = PTR_ERR(sunxi_codec->daudio[0].regmap);
		goto err_i2s0_base_iounmap;
	}

	/* I2S1 reg_base */
	ret = of_address_to_resource(np, 3, &res);
	if (ret) {
		dev_err(&pdev->dev, "parse device node i2s1 resource failed\n");
		ret = -EINVAL;
		goto err_i2s0_base_iounmap;
	}
	memcpy(&(sunxi_codec->daudio[1].res), &res, sizeof(struct resource));

	sunxi_codec->daudio[1].base = of_iomap(np, 3);
	if (sunxi_codec->daudio[1].base == NULL) {
		dev_err(&pdev->dev, "i2s1 register iomap failed\n");
		ret = -EINVAL;
		goto err_i2s0_base_iounmap;
	}
	sunxi_codec->daudio[1].regmap = devm_regmap_init_mmio(&pdev->dev,
				sunxi_codec->daudio[1].base,
				&sunxi_i2s_regmap_config[1]);
	if (IS_ERR(sunxi_codec->daudio[1].regmap)) {
		dev_err(&pdev->dev, "daudio1 regmap init failed\n");
		ret = PTR_ERR(sunxi_codec->daudio[1].regmap);
		goto err_i2s1_base_iounmap;
	}

	/* I2S2 reg_base */
	ret = of_address_to_resource(np, 4, &res);
	if (ret) {
		dev_err(&pdev->dev, "parse device node i2s2 resource failed\n");
		ret = -EINVAL;
		goto err_i2s1_base_iounmap;
	}
	memcpy(&(sunxi_codec->daudio[2].res), &res, sizeof(struct resource));

	sunxi_codec->daudio[2].base = of_iomap(np, 4);
	if (sunxi_codec->daudio[2].base == NULL) {
		dev_err(&pdev->dev, "i2s2 register iomap failed\n");
		ret = -EINVAL;
		goto err_i2s1_base_iounmap;
	}
	sunxi_codec->daudio[2].regmap = devm_regmap_init_mmio(&pdev->dev,
				sunxi_codec->daudio[2].base,
				&sunxi_i2s_regmap_config[2]);
	if (IS_ERR(sunxi_codec->daudio[2].regmap)) {
		dev_err(&pdev->dev, "daudio2 regmap init failed\n");
		ret = PTR_ERR(sunxi_codec->daudio[2].regmap);
		goto err_i2s2_base_iounmap;
	}
#endif

	sunxi_codec->regmap = devm_regmap_init_mmio(&pdev->dev,
				sunxi_codec->addr_dbase, &sunxi_codec_regmap_config);
	if (IS_ERR(sunxi_codec->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(sunxi_codec->regmap);
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
		goto err_i2s2_base_iounmap;
#else
		goto err_analog_iounmap;
#endif
	}

	ret = of_property_read_u32(np, "digital_vol", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "digital_vol get failed\n");
		sunxi_codec->gain_cfg.digital_vol = 0;
	} else {
		sunxi_codec->gain_cfg.digital_vol = temp_val;
	}

	ret = of_property_read_u32(np, "lineout_vol", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "lineout volume get failed\n");
		sunxi_codec->gain_cfg.lineout_vol = 0x1a;
	} else {
		sunxi_codec->gain_cfg.lineout_vol = temp_val;
	}

	ret = of_property_read_u32(np, "mic1gain", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "mic1 gain get failed\n");
		sunxi_codec->gain_cfg.mic1gain = 0x4;
	} else {
		sunxi_codec->gain_cfg.mic1gain = temp_val;
	}

	ret = of_property_read_u32(np, "mic2gain", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "mic2 gain get failed\n");
		sunxi_codec->gain_cfg.mic2gain = 0x4;
	} else {
		sunxi_codec->gain_cfg.mic2gain = temp_val;
	}

	ret = of_property_read_u32(np, "mic3gain", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "mic3 gain get failed\n");
		sunxi_codec->gain_cfg.mic3gain = 0x4;
	} else {
		sunxi_codec->gain_cfg.mic3gain = temp_val;
	}

	ret = of_property_read_u32(np, "adcgain", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "adc input gain get failed\n");
		sunxi_codec->gain_cfg.adcgain = 0x3;
	} else {
		sunxi_codec->gain_cfg.adcgain = temp_val;
	}

	ret = of_property_read_u32(np, "pa_msleep_time", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "pa_msleep_time get failed\n");
		sunxi_codec->spk_cfg.pa_msleep_time = 160;
	} else {
		sunxi_codec->spk_cfg.pa_msleep_time = temp_val;
	}

	ret = of_property_read_u32(np, "pa_ctl_level", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec] pa_clt_level config missing or invalid.\n");
		sunxi_codec->spk_cfg.pa_ctl_level = 1;
	} else {
		sunxi_codec->spk_cfg.pa_ctl_level = temp_val;
	}

	pr_debug("[codec init] digital_vol:%d, lineout_vol:%d, pa_msleep_time:%d\n",
		sunxi_codec->gain_cfg.digital_vol,
		sunxi_codec->gain_cfg.lineout_vol,
		sunxi_codec->spk_cfg.pa_msleep_time);
	pr_debug("[codec init] mic1gain:%d, mic2gain:%d, mic3gain:%d, adcgain:%d\n",
		sunxi_codec->gain_cfg.mic1gain,
		sunxi_codec->gain_cfg.mic2gain,
		sunxi_codec->gain_cfg.mic3gain,
		sunxi_codec->gain_cfg.adcgain
	);

	ret = of_property_read_u32(np, "adcdrc_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec] adcdrc_cfg config missing or invalid.\n");
		sunxi_codec->hw_cfg.adcdrc_cfg = 0;
	} else {
		sunxi_codec->hw_cfg.adcdrc_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "adchpf_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec] adchpf_cfg config missing or invalid.\n");
		sunxi_codec->hw_cfg.adchpf_cfg = 0;
	} else {
		sunxi_codec->hw_cfg.adchpf_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "dacdrc_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec] dacdrc_cfg config missing or invalid.\n");
		sunxi_codec->hw_cfg.dacdrc_cfg = 0;
	} else {
		sunxi_codec->hw_cfg.dacdrc_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "dachpf_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec] dachpf_cfg config missing or invalid.\n");
		sunxi_codec->hw_cfg.dachpf_cfg = 0;
	} else {
		sunxi_codec->hw_cfg.dachpf_cfg = temp_val;
	}

	sunxi_codec->spk_cfg.gpio = of_get_named_gpio_flags(np, "gpio-spk", 0,
				       (enum of_gpio_flags *)&config_gpio);
	if (gpio_is_valid(sunxi_codec->spk_cfg.gpio)) {
		sunxi_codec->spk_cfg.used = 1;
		ret = devm_gpio_request(&pdev->dev,
					sunxi_codec->spk_cfg.gpio, "SPK");
		if (ret) {
			dev_err(&pdev->dev, "failed to request gpio-spk gpio\n");
			ret = -EBUSY;
			goto err_analog_iounmap;
		} else {
			gpio_direction_output(sunxi_codec->spk_cfg.gpio, 1);
			gpio_set_value(sunxi_codec->spk_cfg.gpio,
				!(sunxi_codec->spk_cfg.pa_ctl_level));
		}
	} else {
		sunxi_codec->spk_cfg.used = 0;
		dev_err(&pdev->dev, "gpio-spk is invalid!\n");
	}

	sunxi_codec->pa_power_cfg.gpio =
				of_get_named_gpio_flags(np, "gpio-pa-power", 0,
					(enum of_gpio_flags *)&config_gpio);
	if (gpio_is_valid(sunxi_codec->pa_power_cfg.gpio)) {
		sunxi_codec->pa_power_cfg.used = 1;
		ret = devm_gpio_request(&pdev->dev,
				sunxi_codec->pa_power_cfg.gpio, "PA Power");
		if (ret) {
			dev_err(&pdev->dev, "failed to request gpio-pa-power\n");
			ret = -EBUSY;
			sunxi_codec->pa_power_cfg.used = 0;
			goto err_devm_gpio_free;
		} else {
			gpio_direction_output(sunxi_codec->pa_power_cfg.gpio, 1);
			gpio_set_value(sunxi_codec->pa_power_cfg.gpio, 0);
		}
	} else {
		sunxi_codec->pa_power_cfg.used = 0;
		pr_debug("gpio-pa-power is invalid!\n");
	}

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_sunxi,
				sunxi_codec_dai, ARRAY_SIZE(sunxi_codec_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "register codec failed\n");
		ret = -EBUSY;
		goto err_devm_gpio_free;
	}

	ret  = sysfs_create_group(&pdev->dev.kobj, &audio_debug_attr_group);
	if (ret < 0) {
		dev_warn(&pdev->dev, "failed to create attr group\n");
		goto err_sysfs_create;
	}

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	sunxi_codec->i2s_port = SUNXI_CODEC_MAP_PORT_NULL;
#endif
	return 0;

err_sysfs_create:
	sysfs_remove_group(&pdev->dev.kobj, &audio_debug_attr_group);
err_devm_gpio_free:
	if (sunxi_codec->pa_power_cfg.used)
		devm_gpio_free(&pdev->dev, sunxi_codec->pa_power_cfg.gpio);
	if (sunxi_codec->spk_cfg.used)
		devm_gpio_free(&pdev->dev, sunxi_codec->spk_cfg.gpio);
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
err_i2s2_base_iounmap:
	iounmap(sunxi_codec->daudio[2].base);
err_i2s1_base_iounmap:
	iounmap(sunxi_codec->daudio[1].base);
err_i2s0_base_iounmap:
	iounmap(sunxi_codec->daudio[0].base);
#endif
err_analog_iounmap:
	iounmap(sunxi_codec->addr_abase);
err_dbase_iounmap:
	iounmap(sunxi_codec->addr_dbase);
#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
err_i2s2_clk_put:
	clk_put(sunxi_codec->daudio[2].i2s_clk);
err_i2s1_clk_put:
	clk_put(sunxi_codec->daudio[1].i2s_clk);
err_i2s0_clk_put:
	clk_put(sunxi_codec->daudio[0].i2s_clk);
#endif
err_moduleclk_disable:
	clk_disable_unprepare(sunxi_codec->moduleclk);
err_pllclkx4_disable:
	clk_disable_unprepare(sunxi_codec->pllclkx4);
err_pllclk_disable:
	clk_disable_unprepare(sunxi_codec->pllclk);
err_moduleclk_put:
	clk_put(sunxi_codec->moduleclk);
err_pllclkx4_put:
	clk_put(sunxi_codec->pllclkx4);
err_pllclk_put:
	clk_put(sunxi_codec->pllclk);
err_devm_kfree:
	devm_kfree(&pdev->dev, sunxi_codec);
err_node_put:
	of_node_put(np);
	return ret;
}

static int  __exit sunxi_codec_dev_remove(struct platform_device *pdev)
{
	struct sunxi_codec_info *sunxi_codec = dev_get_drvdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;

	snd_soc_unregister_codec(&pdev->dev);

	iounmap(sunxi_codec->addr_abase);
	iounmap(sunxi_codec->addr_dbase);

#ifdef SUNXI_CODEC_MAP_TO_DAUDIO
	clk_put(sunxi_codec->daudio[0].i2s_clk);
	clk_put(sunxi_codec->daudio[1].i2s_clk);
	clk_put(sunxi_codec->daudio[2].i2s_clk);
#endif
	clk_disable_unprepare(sunxi_codec->moduleclk);
	clk_put(sunxi_codec->moduleclk);
	clk_disable_unprepare(sunxi_codec->pllclkx4);
	clk_put(sunxi_codec->pllclkx4);
	clk_disable_unprepare(sunxi_codec->pllclk);
	clk_put(sunxi_codec->pllclk);

	if (sunxi_codec->pa_power_cfg.used)
		devm_gpio_free(&pdev->dev, sunxi_codec->pa_power_cfg.gpio);
	if (sunxi_codec->spk_cfg.used)
		devm_gpio_free(&pdev->dev, sunxi_codec->spk_cfg.gpio);

	sysfs_remove_group(&pdev->dev.kobj, &audio_debug_attr_group);

	devm_kfree(&pdev->dev, sunxi_codec);
	of_node_put(np);

	return 0;
}

static const struct of_device_id sunxi_internal_codec_of_match[] = {
	{ .compatible = "allwinner,sunxi-internal-codec", },
	{},
};

static struct platform_driver sunxi_internal_codec_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_internal_codec_of_match,
	},
	.probe = sunxi_codec_dev_probe,
	.remove = __exit_p(sunxi_codec_dev_remove),
};

module_platform_driver(sunxi_internal_codec_driver);

MODULE_DESCRIPTION("SUNXI Codec ASoC driver");
MODULE_AUTHOR("wolfgang huang <huangjinhui@allwinnertech.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-internal-codec");
