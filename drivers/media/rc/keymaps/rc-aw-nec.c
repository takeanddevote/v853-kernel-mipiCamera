/* tbs-nec.h - Keytable for tbs_nec Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2019 by allwinnertech
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table aw_nec[] = {
	{ 0x04fb1ae5, KEY_POWER},	/* power */
	{ 0x04fb23dc, KEY_MUTE},	/* mute */
	{ 0x04fb13ec, KEY_1},
	{ 0x04fb10ef, KEY_2},
	{ 0x04fb11ee, KEY_3},
	{ 0x04fb0ff0, KEY_4},
	{ 0x04fb0cf3, KEY_5},
	{ 0x04fb0df2, KEY_6},
	{ 0x04fb0bf4, KEY_7},
	{ 0x04fb08f7, KEY_8},
	{ 0x04fb09f6, KEY_9},
	{ 0x04fb47b8, KEY_0},
	{ 0x04fb45ba, KEY_VOLUMEUP},  /* vol+ */
	{ 0x04fb19e6, KEY_VOLUMEDOWN},/* vol- */
	{ 0x04fb5ca3, KEY_OK},		  /* ok */
	{ 0x04fb44bb, KEY_UP},
	{ 0x04fb1ce3, KEY_LEFT},
	{ 0x04fb4887, KEY_RIGHT},
	{ 0x04fb1de2, KEY_DOWN},
	{ 0x04fb5da2, KEY_MENU},
	{ 0x04fb1fe0, KEY_HOME},
	{ 0x04fb0af5, KEY_BACK},
	{ 0x04fb53ac, KEY_SETUP},
	{ 0x04fb4db2, KEY_TV},
	{ 0x04fb58a7, KEY_TV2},
	{ 0x04fb43bc, KEY_VOD},

};

static struct rc_map_list aw_nec_map = {
	.map = {
		.scan    = aw_nec,
		.size    = ARRAY_SIZE(aw_nec),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_AW_NEC,
	}
};

static int __init init_rc_map_aw_nec(void)
{
	return rc_map_register(&aw_nec_map);
}

static void __exit exit_rc_map_aw_nec(void)
{
	rc_map_unregister(&aw_nec_map);
}

module_init(init_rc_map_aw_nec)
module_exit(exit_rc_map_aw_nec)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("allwinnertech");
