/*
 * vin.h for all v4l2 subdev manage
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 *	Yang Feng <yangfeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _VIN_H_
#define _VIN_H_

#include <media/media-device.h>
#include <media/media-entity.h>

#include "vin-video/vin_video.h"
#include "vin-video/vin_core.h"
#include "vin-csi/sunxi_csi.h"
#include "vin-isp/sunxi_isp.h"
#include "vin-vipp/sunxi_scaler.h"
#include "vin-mipi/sunxi_mipi.h"
#include "vin-tdm/vin_tdm.h"
#include "platform/platform_cfg.h"
#include "top_reg.h"

enum {
	VIN_TOP_CLK = 0,
	VIN_TOP_CLK_SRC,
	VIN_MAX_CLK,
};

enum {
	VIN_MIPI_CLK = 0,
	VIN_MIPI_CLK_SRC,
	VIN_MIPI_MAX_CLK,
};

enum {
	VIN_ISP_CLK = 0,
	VIN_ISP_CLK_SRC,
	VIN_ISP_MAX_CLK,
};

#ifdef CONFIG_ARCH_SUN8IW21P1
#define VIN_PLL_CSI_RATE (2376UL*1000*1000)
#endif
#define VIN_CLK_RATE (432*1000*1000)
#define ISP_CLK_RATE (300*1000*1000)

#define NO_VALID_SENSOR (-1)

struct vin_valid_sensor {
	struct v4l2_subdev *sd;
	char *name;
};

struct vin_csi_info {
	struct v4l2_subdev *sd;
	int id;
};

struct vin_mipi_info {
	struct v4l2_subdev *sd;
	int id;
};

struct vin_cci_info {
	struct v4l2_subdev *sd;
	int id;
};

struct vin_isp_info {
	struct v4l2_subdev *sd;
	int id;
};

struct vin_tdm_rx_info{
	struct v4l2_subdev *sd;
	int id;
};

struct vin_tdm_info {
	struct vin_tdm_rx_info tdm_rx[TDM_RX_NUM];
	int id;
};

struct vin_stat_info {
	struct v4l2_subdev *sd;
	int id;
};

struct vin_scaler_info {
	struct v4l2_subdev *sd;
	int id;
};

struct vin_clk_info {
	struct clk *clock;
	int use_count;
	unsigned long frequency;
};

struct vin_mclk_info {
	struct clk *mclk;
	struct clk *clk_24m;
	struct clk *clk_pll;
	struct pinctrl *pin;
	int use_count;
	unsigned long frequency;
};

enum vin_sub_device_regulator {
	ENUM_IOVDD,
	ENUM_AVDD,
	ENUM_DVDD,
	ENUM_AFVDD,
	ENUM_FLVDD,
	ENUM_CAMERAVDD,
	ENUM_MAX_REGU,
};

struct vin_power {
	struct regulator *pmic;
	int power_vol;
	char power_str[32];
};
struct sensor_instance {	/* 描述一个senor，信息来则sensor设备树节点 */
	char cam_name[I2C_NAME_SIZE];
	int cam_addr;		/* 摄像头地址，比如i2c地址 */
	int cam_type;		/* SENSOR_RAW / SENSOR_YUV */
	int is_isp_used;
	int is_bayer_raw;
	int vflip;
	int hflip;
	int act_addr;
	char act_name[I2C_NAME_SIZE];
	char isp_cfg_name[I2C_NAME_SIZE];
};

struct sensor_list {
	int use_sensor_list;
	int used;
	int csi_sel;			/* 该sensor/vinc使用了哪个csi，由 vinc@x 节点来建立sensorx -> csix -> vincx的映射关系 */
	int device_sel;
	int mclk_id;
	int sensor_bus_sel;		/* 选择了总线类型下的哪条总线号。vin.c就是通过这个来找到sensor的subdev的，很隐晦，但是耦合性极低 */
	int sensor_bus_type;	/* 总线类型，比如i2c还是spi等 */
	int act_bus_sel;
	int act_bus_type;
	int act_separate;
	int power_set;
	int detect_num;
	char sensor_pos[32];
	int valid_idx;
	struct vin_power power[ENUM_MAX_REGU];
	struct gpio_config gpio[MAX_GPIO_NUM];			/* 用于控制sensor的gpio引脚，如复位、上电等 */
	struct sensor_instance inst[MAX_DETECT_NUM];
};

