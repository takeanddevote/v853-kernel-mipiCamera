/*
 * dspo_timing.h
 *
 * Copyright (c) 2007-2021 Allwinnertech Co., Ltd.
 * Author: zhengxiaobin <zhengxiaobin@allwinnertech.com>
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef _DSPO_TIMING_H
#define _DSPO_TIMING_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/dspo_drv.h>



/**
 * @name       :dspo_get_timing_num
 * @brief      :get the number of timing that is support
 * @param[IN]  :none
 * @param[OUT] :none
 * @return     :the number of timing that is support
 */
u32 dspo_get_timing_num(void);

/**
 * @name       :dspo_get_timing_info
 * @brief      :get timing info of specified mode
 * @param[IN]  :mode:output resolution
 * @param[OUT] :info:pointer that store the timing info
 * @return     :0 if success
 */
s32 dspo_get_timing_info(enum dspo_output_mode mode,
			 struct dspo_video_timings *p_info);

/**
 * @name       dspo_is_mode_support
 * @brief      if a specified mode is support
 * @param[IN]  mode:resolution output mode
 * @param[OUT] none
 * @return     1 if support, 0 if not support
 */
s32 dspo_is_mode_support(enum dspo_output_mode mode);

#endif /*End of file*/
