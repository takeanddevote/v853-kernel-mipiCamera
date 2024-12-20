
/*
 * config.c for device tree and sensor list parser.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 * Yang Feng <yangfeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "config.h"
#include "../platform/platform_cfg.h"

#ifdef CONFIG_SENSOR_LIST_MODULE
static void set_used(struct sensor_list *sensors,
		void *value, int len)
{
	sensors->used = *(int *)value;
	vin_log(VIN_LOG_CONFIG, "sensors->used = %d!\n", sensors->used);
}
static void set_csi_sel(struct sensor_list *sensors,
		void *value, int len)
{
	sensors->csi_sel = *(int *)value;
}
static void set_device_sel(struct sensor_list *sensors,
		void *value, int len)
{
	sensors->device_sel = *(int *)value;
}
static void set_sensor_twi_id(struct sensor_list *sensors,
		void *value, int len)
{
	sensors->sensor_bus_sel = *(int *)value;
}
static void set_power_settings_en(struct sensor_list *sensors,
			       void *value, int len)
{
	sensors->power_set = *(int *)value;
}

static void set_cameravdd(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		strcpy(sensors->power[CAMERAVDD].power_str, (char *)value);
}
static void set_cameravdd_vol(struct sensor_list *sensors,
		void *value, int len)
{
	if (!sensors->power_set)
		sensors->power[CAMERAVDD].power_vol = *(int *)value;
}

static void set_iovdd(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		strcpy(sensors->power[IOVDD].power_str, (char *)value);
}
static void set_iovdd_vol(struct sensor_list *sensors,
		void *value, int len)
{
	if (!sensors->power_set)
		sensors->power[IOVDD].power_vol = *(int *)value;
}
static void set_avdd(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		strcpy(sensors->power[AVDD].power_str, (char *)value);
}
static void set_avdd_vol(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		sensors->power[AVDD].power_vol = *(int *)value;
}
static void set_dvdd(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		strcpy(sensors->power[DVDD].power_str, (char *)value);
}
static void set_dvdd_vol(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		sensors->power[DVDD].power_vol = *(int *)value;
}
static void set_afvdd(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		strcpy(sensors->power[AFVDD].power_str, (char *)value);
}
static void set_afvdd_vol(struct sensor_list *sensors,
		void *value, int len)
{
	if (sensors->power_set)
		sensors->power[AFVDD].power_vol = *(int *)value;
}
static void set_detect_sensor_num(struct sensor_list *sensors,
		void *value, int len)
{
	sensors->detect_num = *(int *)value;
}

static void set_sensor_name(struct sensor_list *sensors,
		void *value, int sel)
{
	strcpy(sensors->inst[sel].cam_name, (char *)value);
	vin_log(VIN_LOG_CONFIG, "sensor index %d, name is %s\n",
		sel, (char *)value);
}
static void set_sensor_twi_addr(struct sensor_list *sensors,
		void *value, int sel)
{
	sensors->inst[sel].cam_addr = *(int *)value;
	vin_log(VIN_LOG_CONFIG, "sensor index %d, addr is %d\n",
		sel, *(int *)value);
}
static void set_sensor_type(struct sensor_list *sensors,
		void *value, int sel)
{
	sensors->inst[sel].cam_type = *(int *)value;
}
static void set_sensor_hflip(struct sensor_list *sensors,
		void *value, int sel)
{
	sensors->inst[sel].vflip = *(int *)value;
}
static void set_sensor_vflip(struct sensor_list *sensors,
		void *value, int sel)
{
	sensors->inst[sel].hflip = *(int *)value;
}
static void set_act_name(struct sensor_list *sensors,
		void *value, int sel)
{
	strcpy(sensors->inst[sel].act_name, (char *)value);
}
static void set_act_twi_addr(struct sensor_list *sensors,
		void *value, int sel)
{
	sensors->inst[sel].act_addr = *(int *)value;
}
static void set_isp_cfg_name(struct sensor_list *sensors,
		void *value, int sel)
{
	strcpy(sensors->inst[sel].isp_cfg_name, (char *)value);
	vin_log(VIN_LOG_CONFIG, "isp_cfg_name: %s\n", (char *)value);
}

enum ini_item_type {
	INTEGER,
	STRING,
};

struct SensorParamAttribute {
	char *sub;
	int len;
	enum ini_item_type type;
	void (*set_param)(struct sensor_list *, void *, int len);
};

static struct SensorParamAttribute SensorParamCommon[] = {
	{"used", 1, INTEGER, set_used,},
	{"csi_sel", 1, INTEGER, set_csi_sel,},
	{"device_sel", 1, INTEGER, set_device_sel,},
	{"sensor_twi_id", 1, INTEGER, set_sensor_twi_id,},
	{"power_settings_enable", 1, INTEGER, set_power_settings_en,},
	{"cameravdd", 1, STRING, set_cameravdd,},
	{"cameravdd_vol", 1, INTEGER, set_cameravdd_vol,},
	{"iovdd", 1, STRING, set_iovdd,},
	{"iovdd_vol", 1, INTEGER, set_iovdd_vol,},
	{"avdd", 1, STRING, set_avdd,},
	{"avdd_vol", 1, INTEGER, set_avdd_vol,},
	{"dvdd", 1, STRING, set_dvdd,},
	{"dvdd_vol", 1, INTEGER, set_dvdd_vol,},
	{"afvdd", 1, STRING, set_afvdd,},
	{"afvdd_vol", 1, INTEGER, set_afvdd_vol,},
	{"detect_sensor_num", 1, INTEGER, set_detect_sensor_num,},
};
static struct SensorParamAttribute SensorParamDetect[] = {
	{"sensor_name", 1, STRING, set_sensor_name,},
	{"sensor_twi_addr", 1, INTEGER, set_sensor_twi_addr,},
	{"sensor_type", 1, INTEGER, set_sensor_type,},
	{"sensor_hflip", 1, INTEGER, set_sensor_hflip,},
	{"sensor_vflip", 1, INTEGER, set_sensor_vflip,},
	{"act_name", 1, STRING, set_act_name,},
	{"act_twi_addr", 1, INTEGER, set_act_twi_addr,},
	{"isp_cfg_name", 1, STRING, set_isp_cfg_name,},
};

static int __fetch_sensor_list(struct sensor_list *sl,
			char *main, struct cfg_section *sct,
			struct SensorParamAttribute *sp,
			int detect_id, int len)
{
	int i;
	struct cfg_subkey sk;
	char sub_name[128] = { 0 };

	for (i = 0; i < len; i++) {
		if (main == NULL || sp->sub == NULL) {
			vin_warn("sl main or sp->sub is NULL!\n");
			continue;
		}
		if (-1 == detect_id)
			sprintf(sub_name, "%s", sp->sub);
		else
			sprintf(sub_name, "%s%d", sp->sub, detect_id);

		if (sp->type == INTEGER) {
			if (CFG_ITEM_VALUE_TYPE_INT !=
			    cfg_get_one_subkey(sct, main, sub_name, &sk)) {
				vin_log(VIN_LOG_CONFIG,
					"Warn:%s->%s, apply default value!\n",
					main, sub_name);
			} else {
				if (sp->set_param) {
					sp->set_param(sl,
						(void *)&sk.value.val,
						detect_id);
					vin_log(VIN_LOG_CONFIG,
						"sensor list: %s->%s = %d\n",
						main, sub_name,
						sk.value.val);
				}
			}
		} else if (sp->type == STRING) {
			if (CFG_ITEM_VALUE_TYPE_STR !=
			    cfg_get_one_subkey(sct, main, sub_name, &sk)) {
				vin_log(VIN_LOG_CONFIG,
					"Warn:%s->%s, apply default value!\n",
					main, sub_name);
			} else {
				if (sp->set_param) {
					if (!strcmp(&sk.value.str[0], "\"\""))
						strcpy(&sk.value.str[0], "");
					sp->set_param(sl,
						(void *)&sk.value.str[0],
						detect_id);
					vin_log(VIN_LOG_CONFIG,
						"sensor list: %s->%s = %s\n",
						main, sub_name,
						&sk.value.str[0]);
				}
			}
		}
		sp++;
	}
	return 0;
}
static int fetch_sensor_list(struct sensor_list *sensors,
			char *main, struct cfg_section *section)
{
	int j, len;
	struct SensorParamAttribute *spc;
	static struct SensorParamAttribute *spd;

	spc = &SensorParamCommon[0];
	len = ARRAY_SIZE(SensorParamCommon);
	/* fetch sensor common config */
	vin_log(VIN_LOG_CONFIG, "fetch sensor common config!\n");
	__fetch_sensor_list(sensors, main, section, spc, -1, len);

	/* fetch sensor detect config */
	vin_log(VIN_LOG_CONFIG, "fetch sensor detect config!\n");
	if (sensors->detect_num > MAX_DETECT_NUM) {
		vin_warn("sensor_num = %d > MAX_DETECT_NUM = %d\n",
			sensors->detect_num, MAX_DETECT_NUM);
		sensors->detect_num = MAX_DETECT_NUM;
	}
	vin_log(VIN_LOG_CONFIG, "detect num is %d\n", sensors->detect_num);
	for (j = 0; j < sensors->detect_num; j++) {
		spd = &SensorParamDetect[0];
		len = ARRAY_SIZE(SensorParamDetect);
		__fetch_sensor_list(sensors, main, section,
				spd, j, len);
	}
	vin_log(VIN_LOG_CONFIG, "fetch sensors done!\n");
	return 0;
}

