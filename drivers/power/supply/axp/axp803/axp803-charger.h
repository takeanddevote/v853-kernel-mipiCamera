/*
 * drivers/power/axp/axp803/axp803-charger.h
 * (C) Copyright 2010-2016
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Pannan <pannan@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef AXP803_CHARGER_H
#define AXP803_CHARGER_H
#include "axp803.h"

#define AXP803_CHARGER_ENABLE            (1 << 7)
#define AXP803_IN_CHARGE                 (1 << 6)
#define AXP803_FAULT_LOG_COLD            (1 << 0)
#define AXP803_FAULT_LOG_CHA_CUR_LOW     (1 << 2)
#define AXP803_FAULT_LOG_BATINACT        (1 << 3)
#define AXP803_FAULT_LOG_OVER_TEMP       (1 << 7)
#define AXP803_FINISH_CHARGE             (1 << 2)
#define AXP803_COULOMB_ENABLE            (1 << 7)
#define AXP803_COULOMB_SUSPEND           (1 << 6)
#define AXP803_COULOMB_CLEAR             (1 << 5)

#define AXP803_ADC_BATVOL_ENABLE         (1 << 7)
#define AXP803_ADC_BATCUR_ENABLE         (1 << 6)
#define AXP803_ADC_DCINVOL_ENABLE        (1 << 5)
#define AXP803_ADC_DCINCUR_ENABLE        (1 << 4)
#define AXP803_ADC_USBVOL_ENABLE         (1 << 3)
#define AXP803_ADC_USBCUR_ENABLE         (1 << 2)
#define AXP803_ADC_APSVOL_ENABLE         (1 << 1)
#define AXP803_ADC_TSVOL_ENABLE          (1 << 0)
#define AXP803_ADC_INTERTEM_ENABLE       (1 << 7)
#define AXP803_ADC_GPIO0_ENABLE          (1 << 3)
#define AXP803_ADC_GPIO1_ENABLE          (1 << 2)
#define AXP803_ADC_GPIO2_ENABLE          (1 << 1)
#define AXP803_ADC_GPIO3_ENABLE          (1 << 0)

#endif /* AXP803_CHARGER_H */