enum module_type {
	VIN_MODULE_TYPE_I2C,
	VIN_MODULE_TYPE_CCI,
	VIN_MODULE_TYPE_SPI,
	VIN_MODULE_TYPE_GPIO,
	VIN_MODULE_TYPE_MAX,
};

struct vin_act_info {
	struct v4l2_subdev *sd;
	enum module_type type;
	int id;
};

struct vin_flash_info {
	struct v4l2_subdev *sd;
	enum module_type type;
	int id;
};

struct vin_sensor_info {
	struct v4l2_subdev *sd;
	enum module_type type;
	int id;
};

struct vin_module_info {
	struct vin_act_info act[MAX_DETECT_NUM];
	struct vin_flash_info flash;
	struct vin_sensor_info sensor[MAX_DETECT_NUM];
	int id;
};

struct modules_config {
	struct vin_module_info modules;
	struct sensor_list sensors;
	int flash_used;		/* 是否有闪光灯 */
	int act_used;		/* 摄像头是否有执行器，如控制镜头方向 */
};

struct vin_md {
	struct vin_csi_info csi[VIN_MAX_CSI];
	struct vin_mipi_info mipi[VIN_MAX_MIPI];
	struct vin_cci_info cci[VIN_MAX_CCI];
	struct vin_isp_info isp[VIN_MAX_ISP];
	struct vin_tdm_info tdm[VIN_MAX_TDM];
	struct vin_stat_info stat[VIN_MAX_ISP];
	struct vin_scaler_info scaler[VIN_MAX_SCALER];
	struct vin_core *vinc[VIN_MAX_DEV];
	struct vin_clk_info clk[VIN_MAX_CLK];
	struct vin_clk_info mipi_clk[VIN_MIPI_MAX_CLK];
	struct vin_mclk_info mclk[VIN_MAX_CCI];
	struct vin_clk_info isp_clk[VIN_ISP_MAX_CLK];
	struct modules_config modules[VIN_MAX_DEV];		/* 每个senor节点的配置，device_type指定的id作为下标 */
	struct csic_feature_list csic_fl;
	struct csic_version csic_ver;
	unsigned int isp_ver_major;
	unsigned int isp_ver_minor;
	unsigned int is_empty;
	unsigned int id;			/* 由设备树节点 device_id 指定 */
	unsigned int irq;
	int use_count;
	void __iomem *base;			/* 寄存器组ioremap基地址 */
	void __iomem *ccu_base;
	void __iomem *cmb_top_base;
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct platform_device *pdev;
	bool user_subdev_api;
	spinlock_t slock;
	bool sensor_power_on;
	bool clk_en;
};

static inline struct vin_md *entity_to_vin_mdev(struct media_entity *me)
{
	return me->graph_obj.mdev == NULL ? NULL :
		container_of(me->graph_obj.mdev, struct vin_md, media_dev);
}

/*
 * Media pipeline operations to be called from within the video
 * node when it is the last entity of the pipeline. Implemented
 * by corresponding media device driver.
 */

struct vin_pipeline_ops {
	int (*open)(struct vin_pipeline *p, struct media_entity *me,
			  bool resume);
	int (*close)(struct vin_pipeline *p);
	int (*set_stream)(struct vin_pipeline *p, int state);
};

#define vin_pipeline_call(f, op, p, args...)				\
	(((f)->pipeline_ops && (f)->pipeline_ops->op) ? \
			    (f)->pipeline_ops->op((p), ##args) : -ENOIOCTLCMD)


#endif /*_VIN_H_*/