static int parse_sensor_list_info(struct sensor_list *sl, char *pos)
{
	int ret = 0;
	struct cfg_section *section;
	char sensor_list_cfg[128];

	if (strcmp(pos, "rear") && strcmp(pos, "REAR") && strcmp(pos, "FRONT")
	    && strcmp(pos, "front")) {
		vin_err("Camera position config ERR! POS = %s\n", pos);
		return -EINVAL;
	}

	/* sprintf(sensor_list_cfg, "/system/etc/hawkview/sensor_list_cfg.ini"); */
	sprintf(sensor_list_cfg, "/vendor/etc/hawkview/sensor_list_cfg.ini");
	vin_log(VIN_LOG_CONFIG, "Fetch %s sensor list form\"%s\"\n", pos, sensor_list_cfg);

	cfg_section_init(&section);
	ret = cfg_read_ini(sensor_list_cfg, &section);
	if (ret == -1) {
		cfg_section_release(&section);
		goto parse_sensor_list_info_end;
	}
	if (strcmp(pos, "rear") == 0 || strcmp(pos, "REAR") == 0)
		fetch_sensor_list(sl, "rear_camera_cfg", section);
	else
		fetch_sensor_list(sl, "front_camera_cfg", section);
	cfg_section_release(&section);

parse_sensor_list_info_end:
	vin_log(VIN_LOG_CONFIG, "fetch %s sensor list info end!\n", pos);
	return ret;
}
#else
static int parse_sensor_list_info(struct sensor_list *sl, char *pos)
{
	return 0;
}
#endif
static int get_value_int(struct device_node *np, const char *name,
			  u32 *value)
{
	int ret;

	ret = of_property_read_u32(np, name, value);
	if (ret) {
		*value = 0;
		vin_log(VIN_LOG_CONFIG, "fetch %s from device_tree failed\n", name);
		return -EINVAL;
	}
	vin_log(VIN_LOG_CONFIG, "%s = %x\n", name, *value);
	return 0;
}
static int get_value_string(struct device_node *np, const char *name,
			    char *string)
{
	int ret;
	const char *const_str;

	ret = of_property_read_string(np, name, &const_str);
	if (ret) {
		strcpy(string, "");
		vin_log(VIN_LOG_CONFIG, "fetch %s from device_tree failed\n", name);
		return -EINVAL;
	}
	strcpy(string, const_str);
	vin_log(VIN_LOG_CONFIG, "%s = %s\n", name, string);
	return 0;
}

