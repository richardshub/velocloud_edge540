/* velocloud.c
 * (c) Sandra Berndt (sberndt@velocloud.net)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <misc/velocloud.h>
// fix reboot style for some boards
#include <linux/reboot.h>

// build with force enabled;
#define VC_FIX_DMI

// per-board dmi matching info;
// unfortunatedly, the edge-portwell has identical dmi info to edge-nexcom;

#define VC_EDGE_ID(x) .driver_data = (void *)(x)

static struct dmi_system_id vc_dmi_table[] = {
	// 5*0 series uses DMI_BOARD_NAME to match
	{ .ident = "edge500",
	  { DMI_MATCH(DMI_BOARD_NAME, "EDGE500"),
	  },
	  VC_EDGE_ID(VC_EDGE500),
	},
	{ .ident = "edge520b",
	  { DMI_MATCH(DMI_BOARD_NAME, "EDGE520"),
	    DMI_MATCH(DMI_BOARD_VERSION, "2"),
	  },
	  VC_EDGE_ID(VC_EDGE520B),
	},
	{ .ident = "edge540b",
	  { DMI_MATCH(DMI_BOARD_NAME, "EDGE540"),
	    DMI_MATCH(DMI_BOARD_VERSION, "2"),
	  },
	  VC_EDGE_ID(VC_EDGE540B),
	},
	{ .ident = "edge520",
	  { DMI_MATCH(DMI_BOARD_NAME, "EDGE520"),
	  },
	  VC_EDGE_ID(VC_EDGE520),
	},
	{ .ident = "edge540",
	  { DMI_MATCH(DMI_BOARD_NAME, "EDGE540"),
	  },
	  VC_EDGE_ID(VC_EDGE540),
	},
	{ .ident = "edge510",
	  { DMI_MATCH(DMI_BOARD_NAME, "EDGE510"),
	  },
	  VC_EDGE_ID(VC_EDGE510),
	},
	// 8x0,1000 series uses DMI_PRODUCT_NAME to match
	{ .ident = "edge1000sm",
	  { DMI_MATCH(DMI_PRODUCT_NAME, "EDGE1000"),
	  },
	  VC_EDGE_ID(VC_EDGE1000SM),
	},
	{ .ident = "edge1000qat",
	  { DMI_MATCH(DMI_PRODUCT_NAME, "EDGE2000"), // Edge1000+qat
	  },
	  VC_EDGE_ID(VC_EDGE1000QAT),
	},
	{ .ident = "edge840sm",
	  { DMI_MATCH(DMI_PRODUCT_NAME, "EDGE840"),
	  },
	  VC_EDGE_ID(VC_EDGE800SM),
	},
	//
	// Fallback entries, for pre-production and experimental systems:
	//
	// Fallbacks for Supermicro systems:
	{ .ident = "edge1000sm",
	  { DMI_MATCH(DMI_BOARD_NAME, "X10SRW-F"), // SuperMicro
	  },
	  VC_EDGE_ID(VC_EDGE1000SM),
	},
	{ .ident = "edge840sm",
	  { DMI_MATCH(DMI_BOARD_NAME, "X10SDV-F"), // Supermicro 800
	  },
	  VC_EDGE_ID(VC_EDGE800SM),
	},
	{ .ident = "edge840sm",
	  { DMI_MATCH(DMI_BOARD_NAME, "X10SDV-TP8F"), // Supermicro 800
	  },
	  VC_EDGE_ID(VC_EDGE800SM),
	},
	// Fallback for Dolphin with raw ADI Coreboot:
	{ .ident = "edge510",
	  { DMI_MATCH(DMI_BOARD_NAME, "RCC-VE"),
	  },
	  VC_EDGE_ID(VC_EDGE510),
	},
	// Entries for obsolete/unused platforms:
	{ .ident = "edge-nexcom",
	  { DMI_MATCH(DMI_PRODUCT_UUID, "00020003-0004-0005-0006-000700080009"),
	  },
	  VC_EDGE_ID(VC_EDGE_NEXCOM),
	},
	{ .ident = "edge1000ifm",
	  { DMI_MATCH(DMI_BOARD_NAME, "S1600JP"), // Interface Masters
	  },
	  VC_EDGE_ID(VC_EDGE1000IFM),
	},
	{ 0 },
};

#ifdef VC_FIX_DMI

static char *board = "";

static const struct dmi_system_id *
vc_fix_board(const struct dmi_system_id *dmi, char *name)
{
	const char *id;

	for(; (id = dmi->ident); dmi++) {
		if( !strcmp(id, name))
			return(dmi);
	}
	return(NULL);
}

module_param(board,charp,S_IRUGO);
MODULE_PARM_DESC(board, "overwrite board name");

#endif // VC_FIX_DMI

// make up i2c-bb for smb0 firmware devices;

int vc_bbfw = 0;
module_param(vc_bbfw,int,S_IRUGO);
MODULE_PARM_DESC(vc_bbfw, "create i2c bitbang driver for smb0");
EXPORT_SYMBOL(vc_bbfw);

// exports to other vc modules;

const struct dmi_system_id *vc_dmi = NULL;
EXPORT_SYMBOL(vc_dmi);

static void
vc_set_reboot(void)
{
	// assert(vc_dmi != NULL);
	int id = (unsigned long)vc_dmi->driver_data;

	// See if reboot params have already been overridden
	if (!reboot_default) {
		pr_info("vc: reboot style already set to '%c'\n", reboot_type);
		return;
	}
	switch (id) {
		case VC_EDGE500:
		case VC_EDGE510: // Requires PIC v5
		case VC_EDGE520:
		case VC_EDGE540:
		case VC_EDGE520B:
		case VC_EDGE540B:
			reboot_type = BOOT_CF9;
			pr_info("vc: setting reboot style to PIC ('%c')\n", reboot_type);
			break;
		default:
			// No change
			break;
	}
}

// init vc dmi module;

static int __init
vc_dmi_init(void)
{
	// determine velocloud board;

	vc_dmi = dmi_first_match(vc_dmi_table);

	// allow kernel param overwrite;

#ifdef VC_FIX_DMI
	if(board[0]) {
		pr_info("vc: board overwrite '%s'\n", board);
		vc_dmi = vc_fix_board(vc_dmi_table, board);
	}
#endif // VC_FIX_DMI

	if( !vc_dmi) {
		pr_info("vc: no board found\n");
		return(-ENODEV);
	}
	pr_info("vc: found %s\n", vc_dmi->ident);

	// Set the reboot style
	vc_set_reboot();
	return(0);
}

// exit vc dmi module;

static void __exit
vc_dmi_exit(void)
{
	pr_info("vc: exit\n");
}

subsys_initcall(vc_dmi_init);
module_exit(vc_dmi_exit);