static int get_gpio_info(struct device_node *np, const char *name,
			 struct gpio_config *gc)
{
	int gnum;

	gnum = of_get_named_gpio_flags(np, name, 0, (enum of_gpio_flags *)gc);
	if (!gpio_is_valid(gnum)) {
		gc->gpio = GPIO_INDEX_INVALID;
		vin_log(VIN_LOG_CONFIG, "fetch %s from device_tree failed\n", name);
		return -EINVAL;
	}
	vin_log(VIN_LOG_CONFIG,
		"%s: pin=%d  mul-sel=%d  drive=%d  pull=%d  data=%d gnum=%d\n",
		name, gc->gpio, gc->mul_sel, gc->drv_level, gc->pull, gc->data,
		gnum);
	return 0;
}

static int get_mname(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_string(np, name, sc->inst[0].cam_name);
}
static int get_twi_addr(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->inst[0].cam_addr);
}

static int get_twi_cci_spi(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->sensor_bus_type);
}

static int get_twi_id(struct device_node *np, const char *name,
		      struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->sensor_bus_sel);
}
static int get_mclk_id(struct device_node *np, const char *name,
		      struct sensor_list *sc)
{
	if (get_value_int(np, name, &sc->mclk_id))
		sc->mclk_id = -1;
	return 0;
}
static int get_pos(struct device_node *np, const char *name,
		   struct sensor_list *sc)
{
	return get_value_string(np, name, sc->sensor_pos);
}
static int get_isp_used(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->inst[0].is_isp_used);
}
static int get_fmt(struct device_node *np, const char *name,
		   struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->inst[0].is_bayer_raw);
}
static int get_vflip(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->inst[0].vflip);
}
static int get_hflip(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->inst[0].hflip);
}
static int get_cameravdd(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_string(np, name, sc->power[CAMERAVDD].power_str);
}
static int get_cameravdd_vol(struct device_node *np, const char *name,
			 struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->power[CAMERAVDD].power_vol);
}
static int get_iovdd(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_string(np, name, sc->power[IOVDD].power_str);
}
static int get_iovdd_vol(struct device_node *np, const char *name,
			 struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->power[IOVDD].power_vol);
}
static int get_avdd(struct device_node *np, const char *name,
		    struct sensor_list *sc)
{
	return get_value_string(np, name, sc->power[AVDD].power_str);
}
static int get_avdd_vol(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->power[AVDD].power_vol);
}
static int get_dvdd(struct device_node *np, const char *name,
		    struct sensor_list *sc)
{
	return get_value_string(np, name, sc->power[DVDD].power_str);
}
static int get_dvdd_vol(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->power[DVDD].power_vol);
}

#ifdef CONFIG_ACTUATOR_MODULE
static int get_afvdd(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_string(np, name, sc->power[AFVDD].power_str);
}
static int get_afvdd_vol(struct device_node *np, const char *name,
			 struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->power[AFVDD].power_vol);
}
#endif

static int get_power_en(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[POWER_EN]);
}
static int get_reset(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[RESET]);
}
static int get_pwdn(struct device_node *np, const char *name,
		    struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[PWDN]);
}
static int get_sm_hs(struct device_node *np, const char *name,
		    struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[SM_HS]);
}
static int get_sm_vs(struct device_node *np, const char *name,
		    struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[SM_VS]);
}

#ifdef CONFIG_FLASH_MODULE
static int get_flash_en(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[FLASH_EN]);
}
static int get_flash_mode(struct device_node *np, const char *name,
			  struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[FLASH_MODE]);
}
static int get_flvdd(struct device_node *np, const char *name,
		     struct sensor_list *sc)
{
	return get_value_string(np, name, sc->power[FLVDD].power_str);
}
static int get_flvdd_vol(struct device_node *np, const char *name,
			 struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->power[FLVDD].power_vol);
}
#endif

#ifdef CONFIG_ACTUATOR_MODULE
static int get_act_bus_type(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->act_bus_type);
}

static int get_act_bus_sel(struct device_node *np, const char *name,
		      struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->act_bus_sel);
}

static int get_act_separate(struct device_node *np, const char *name,
		      struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->act_separate);
}

static int get_af_pwdn(struct device_node *np, const char *name,
		       struct sensor_list *sc)
{
	return get_gpio_info(np, name, &sc->gpio[AF_PWDN]);
}
static int get_act_name(struct device_node *np, const char *name,
			struct sensor_list *sc)
{
	return get_value_string(np, name, sc->inst[0].act_name);
}
static int get_act_slave(struct device_node *np, const char *name,
			 struct sensor_list *sc)
{
	return get_value_int(np, name, &sc->inst[0].act_addr);
}
#endif

struct FetchFunArr {
	char *sub;
	int flag;
	/* 接口作用是从设备树节点中，获取xxx属性的值，并设置到sensor_list对应的成员中 */
	int (*fun)(struct device_node *, const char *, struct sensor_list *);
};
/* 目的是从sensor@x 设备树节点中，获取属性值，并填充 sensor_list 结构体 */
static struct FetchFunArr fetch_camera[] = {
	{"mname", 0, get_mname,},				/* 模块名字 */
	{"twi_addr", 0, get_twi_addr,},			/* 设备地址 */
	{"twi_cci_spi", 1, get_twi_cci_spi,},	/* 通信总线类型，spi 或 i2c */
	{"twi_cci_id", 1, get_twi_id,},			/* 总线类型下的总线号 */
	{"mclk_id", 1, get_mclk_id,},
	{"pos", 1, get_pos,},					/* sensor的物理位置，前方、后方等 */
	{"isp_used", 1, get_isp_used,},
	{"fmt", 1, get_fmt,},					/* 像素输出格式？ */
	{"vflip", 1, get_vflip,},				/* 垂直翻转 */
	{"hflip", 1, get_hflip,},
	{"cameravdd", 1, get_cameravdd,},		/*  */
	{"cameravdd_vol", 1, get_cameravdd_vol},
	{"iovdd", 1, get_iovdd,},
	{"iovdd_vol", 1, get_iovdd_vol},		/* iovdd的电压1.8 / 3.3 */
	{"avdd", 1, get_avdd,},
	{"avdd_vol", 1, get_avdd_vol,},
	{"dvdd", 1, get_dvdd,},
	{"dvdd_vol", 1, get_dvdd_vol,},
	{"power_en", 1, get_power_en,},		/* gpio引脚编号 */
	{"reset", 1, get_reset,},
	{"pwdn", 1, get_pwdn,},
	{"sm_hs", 1, get_sm_hs,},
	{"sm_vs", 1, get_sm_vs,},
};

#ifdef CONFIG_FLASH_MODULE
static struct FetchFunArr fetch_flash[] = {
	{"en", 1, get_flash_en,},
	{"mode", 1, get_flash_mode,},
	{"flvdd", 1, get_flvdd,},
	{"flvdd_vol", 1, get_flvdd_vol,},
};
#endif

#ifdef CONFIG_ACTUATOR_MODULE
static struct FetchFunArr fetch_actuator[] = {
	{"name", 0, get_act_name,},
	{"slave", 0, get_act_slave,},
	{"separate", 1, get_act_separate,},
	{"twi_cci_spi", 1, get_act_bus_type,},
	{"twi_cci_id", 1, get_act_bus_sel,},
	{"af_pwdn", 1, get_af_pwdn,},
	{"afvdd", 1, get_afvdd,},
	{"afvdd_vol", 1, get_afvdd_vol,},
};
#endif

/* 解析所有sensor节点和vinc节点。并填充 vin_md.modules_config */
int parse_modules_from_device_tree(struct vin_md *vind)
{
#ifdef FPGA_VER
	unsigned int i, j;
	struct modules_config *module;
	struct sensor_list *sensors;
	struct sensor_instance *inst;
	unsigned int sensor_uses = 2; /*1/2 mean use one/two sensor*/
	struct sensor_list sensors_def[2] = {
		{
		.used = 1,
		.csi_sel = 0,
		.device_sel = 0,
		.sensor_bus_sel = 0,
		.power_set = 1,
		.detect_num = 1,
		.sensor_pos = "rear",
		.power = {
			  [IOVDD] = {NULL, 2800000, ""},
			  [AVDD] = {NULL, 2800000, ""},
			  [DVDD] = {NULL, 1500000, ""},
			  [AFVDD] = {NULL, 2800000, ""},
			  [FLVDD] = {NULL, 3300000, ""},
			  },
		.gpio = {
			 [RESET] = {GPIOE(14), 1, 0, 1, 0,},
			 [PWDN] = {GPIOE(15), 1, 0, 1, 0,},
			 [POWER_EN] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 [FLASH_EN] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 [FLASH_MODE] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 [AF_PWDN] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 },
		.inst = {
				[0] = {
				       .cam_name = "ar0238",
				       .cam_addr = 0x20,
				       .cam_type = 0,
				       .is_isp_used = 1,
				       .is_bayer_raw = 1,
				       .vflip = 0,
				       .hflip = 0,
				       .act_name = "ad5820_act",
				       .act_addr = 0x18,
				       .isp_cfg_name = "",
				       },
			},
		}, {
		.used = 1,
		.csi_sel = 1,
		.device_sel = 1,
		.sensor_bus_sel = 1,
		.power_set = 1,
		.detect_num = 1,
		.sensor_pos = "front",
		.power = {
			  [IOVDD] = {NULL, 2800000, ""},
			  [AVDD] = {NULL, 2800000, ""},
			  [DVDD] = {NULL, 1500000, ""},
			  [AFVDD] = {NULL, 2800000, ""},
			  [FLVDD] = {NULL, 3300000, ""},
			  },
		.gpio = {
			 [RESET] = {GPIOE(14), 1, 0, 1, 0,},
			 [PWDN] = {GPIOE(15), 1, 0, 1, 0,},
			 [POWER_EN] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 [FLASH_EN] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 [FLASH_MODE] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 [AF_PWDN] = {GPIO_INDEX_INVALID, 0, 0, 0, 0,},
			 },
		.inst = {
				[0] = {
				       .cam_name = "ar0238_2",
				       .cam_addr = 0x20,
				       .cam_type = 0,
				       .is_isp_used = 1,
				       .is_bayer_raw = 1,
				       .vflip = 0,
				       .hflip = 0,
				       .act_name = "ad5820_act",
				       .act_addr = 0x18,
				       .isp_cfg_name = "",
				       },
			},
		}
	};

	for (i = 0; i < sensor_uses; i++) {
		module = &vind->modules[i];
		sensors = &module->sensors;
		inst = &sensors->inst[0];

		sensors->use_sensor_list = 0;
		sensors->sensor_bus_sel = sensors_def[i].sensor_bus_sel;
		/*when insmod without parm*/
		if (inst->cam_addr == 0xff) {
			strcpy(inst->cam_name, sensors_def[i].inst[0].cam_name);
			strcpy(inst->isp_cfg_name, sensors_def[i].inst[0].cam_name);
			inst->cam_addr = sensors_def[i].inst[0].cam_addr;
		}
		inst->is_isp_used = sensors_def[i].inst[0].is_isp_used;
		inst->is_bayer_raw = sensors_def[i].inst[0].is_bayer_raw;
		inst->vflip = sensors_def[i].inst[0].vflip;
		inst->hflip = sensors_def[i].inst[0].hflip;
		for (j = 0; j < MAX_POW_NUM; j++) {
			strcpy(sensors->power[j].power_str,
			       sensors_def[i].power[j].power_str);
			sensors->power[j].power_vol = sensors_def[i].power[j].power_vol;
		}
		module->flash_used = 0;
		module->act_used = 0;
		/*when insmod without parm*/
		if (inst->act_addr == 0xff) {
			strcpy(inst->act_name, sensors_def[i].inst[0].act_name);
			inst->act_addr = sensors_def[i].inst[0].act_addr;
		}

		for (j = 0; j < MAX_GPIO_NUM; j++) {
			sensors->gpio[j].gpio = sensors_def[i].gpio[j].gpio;
			sensors->gpio[j].mul_sel = sensors_def[i].gpio[j].mul_sel;
			sensors->gpio[j].pull = sensors_def[i].gpio[j].pull;
			sensors->gpio[j].drv_level = sensors_def[i].gpio[j].drv_level;
			sensors->gpio[j].data = sensors_def[i].gpio[j].data;
		}

		sensors->detect_num = sensors_def[i].detect_num;
		vin_log(VIN_LOG_CONFIG, "vin cci_sel is %d\n", sensors->sensor_bus_sel);
	}
#else
	int i = 0, j = 0;
	struct device_node *parent = vind->pdev->dev.of_node;
	struct device_node *cam = NULL, *child;
	char property[32] = { 0 };
	struct modules_config *module = NULL;
	struct sensor_list *sensors = NULL;
#ifdef CONFIG_ACTUATOR_MODULE
	int size = 0;
	const __be32 *list;
#endif

	for_each_available_child_of_node(parent, child) { /* 处理子设备名字为 sensor 的节点 */
		if (!strcmp(child->name, "sensor")) {	
			cam = child;
			sscanf(cam->type, "sensor%d", &i);	/* device_type 字符串属性中，获取sensor的id，这个id用来索引 modules */
			vin_log(VIN_LOG_CONFIG, "get sensor%d config for device tree\n", i);
			module = &vind->modules[i];
			sensors = &module->sensors;
		} else {
			continue;
		}

		/*when insmod without parm*/
		if (!strcmp(sensors->inst[0].cam_name, "")) {
			fetch_camera[0].flag = 1;
			fetch_camera[1].flag = 1;
		}
		/* 解析设备树节点，并填充 sensor_list 实例 */
		for (j = 0; j < ARRAY_SIZE(fetch_camera); j++) {	/* 遍历 fetch_camera 数组 */
			if (!fetch_camera[j].flag)	/* 处理flag不为0的元素 */
				continue;

			sprintf(property, "%s_%s", cam->type, fetch_camera[j].sub); /* 组成类似 sensor0_twi_cci_spi 字符串，这个是设备树sensorx节点的属性值 */
			fetch_camera[j].fun(cam, property, sensors);	
			/* 调用对应函数 get_twi_cci_spi 读取of节点的 sensor0_twi_cci_spi 属性的值，这是bus类型，即是i2c还是spi接口控制sensor */
		}

#ifdef CONFIG_ACTUATOR_MODULE
		/*get actuator node */
		sprintf(property, "%s", "act_handle");
		list = of_get_property(cam, property, &size);
		if ((!list) || (size == 0)) {
			vin_log(VIN_LOG_CONFIG, "missing %s property in node %s\n",
				property, cam->name);
			module->act_used = 0;
		} else {
			struct device_node *act = of_find_node_by_phandle(be32_to_cpup(list));
			if (!act) {
				vin_warn("%s invalid phandle\n", property);
			} else if (of_device_is_available(act)) {
				module->act_used = 1;
				/*when insmod without parm*/
				if (!strcmp(sensors->inst[0].act_name, "")) {
					fetch_actuator[0].flag = 1;
					fetch_actuator[1].flag = 1;
				}
				for (j = 0; j < ARRAY_SIZE(fetch_actuator); j++) {
					if (!fetch_actuator[j].flag)
						continue;
					sprintf(property, "%s_%s", act->type,
						fetch_actuator[j].sub);
					fetch_actuator[j].fun(act,
							property, sensors);
				}
			}
		}

		if (!sensors->act_separate) {
			sensors->act_bus_sel = sensors->sensor_bus_sel;
			sensors->act_bus_type = sensors->sensor_bus_type;
		}
#else
		module->act_used = 0;
#endif

#ifdef CONFIG_FLASH_MODULE
		/*get flash node */
		sprintf(property, "%s", "flash_handle");
		list = of_get_property(cam, property, &size);
		if ((!list) || (size == 0)) {
			vin_log(VIN_LOG_CONFIG, "missing %s property in node %s\n",
				property, cam->name);
			module->flash_used = 0;
		} else {
			struct device_node *flash = of_find_node_by_phandle(be32_to_cpup(list));
			if (!flash) {
				vin_warn("%s invalid phandle\n", property);
			} else if (of_device_is_available(flash)) {
				module->flash_used = 1;
				for (j = 0; j < ARRAY_SIZE(fetch_flash); j++) {
					if (!fetch_flash[j].flag)
						continue;

					sprintf(property, "%s_%s", flash->type,
						fetch_flash[j].sub);
					fetch_flash[j].fun(flash,
							property, sensors);
				}
				get_value_int(flash, "device_id",
						&module->modules.flash.id);
			}
		}
#else
		module->flash_used = 0;
#endif
		sensors->detect_num = 1;
	}

	for_each_available_child_of_node(parent, child) {			/* 处理名字为 vinc 的节点 */
		if (strcmp(child->name, "vinc"))
			continue;

		sprintf(property, "%s_rear_sensor_sel", child->type);		/* 获取 vincx__rear_sensor_sel 属性，即sensor的id */
		if (get_value_int(child, property, &i))
			i = 0;
		module = &vind->modules[i];	/* 获取sensor id对应的 modules_config */
		sensors = &module->sensors;
		sprintf(property, "%s_csi_sel", child->type);	
		get_value_int(child, property, &sensors->csi_sel);	/* 获取 vincx_csi_sel 属性，即vinc连接哪个csi */

		if (sensors->use_sensor_list == 0xff) {
			sprintf(property, "%s_sensor_list", child->type);
			get_value_int(child, property, &sensors->use_sensor_list);
		}
		if (sensors->use_sensor_list == 1) {
			if (!strcmp(sensors->sensor_pos, ""))
				strcpy(sensors->sensor_pos, "rear");
			parse_sensor_list_info(sensors, sensors->sensor_pos);
		}

		sprintf(property, "%s_front_sensor_sel", child->type);
		if (get_value_int(child, property, &i))
			i = 1;
		module = &vind->modules[i];
		sensors = &module->sensors;
		sprintf(property, "%s_csi_sel", child->type);
		get_value_int(child, property, &sensors->csi_sel);

		if (sensors->use_sensor_list == 0xff) {
			sprintf(property, "%s_sensor_list", child->type);
			get_value_int(child, property, &sensors->use_sensor_list);
		}
		if (sensors->use_sensor_list == 1) {
			if (!strcmp(sensors->sensor_pos, ""))
				strcpy(sensors->sensor_pos, "front");
			parse_sensor_list_info(sensors, sensors->sensor_pos);
		}
	}
#endif
	return 0;
}

