/* Velocloud Intel IGB Ethernet driver
 * Copyright(c) 2014 Velocloud Inc.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/if_ether.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/platform_data/mdio-gpio.h>
#include "e1000_mac.h"
#include "e1000_82575.h"
#include "e1000_hw.h"
#include "igb.h"

#include <linux/phy.h>
#include <linux/platform_device.h>
#include <net/dsa.h>

//#define IGB_VC_DEBUG
#undef IGB_VC_DEBUG
#ifdef IGB_VC_DEBUG
#define vcdbg(fmt, ...) printk("%s/%d " fmt, __func__, hw->bus.func, ##__VA_ARGS__);
#else
#define vcdbg(fmt, ...)
#endif

// dump switch/phy regs;

#undef IGB_VC_DUMP_6320
#undef IGB_VC_DUMP_6176
#undef IGB_VC_DUMP_1112
#undef IGB_VC_DUMP_1514
#undef IGB_VC_DUMP_ATU
#undef IGB_VC_DUMP_XLINK

#undef IGB_VC_DUMP_DSA_REG
#ifdef IGB_VC_DUMP_DSA_REG
#define vcdsareg(fmt, ...) printk("%s:" fmt, __func__, ##__VA_ARGS__);
#else
#define vcdsareg(fmt, ...)
#endif

#undef IGB_VC_DUMP_DSA_PHY
#ifdef IGB_VC_DUMP_DSA_PHY
#define vcdsaphy(fmt, ...) printk("%s:" fmt, __func__, ##__VA_ARGS__);
#else
#define vcdsaphy(fmt, ...)
#endif

// number of i354 rangeley bus functions;

#define IGB_VC_N_BUS_FUNC 4

// functions;

extern bool igb_sgmii_active_82575(struct e1000_hw *hw);
extern bool igb_sgmii_uses_mdio_82575(struct e1000_hw *hw);
extern s32  igb_read_phy_reg_82580(struct e1000_hw *, u32, u16 *);
extern s32  igb_write_phy_reg_82580(struct e1000_hw *, u32, u16);
extern s32 igb_init_phy_params_82575(struct e1000_hw *hw);
extern s32 igb_setup_copper_link_82575(struct e1000_hw *hw);
extern s32 igb_setup_serdes_link_82575(struct e1000_hw *hw);
extern s32 igb_get_pcs_speed_and_duplex_82575(struct e1000_hw *, u16 *, u16 *);
extern s32 igb_check_polarity_m88(struct e1000_hw *hw);
extern s32 igb_get_cable_length_m88_gen2(struct e1000_hw *hw);
extern s32 igb_check_for_link_82575(struct e1000_hw *hw);
extern s32 igb_phy_hw_reset_sgmii_82575(struct e1000_hw *hw);

s32 igb_vc_i2c_init(struct igb_adapter *adapter);
void igb_vc_i2c_exit(struct igb_adapter *adapter);

// mdio bus exported by platform driver;

extern struct mii_bus *vc_mdio_bus;

// MDIO timeout;

#define IGB_VC_MDIO_READY_TO 20		// ready timeout in 1msec steps;
#define IGB_VC_MDIO_TIMEOUT 100		// in 10us steps;
#define IGB_VC_SMI_MDIO_RETRIES 50	// # of smi mdio retries;

// marvell switch defs;

#define M88E6320_ID ((M88_VENDOR << 16) | 0x1150)
#define M88E6176_ID ((M88_VENDOR << 16) | 0x1760)
#define M88E1112_ID ((M88_VENDOR << 16) | 0x0c90)
#define M88E1514_ID ((M88_VENDOR << 16) | 0x0dd0)

#define M88_SW_PORT_BASE 0x10		// base of port registers;
#define M88_SW_PORT_STATUS 0x00		// port status reg;
#define M88_SW_PORT_PHYSCTRL 0x01	// port physical control reg;
#define M88_SW_PORT_PROD_ID 0x03	// product ID reg;
#define M88_SW_PORT_CONTROL 0x04	// port control reg;
#define M88_SW_PORT_VLAN_MAP 0x06	// port based vlan map;

#define M88_SW_PORT_STATUS_PHYDET	(1 << 12)
#define M88_SW_PORT_STATUS_LINK_UP	(1 << 11)
#define M88_SW_PORT_STATUS_DUPLEX	(1 << 10)
#define M88_SW_PORT_STATUS_SPEED	(3 << 8)
#define M88_SW_PORT_STATUS_SPEED_10	(0 << 8)
#define M88_SW_PORT_STATUS_SPEED_100	(1 << 8)
#define M88_SW_PORT_STATUS_SPEED_1000	(2 << 8)

#define M88_SW_PORT_CONTROL_DIS		(0 << 0)
#define M88_SW_PORT_CONTROL_BLKLIST	(1 << 0)
#define M88_SW_PORT_CONTROL_LEARN	(2 << 0)
#define M88_SW_PORT_CONTROL_FWD		(3 << 0)

#define M88_SW_GLOBAL1 0x1b		// global1 regs;
#define M88_SW_GLOBAL2 0x1c		// global2 regs;
#define M88_SW_GLOBAL3 0x1d		// global3 regs;

#define M88_SW_GL1_GSTA 0
#define M88_SW_GL1_CTRL 4
#define M88_SW_GL1_CTRL_SWRESET 0x8000

#define M88_GL2_SMI_CMD 0x18		// SMI command;
#define M88_GL2_SMI_CMD_BUSY 0x8000	// SMI busy;
#define M88_GL2_SMI_CMD_MODE 0x1000	// SMI mode, 0=clause45, 1=clause22;
#define M88_GL2_SMI_CMD_OP 0x0c00	// SMI op;
#define M88_GL2_SMI_CMD_OP_WRITE 0x0400	// SMI write;
#define M88_GL2_SMI_CMD_OP_READ 0x0800	// SMI read;
#define M88_GL2_SMI_CMD_ADDR 0x003e	// SMI device address;
#define M88_GL2_SMI_CMD_REG 0x001f	// SMI register address;
#define M88_GL2_SMI_DATA 0x19		// SMI data;

#define M88_GL2_SRATCH_MISC 0x1a	// scratch/misc;

#define SUPPORTED_M88E6320 (SUPPORTED_Autoneg | SUPPORTED_TP \
	| SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full \
	| SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full \
	| SUPPORTED_1000baseT_Full)

// per-port registers;

#define M88_SW_REG(bank,reg) ((bank<<5) | reg)
#define M88_SW_PORT_REG(port,reg) (((M88_SW_PORT_BASE+port)<<5) | reg)

// phy fiber special control;

#define M88E1000_SPEC_CTRL_SIGDET (1 << 9)

#define M88E1000_SPEC_CTRL_PREFER			(3 << 10)
#define M88E1000_SPEC_CTRL_PREFER_NONE			(0 << 10)
#define M88E1000_SPEC_CTRL_PREFER_FIBER			(1 << 10)
#define M88E1000_SPEC_CTRL_PREFER_COPPER		(2 << 10)

#define M88E1000_SPEC_CTRL_MODE				(7 << 7)
#define M88E1000_SPEC_CTRL_MODE_100BASEFX		(0 << 7)
#define M88E1000_SPEC_CTRL_MODE_COPPER_GBIC		(1 << 7)
#define M88E1000_SPEC_CTRL_MODE_A_COPPER_SGMII		(2 << 7)
#define M88E1000_SPEC_CTRL_MODE_A_COPPER_1000BASEX	(3 << 7)
#define M88E1000_SPEC_CTRL_MODE_COPPER_ONLY		(5 << 7)
#define M88E1000_SPEC_CTRL_MODE_SGMII_ONLY		(6 << 7)
#define M88E1000_SPEC_CTRL_MODE_1000BASEX_ONLY		(7 << 7)

#define M88E1000_SPEC_CTRL_PWR_UP			(1 << 3)
#define M88E1000_SPEC_CTRL_ENH_SGMII			(1 << 2)

// GPIOs for board control;
// from velocloud_vc.c

#define EDGE5X0_GPIO_PCA9557_30 70
#define EDGE5X0_GPIO_RST_SW_A (EDGE5X0_GPIO_PCA9557_30 + 2)
#define EDGE5X0_GPIO_RST_SW_B (EDGE5X0_GPIO_PCA9557_30 + 3)
#define EDGE5X0_GPIO_RST_1514 (EDGE5X0_GPIO_PCA9557_30 + 4)

// others;

#define IGB_VC_PAGE 0x8000	// set phy page;

// SFP I2C;

#define IGB_VC_SFP "sfp"	// sfp i2c driver name;
#define IGB_VC_SFP_BUS_FUNC	3

#define SFP_OFF_IDENTIFIER	0
#define SFP_IDENTIFIER_SFP	0x03

#define SFP_OFF_VENDOR		20
#define SFP_SIZE_VENDOR		16

#define SFP_OFF_OUI		37
#define SFP_SIZE_OUI		3

#define SFP_OFF_PARTNUM		40
#define SFP_SIZE_PARTNUM	16

enum sfp_i2c_client {
	SFP_EEPROM = 0,
	SFP_DMI,
	N_SFP_CLIENT,
};

struct vc_i2c_sfp {
	struct igb_adapter *adapter;
	struct i2c_client *client[N_SFP_CLIENT];
	int plugged;
	char vendor[SFP_SIZE_VENDOR+1];
	unsigned char oui[SFP_SIZE_OUI];
	char partnum[SFP_SIZE_PARTNUM+1];
};

// sfp i2c data;

static unsigned short igb_vc_i2c_addrs[] = { 0x50, 0x51, I2C_CLIENT_END };
static struct vc_i2c_sfp igb_vc_i2c_sfp;

// DSA specific;

// module param to disable dsa;

enum dsa_flags {
	DSA_DISABLE = 0,
	DSA_ENABLE = 0x01,	// enable DSA;
	DSA_NOSWBR = 0x02,	// do not bring up switch(es) in LAN bridging mode;
	DSA_PORTEN = 0x04,	// force ports on non-bridged;
};

static int dsa = 0;
module_param(dsa, int, 0444);
MODULE_PARM_DESC(dsa, "Enable DSA on switch.");

#define VC_DSA "dsa"

// switch/phy voltage control;

static int avdd = 0x80;
module_param(avdd, int, 0444);
MODULE_PARM_DESC(avdd, "Set switch/phy analog Vdd.");

enum avdd {
	AVDD_6320_1_35V = 0,
	AVDD_6320_1_40V = 1,
	AVDD_6320_1_45V = 2,
	AVDD_6320_1_50V = 3,
	AVDD_6320_MASK = 0x7f,
	AVDD_6320_SET = 0x80,
};

// switch tree;

static char vc_sw_name[] = "sw%d";
static char vc_sw_cpu[] = "swcpu%d";
static char vc_sw0_name[] = "sw0p%d";
static char vc_sw0_cpu[] = "sw0cpu%d";
static char vc_sw0_dsa[] = "sw0dsa%d";
static char vc_sw1_name[] = "sw1p%d";
static char vc_sw1_cpu[] = "sw1cpu%d";
static char vc_sw1_dsa[] = "sw1dsa%d";

// edge500;

static struct dsa_chip_data vc_switch = {
	.sw_addr = 0,
	.port_names = {
		vc_sw_name,	// 0
		vc_sw_name,	// 1
		vc_sw_name,	// 2
		vc_sw_name,	// 3
		vc_sw_cpu,	// 4
	},
};

static struct dsa_platform_data vc_switch_data = {
	.nr_chips = 1,
	.chip = &vc_switch,
};

// edge520/540;
// ports 5/6 are rgmii cross connects;

static struct dsa_chip_data vc_sw0 = {
	.sw_addr = 0,
	.root = 0,
	.port_names = {
		vc_sw0_name,	// 0 rj45
		vc_sw0_name,	// 1 rj45
		vc_sw0_name,	// 2 rj45
		vc_sw0_name,	// 3 rj45
		vc_sw0_cpu,	// 4
		vc_sw0_dsa,	// 5 rgmii
		vc_sw0_dsa,	// 6 rgmii
	},
};

static struct dsa_chip_data vc_sw1 = {
	.sw_addr = 0,
	.root = 1,
	.port_names = {
		vc_sw1_name,	// 0 rj45
		vc_sw1_name,	// 1 rj45
		vc_sw1_name,	// 2 rj45
		vc_sw1_name,	// 3 rj45
		vc_sw1_cpu,	// 4
		vc_sw1_dsa,	// 5 rgmii
		vc_sw1_dsa,	// 6 rgmii
	},
};

static struct dsa_platform_data vc_sw0_data = {
	.nr_chips = 1,
	.chip = &vc_sw0,
};

static struct dsa_platform_data vc_sw1_data = {
	.nr_chips = 1,
	.chip = &vc_sw1,
};

// dsa config per platform;

static struct dsa_platform_data *vc_dsa_500[IGB_VC_N_BUS_FUNC] = { NULL, &vc_switch_data, NULL, NULL };
static struct dsa_platform_data *vc_dsa_5x0[IGB_VC_N_BUS_FUNC] = { &vc_sw0_data, &vc_sw1_data, NULL, NULL };

static u8 vc_5x0_igb_inmem_reset[IGB_VC_N_BUS_FUNC] = { 0 };

// dsa private;

struct vc_dsa_priv {
	struct e1000_hw *hw;
	struct platform_device pdev;
	int irqs[PHY_MAX_ADDR];
};


// busy-wait for MDIO completion;
// returns <0 if error, else mdic bits;
// XXX why we not use completion intr?

static s32 igb_vc_mdio_wait(struct e1000_hw *hw){
	int i;
	u32 mdic;

	// poll the ready bit, busy-waiting;

	for(i = 0; i < IGB_VC_MDIO_TIMEOUT; i++) {
		udelay(10);
		mdic = rd32(E1000_MDIC);
		if(mdic & E1000_MDIC_READY)
			break;
	}
	if( !(mdic & E1000_MDIC_READY)) {
		hw_dbg("MDIO read did not complete\n");
		return(-E1000_ERR_PHY);
	}
	if(mdic & E1000_MDIC_ERROR) {
		hw_dbg("MDIO error\n");
		return(-E1000_ERR_PHY);
	}
	return(mdic & 0xffff);
}

// set page for phy access;

static s32 igb_vc_mdio_page(struct e1000_hw *hw, u16 page){
	u32 mdic;

	mdic = E1000_MDIC_OP_WRITE | (M88E1000_PHY_PAGE22 << E1000_MDIC_REG_SHIFT) | page;
	wr32(E1000_MDIC, mdic);
	return(igb_vc_mdio_wait(hw));
}

// read MDIO registers;
// works with single/multi-chip addressing;

static s32 igb_vc_mdio_read(struct e1000_hw *hw, u32 reg, u16 *data){
	u32 addr, mdic;
	s32 ret = 0;

	// phy->addr=0 indicates single-chip addressing;

	addr = hw->phy.addr;
	if(addr == 0)
		addr = (reg >> 5) & 0x1f;
	//printk("%s %d/%d\n", __func__, addr, reg);

	// acquire MDIO interface;

	ret = hw->phy.ops.acquire(hw);
	if(ret)
		goto out;

	// the phy address is now in MDICNFG;
	// always overwrite PHY addr, since other device can share the bus;

	mdic = rd32(E1000_MDICNFG);
	mdic &= (E1000_MDICNFG_EXT_MDIO | E1000_MDICNFG_COM_MDIO);
	mdic |= (addr << E1000_MDIC_PHY_SHIFT);
	wr32(E1000_MDICNFG, mdic);

	// set phy page;
	// needs to be atomic with respect to mdio access;

	if(reg & IGB_VC_PAGE) {
		ret = igb_vc_mdio_page(hw, reg >> 16);
		if(ret < 0)
			goto fail;
	}

	// setup MDIC op code;
	// this starts the MDIO access by the MAC;

	reg &= 0x1f;
	mdic = E1000_MDIC_OP_READ | (reg << E1000_MDIC_REG_SHIFT);
	wr32(E1000_MDIC, mdic);

	// wait for MDIC to complete or error;
	// release MDIO interface;

	ret = igb_vc_mdio_wait(hw);
fail:
	hw->phy.ops.release(hw);
	if(ret < 0)
		goto out;
	*data = ret;
	ret = 0;
out:
	return(ret);
}

// write MDIO registers;
// works with single/multi-chip addressing;

static s32 igb_vc_mdio_write(struct e1000_hw *hw, u32 reg, u16 data){
	u32 addr, mdic;
	s32 ret = 0;

	// phy->addr=0 indicates single-chip addressing;

	addr = hw->phy.addr;
	if(addr == 0)
		addr = (reg >> 5) & 0x1f;
	//printk("%s %d/%d=0x%x\n", __func__, addr, reg, data);

	// acquire MDIO interface;

	ret = hw->phy.ops.acquire(hw);
	if(ret)
		goto out;

	// the phy address is now in MDICNFG;
	// always overwrite PHY addr, since other device can share the bus;

	mdic = rd32(E1000_MDICNFG);
	mdic &= (E1000_MDICNFG_EXT_MDIO | E1000_MDICNFG_COM_MDIO);
	mdic |= (addr << E1000_MDIC_PHY_SHIFT);
	wr32(E1000_MDICNFG, mdic);

	// set phy page;
	// needs to be atomic with respect to mdio access;

	if(reg & IGB_VC_PAGE) {
		ret = igb_vc_mdio_page(hw, reg >> 16);
		if(ret < 0)
			goto fail;
	}

	// setup MDIC op code;
	// this starts the MDIO access by the MAC;

	reg &= 0x1f;
	mdic = E1000_MDIC_OP_WRITE | (reg << E1000_MDIC_REG_SHIFT) | data;
	wr32(E1000_MDIC, mdic);

	// wait for MDIC to complete or error;
	// release MDIO interface;

	ret = igb_vc_mdio_wait(hw);
fail:
	hw->phy.ops.release(hw);
	if(ret < 0)
		goto out;
	ret = 0;
out:
	return(ret);
}

// read MDIO registers;
// using bitbang MDIO bus;

static s32 igb_vc_mdiobb_read(struct e1000_hw *hw, u32 reg, u16 *data){
	s32 ret = -ENODEV;

	// read the register;

	if( !vc_mdio_bus)
		return(ret);
	ret = mdiobus_read(vc_mdio_bus, hw->phy.addr, reg);
	vcdbg("%d/%d = 0x%x\n", 0, reg, ret);
	if(ret < 0)
		return(ret);
	*data = ret;
	return(0);
}

// write MDIO registers;
// using bitbang MDIO bus;

static s32 igb_vc_mdiobb_write(struct e1000_hw *hw, u32 reg, u16 data){
	s32 ret = -ENODEV;

	// read the register;

	if( !vc_mdio_bus)
		return(ret);
	ret = mdiobus_write(vc_mdio_bus, hw->phy.addr, reg, data);
	vcdbg("%d/%d = %d 0x%x\n", 0, reg, ret, data);
	if(ret < 0)
		return(ret);
	return(0);
}

// read MDIO SGMII register;
// on 88e1112 point to the MAC of the port;

static s32 igb_vc_m88phy_sgmii_mdio_read(struct e1000_hw *hw, u32 reg, u16 *data){
	s32 ret;

	ret = igb_vc_mdio_read(hw, IGB_VC_PAGE | (2 << 16) | reg, data);
	vcdbg("%d/%d = %d 0x%x\n", 2, reg, ret, *data);
	return(ret);
}

// write MDIO SGMII register;
// on 88e1112 point to the MAC of the port;

static s32 igb_vc_m88phy_sgmii_mdio_write(struct e1000_hw *hw, u32 reg, u16 data){
	s32 ret;

	ret = igb_vc_mdio_write(hw, IGB_VC_PAGE | (2 << 16) | reg, data);
	vcdbg("%d/%d = %d 0x%x\n", 2, reg, ret, data);
	return(ret);
}

// read MDIO SFP PHY register;
// on 88e1112 point to the SFP side;

static s32 igb_vc_m88phy_sfp_mdio_read(struct e1000_hw *hw, u32 reg, u16 *data){
	s32 ret;

	ret = igb_vc_mdio_read(hw, IGB_VC_PAGE | (1 << 16) | reg, data);
	vcdbg("%d/%d = %d 0x%x\n", 1, reg, ret, *data);
	return(ret);
}

// write MDIO PHY register;
// on 88e1112 point to the SFP side;

static s32 igb_vc_m88phy_sfp_mdio_write(struct e1000_hw *hw, u32 reg, u16 data){
	s32 ret;

	ret = igb_vc_mdio_write(hw, IGB_VC_PAGE | (1 << 16) | reg, data);
	vcdbg("%d/%d = %d 0x%x\n", 1, reg, ret, data);
	return(ret);
}

// issue smi command;
// wait for completion;
// reg[4:0] register;
// reg[9:5] device (copper 0..N, 0xf serdes);

static s32 igb_vc_smi_cmd(struct e1000_hw *hw, u32 reg, u16 write, u16 *data){
	u32 mdic, i;
	u16 cmd;
	s32 ret;

	// write SMI_DATA for write command;

	cmd = M88_GL2_SMI_CMD_BUSY | M88_GL2_SMI_CMD_MODE | (reg & 0x3ff);
	if(write) {
		mdic = E1000_MDIC_OP_WRITE | (M88_GL2_SMI_DATA << E1000_MDIC_REG_SHIFT) | *data;
		wr32(E1000_MDIC, mdic);
		ret = igb_vc_mdio_wait(hw);
		if(ret < 0)
			goto out;
		cmd |= M88_GL2_SMI_CMD_OP_WRITE;
	} else
		cmd |= M88_GL2_SMI_CMD_OP_READ;

	// issue smi command;

	mdic = E1000_MDIC_OP_WRITE | (M88_GL2_SMI_CMD << E1000_MDIC_REG_SHIFT) | cmd;
	wr32(E1000_MDIC, mdic);
	ret = igb_vc_mdio_wait(hw);
	if(ret < 0)
		goto out;

	// poll SMI_CMD for completion;

	for(i = 0; i < IGB_VC_SMI_MDIO_RETRIES; i++) {
		mdic = E1000_MDIC_OP_READ | (M88_GL2_SMI_CMD << E1000_MDIC_REG_SHIFT);
		wr32(E1000_MDIC, mdic);
		ret = igb_vc_mdio_wait(hw);
		if(ret < 0)
			goto out;
		if( !(ret & M88_GL2_SMI_CMD_BUSY))
			break;
		udelay(10);
	}
	if(i >= IGB_VC_SMI_MDIO_RETRIES) {
		ret = -E1000_ERR_PHY;
		goto out;
	}

	// read data for read command;

	if( !write) {
		mdic = E1000_MDIC_OP_READ | (M88_GL2_SMI_DATA << E1000_MDIC_REG_SHIFT);
		wr32(E1000_MDIC, mdic);
		ret = igb_vc_mdio_wait(hw);
		if(ret < 0)
			goto out;
		*data = ret;
	}
	ret = 0;
out:
	return(ret);
}

// read SMI MDIO registers;
// of Marvell switch acting as SGMII/copper PHY;
// the PHY registers cannot be accessed directly through bank;
// instead, we must go through the SMI_CMD/SMI_DATA registers;
// reg[4:0] register;
// reg[9:5] device (copper 0..N, 0xf fiber);
// reg[31:16] page;

static s32 igb_vc_smi_mdio_read(struct e1000_hw *hw, u32 reg, u16 *data){
	u32 mdic;
	u16 page;
	s32 ret;

	// acquire MDIO interface;

	ret = hw->phy.ops.acquire(hw);
	if(ret)
		return(ret);

	// the phy address is now in MDICNFG;
	// always overwrite PHY addr, since other device can share the bus;
	// SMI_CMD/SMI_DATA are in GLOBAL2;

	mdic = rd32(E1000_MDICNFG);
	mdic &= (E1000_MDICNFG_EXT_MDIO | E1000_MDICNFG_COM_MDIO);
	mdic |= (M88_SW_GLOBAL2 << E1000_MDIC_PHY_SHIFT);
	wr32(E1000_MDICNFG, mdic);

	// the smi interface should be available, no other contenders;

	mdic = E1000_MDIC_OP_READ | (M88_GL2_SMI_CMD << E1000_MDIC_REG_SHIFT);
	wr32(E1000_MDIC, mdic);
	ret = igb_vc_mdio_wait(hw);
	if(ret < 0)
		goto out;
	if(mdic & M88_GL2_SMI_CMD_BUSY) {
		ret = -E1000_ERR_PHY;
		goto out;
	}

	// issue smi command to set page;

	page = reg >> 16;
	ret = igb_vc_smi_cmd(hw, (reg & 0x3e0) | M88E1000_PHY_PAGE22, 1, &page);
	if(ret)
		goto out;

	// issue smi read command;

	ret = igb_vc_smi_cmd(hw, reg, 0, data);
out:
	hw->phy.ops.release(hw);
	return(ret);
}

// write SMI MDIO registers;
// of Marvell switch acting as SGMII/copper PHY;
// the PHY registers cannot be accessed directly through bank;
// instead, we must go through the SMI_CMD/SMI_DATA registers;
// reg[4:0] register;
// reg[9:5] device (copper 0..N, 0xf fiber);
// reg[31:16] page;

static s32 igb_vc_smi_mdio_write(struct e1000_hw *hw, u32 reg, u16 data){
	u32 mdic;
	u16 page;
	s32 ret;

	// acquire MDIO interface;

	ret = hw->phy.ops.acquire(hw);
	if(ret)
		return(ret);

	// the phy address is now in MDICNFG;
	// always overwrite PHY addr, since other device can share the bus;
	// SMI_CMD/SMI_DATA are in GLOBAL2;

	mdic = rd32(E1000_MDICNFG);
	mdic &= (E1000_MDICNFG_EXT_MDIO | E1000_MDICNFG_COM_MDIO);
	mdic |= (M88_SW_GLOBAL2 << E1000_MDIC_PHY_SHIFT);
	wr32(E1000_MDICNFG, mdic);

	// the smi interface should be available, no other contenders;

	mdic = E1000_MDIC_OP_READ | (M88_GL2_SMI_CMD << E1000_MDIC_REG_SHIFT);
	wr32(E1000_MDIC, mdic);
	ret = igb_vc_mdio_wait(hw);
	if(ret < 0)
		goto out;

	// issue smi command to set page;

	page = reg >> 16;
	ret = igb_vc_smi_cmd(hw, (reg & 0x3e0) | M88E1000_PHY_PAGE22, 1, &page);
	if(ret)
		goto out;

	// issue smi read command;

	ret = igb_vc_smi_cmd(hw, reg, 1, &data);
out:
	hw->phy.ops.release(hw);
	return(ret);
}

// read MDIO SGMII register;
// on 88e6320 point to the serdes of the port;

static s32 igb_vc_m88sw_sgmii_mdio_read(struct e1000_hw *hw, u32 reg, u16 *data){
	s32 ret;

	ret = igb_vc_smi_mdio_read(hw, (1 << 16) | (hw->phy.ports[e1000_port_sgmii] << 5) | reg, data);
	vcdbg("%d/%d/%d = %d 0x%x\n", 1, hw->phy.ports[e1000_port_sgmii], reg & 0x1f, ret, *data);
	return(ret);
}

// write MDIO SGMII register;
// on 88e6320 point to the serdes of the port;

static s32 igb_vc_m88sw_sgmii_mdio_write(struct e1000_hw *hw, u32 reg, u16 data){
	s32 ret;

	ret = igb_vc_smi_mdio_write(hw, (1 << 16) | (hw->phy.ports[e1000_port_sgmii] << 5) | reg, data);
	vcdbg("%d/%d/%d = %d 0x%x\n", 1, hw->phy.ports[e1000_port_sgmii], reg & 0x1f, ret, data);
	return(ret);
}

// read MDIO PHY register;
// on 88e6320 point to the copper PHYs of the port;

static s32 igb_vc_m88sw_phy_mdio_read(struct e1000_hw *hw, u32 reg, u16 *data){
	s32 ret;

	ret = igb_vc_smi_mdio_read(hw, (hw->phy.ports[e1000_port_phy] << 5) | reg, data);
	vcdbg("%d/%d/%d = %d 0x%x\n", 0, hw->phy.ports[e1000_port_phy], reg, ret, *data);
	return(ret);
}

// write MDIO PHY register;
// on 88e6320 point to the copper PHYs of the port;

static s32 igb_vc_m88sw_phy_mdio_write(struct e1000_hw *hw, u32 reg, u16 data){
	s32 ret;

	ret = igb_vc_smi_mdio_write(hw, (hw->phy.ports[e1000_port_phy] << 5) | reg, data);
	vcdbg("%d/%d/%d = %d 0x%x\n", 0, hw->phy.ports[e1000_port_phy], reg, ret, data);
	return(ret);
}

#if defined(IGB_VC_DUMP_6320) || defined(IGB_VC_DUMP_6176)

// dump page of m88 switch regs;

static void igb_dump_sw(struct e1000_hw *hw, int page){
	int reg, ret;
	u16 data;

	printk("%s %d\n", __func__, hw->bus.func);

	for(reg = 0; reg < 32; reg++) {
		ret = igb_vc_mdio_read(hw, M88_SW_REG(page, reg), &data);
		if(ret)
			continue;
		printk(" reg%d/%d 0x%x\n", page, reg, data);
	}
}

// dump page of m88 switch phy regs;

static void igb_dump_swphy(struct e1000_hw *hw, u32 addr){
	int reg, ret;
	u16 data;

	printk("%s %d\n", __func__, hw->bus.func);

	for(reg = 0; reg < 32; reg++) {
		ret = igb_vc_smi_mdio_read(hw, addr | reg, &data);
		if(ret)
			continue;
		printk(" phy%x/%d/%d 0x%x\n", addr >> 16, (addr >> 5) & 0x1f, reg, data);
	}
}

#endif

#if defined(IGB_VC_DUMP_1112)

// dump page of m88 phy regs;

static void igb_dump_phy(struct e1000_hw *hw, short page){
	int reg, ret;
	u16 data;

	printk("%s %d\n", __func__, hw->bus.func);

	for(reg = 0; reg < 32; reg++) {
		ret = hw->phy.ops.read_reg(hw, IGB_VC_PAGE | (page << 16) | reg, &data);
		if(ret)
			continue;
		printk(" phy%x/%d 0x%x\n", page, reg & 0x1f, data);
	}
}

#endif

// dump page of m88 phy via bitbang MDIO;

#if defined(IGB_VC_DUMP_1514)

static void igb_dump_phy_bb(struct e1000_hw *hw, short page){
	int reg, ret;
	u16 old, data;

	printk("%s %d\n", __func__, hw->bus.func);

	hw->phy.ops.read_reg(hw, M88E1000_PHY_PAGE22, &old);
	hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE22, page);

	for(reg = 0; reg < 32; reg++) {
		ret = hw->phy.ops.read_reg(hw, reg, &data);
		if(ret)
			continue;
		printk(" phy%x/%d 0x%x\n", page, reg & 0x1f, data);
	}
	hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE22, old);
}

#endif

// extra info for edge500;
struct igb_vc_info {
	u8 ports[e1000_n_phy_ports];
	u8 owr;
	int (*getid)(struct e1000_hw *);
	s32 (*read_reg)(struct e1000_hw *, u32, u16 *);
	s32 (*write_reg)(struct e1000_hw *, u32, u16);
};

// probe Marvell switches as PHYs;
// probe the product ID of the port registers;

// 88e6320 uses single-chip addressing, ie. all device addresses used as bank select;
// the 88e1112 is on the same MDIO bus, bus responds to address 0x7, which is
// not used by the 88e6320, so both should coexist without conflicts;

// 88e6176 uses single-chip addressing, ie. all device addresses used as bank select;

static int igb_vc_m88sw_id(struct e1000_hw *hw){
	int i, ret;
	u32 addr = M88_SW_PORT_REG(hw->phy.ports[e1000_port_cpu], M88_SW_PORT_PROD_ID);
	u16 prod_id;

	// flush garbage on mdio bus via read;
	// read product ID from port;
	// make up marvell PHY ID;

	for(i = 0; i < IGB_VC_MDIO_READY_TO; i++) {
		ret = igb_vc_mdio_read(hw, addr, &prod_id);
		if(ret >= 0)
			break;
		udelay(1000);
	}
	vcdbg("prod id  %d 0x%x\n", ret, prod_id);
	if(ret == 0) {
		hw->phy.id = (M88_VENDOR << 16) | (prod_id & PHY_REVISION_MASK);
		hw->phy.revision = prod_id & ~PHY_REVISION_MASK;
	}
	return(ret);
}

// extra info to handle special PHYs;
// unfortunatedly, we need to make this link dependent;

// edge500;
// 0: 88e6320 port 0 (WAN0);
// 1: 88e6176 port 4 (LAN switch);
// 2: 88e6320 port 1 (WAN1);
// 3: 88e1112 PHY (SFP);

static struct igb_vc_info igb_vc500_info[IGB_VC_N_BUS_FUNC] = {
	{ .ports = { 0, 0xc, 3 },
	  .getid = igb_vc_m88sw_id,
	},
	{ .ports = { 4, 0xf, 0 },
	  .getid = igb_vc_m88sw_id,
	},
	{ .ports = { 1, 0xd, 4 },
	  .getid = igb_vc_m88sw_id,
	},
	{ .ports = { 0, 0, 0 },
	  .getid = igb_get_phy_id,
	  .read_reg = igb_read_phy_reg_82580,
	  .write_reg = igb_write_phy_reg_82580,
	},
};

// edge520/540;
// 0: 88e6176 port 4 (lan0 switch);
// 1: 88e6176 port 4 (lan1 switch);
// 2: 88e1514 PHY (WAN0);
// 3: 88e1514 PHY (WAN1);

static struct igb_vc_info igb_vc5x0_info[IGB_VC_N_BUS_FUNC] = {
	{ .ports = { 4, 0xf, 0 },
	  .getid = igb_vc_m88sw_id,
	},
	{ .ports = { 4, 0xf, 0 },
	  .getid = igb_vc_m88sw_id,
	},
	{ .ports = { 0, 0, 0 },
	  .owr = e1000_port_phy,
	  .getid = igb_get_phy_id,
	  .read_reg = igb_vc_mdiobb_read,
	  .write_reg = igb_vc_mdiobb_write,
	},
	{ .ports = { 0, 0, 1 },
	  .owr = e1000_port_phy,
	  .getid = igb_get_phy_id,
	  .read_reg = igb_vc_mdiobb_read,
	  .write_reg = igb_vc_mdiobb_write,
	},
};

// reset gpio per bus function;

static int igb_vc5x0_reset_gpio[IGB_VC_N_BUS_FUNC] = {
	EDGE5X0_GPIO_RST_SW_A,
	EDGE5X0_GPIO_RST_SW_B,
	EDGE5X0_GPIO_RST_1514,
	-1,
};

// edge5x0: reset switches and PHYs;

static s32 igb_vc5x0_reset(struct e1000_hw *hw){
	int gpio = igb_vc5x0_reset_gpio[hw->bus.func];

	if(gpio >= 0) {
		vcdbg("resetting func %d sw/phy\n", hw->bus.func);
		gpio_set_value(gpio, 0);
		mdelay(100);
		gpio_set_value(gpio, 1);
	}
	return(0);
}

// reset cpu port serdes phy;

static s32 igb_vc_m88sw_sgmii_reset(struct e1000_hw *hw){
	s32 ret;
	u16 ctrl;

	// force auto-negotiation off;
	// take out of power-down mode;

	ret = igb_vc_m88sw_sgmii_mdio_read(hw, PHY_CONTROL, &ctrl);
	if(ret)
		goto out;
	ctrl |= (MII_CR_RESET | MII_CR_FULL_DUPLEX);
	ctrl &= ~(MII_CR_AUTO_NEG_EN | MII_CR_POWER_DOWN);
	ret = igb_vc_m88sw_sgmii_mdio_write(hw, PHY_CONTROL, ctrl);
	udelay(1);
out:
	return(ret);
}

// reset copper phy port;

static s32 igb_vc_m88sw_phy_reset(struct e1000_hw *hw){
	s32 ret;
	u16 ctrl;

	// take out of power-down mode;

	ret = igb_vc_m88sw_phy_mdio_read(hw, PHY_CONTROL, &ctrl);
	if(ret)
		goto out;
	ctrl &= ~(MII_CR_POWER_DOWN
		| MII_CR_FULL_DUPLEX
		| MII_CR_SPEED_100 | MII_CR_SPEED_1000
		| MII_CR_AUTO_NEG_EN);

	if(hw->swphy.autoneg)
		ctrl |= MII_CR_AUTO_NEG_EN;
	else {
		if(hw->swphy.duplex)
			ctrl |= MII_CR_FULL_DUPLEX;
		switch(hw->swphy.speed) {
		case SPEED_10:
			ctrl |= MII_CR_SPEED_10;
			break;
		case SPEED_100:
			ctrl |= MII_CR_SPEED_100;
			break;
		case SPEED_1000:
			ctrl |= MII_CR_SPEED_1000;
			break;
		}
	}
	ctrl |= MII_CR_RESET;

	ret = igb_vc_m88sw_phy_mdio_write(hw, PHY_CONTROL, ctrl);
	udelay(1);
out:
	return(ret);
}

// force a switch port up or down;

enum {
	IGB_SW_PORT_UNFORCE = 0,
	IGB_SW_PORT_DOWN,
	IGB_SW_PORT_UP,
};

static s32 igb_vc_m88sw_port_force(struct e1000_hw *hw, int what){
	s32 ret;
	u16 ctrl;
	u32 port = hw->phy.ports[e1000_port_phy];

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(port, M88_SW_PORT_PHYSCTRL), &ctrl);
	if(ret)
		goto out;

	switch(what) {
	case IGB_SW_PORT_UNFORCE:
		ctrl &= ~0x10;
		break;
	case IGB_SW_PORT_DOWN:
		ctrl |= 0x10;
		ctrl &= ~0x20;
		break;
	case IGB_SW_PORT_UP:
		ctrl |= 0x30;
		break;
	}
	ret = igb_vc_mdio_write(hw,
		M88_SW_PORT_REG(port, M88_SW_PORT_PHYSCTRL), ctrl);
out:
	return(ret);
}

// reset marvell switch ports;

static s32 igb_vc_m88sw_port_reset(struct e1000_hw *hw){
	s32 ret;

	// reset the copper PHY;

	ret = igb_vc_m88sw_phy_reset(hw);
	if(ret)
		goto out;

	// reset the cpu port serdes;

	ret = igb_vc_m88sw_sgmii_reset(hw);
out:
	return(ret);
}

// reset sfp port;

static s32 igb_vc_m88phy_sfp_reset(struct e1000_hw *hw){
	s32 ret;
	u16 ctrl;

	// reset the sfp side;

	ret = igb_vc_m88phy_sfp_mdio_read(hw, PHY_CONTROL, &ctrl);
	if(ret)
		goto out;
	ctrl &= ~MII_CR_POWER_DOWN;
	ctrl |= MII_CR_RESET;
	ret = igb_vc_m88phy_sfp_mdio_write(hw, PHY_CONTROL, ctrl);
	if(ret)
		goto out;

	// reset the sgmii side;

	ret = igb_vc_m88phy_sgmii_mdio_read(hw, PHY_CONTROL, &ctrl);
	if(ret)
		goto out;
	ctrl &= ~MII_CR_POWER_DOWN;
	ctrl |= MII_CR_RESET;
	ret = igb_vc_m88phy_sgmii_mdio_write(hw, PHY_CONTROL, ctrl);
out:
	return(ret);
}

// setup sgmii link;
// link needs to be forced due to MAC-MAC link;

static s32 igb_vc_setup_sgmii_link_forced(struct e1000_hw *hw){
	u32 cfg, ctrl, lctl;

	// enable PCS;

	cfg = rd32(E1000_PCS_CFG0);
	cfg |= E1000_PCS_CFG_PCS_EN;
	wr32(E1000_PCS_CFG0, cfg);

	// SLU must be set to enable serdes;
	// force speed and duplex;

	ctrl = rd32(E1000_CTRL);
	ctrl |= (E1000_CTRL_SLU
		| E1000_CTRL_SPD_1000
		| E1000_CTRL_FD
		| E1000_CTRL_FRCSPD
		| E1000_CTRL_FRCDPX);
	wr32(E1000_CTRL, ctrl);
	vcdbg("ctrl 0x%x\n", ctrl);

	// force sgmii link;
	// no auto-negotitation;

	lctl = rd32(E1000_PCS_LCTL);
	lctl &= ~(E1000_PCS_LCTL_AN_ENABLE
		| E1000_PCS_LCTL_AN_RESTART
		| E1000_PCS_LCTL_AN_TIMEOUT);
	lctl |= (E1000_PCS_LCTL_FLV_LINK_UP
		| E1000_PCS_LCTL_FSV_1000
		| E1000_PCS_LCTL_FDV_FULL
		| E1000_PCS_LCTL_FSD
		| E1000_PCS_LCTL_FORCE_FCTRL
		| E1000_PCS_LCTL_FORCE_LINK);
	wr32(E1000_PCS_LCTL, lctl);
	vcdbg("lctl 0x%x\n", lctl);

	ctrl = rd32(E1000_PCS_LSTAT);
	vcdbg("lsts 0x%x\n", ctrl);

	return(E1000_SUCCESS);
}

// setup sgmii link for autoneg;

static s32 igb_vc_setup_sgmii_link_aneg(struct e1000_hw *hw){
	u32 cfg, ctrl, lctl, an;

	// enable PCS;

	cfg = rd32(E1000_PCS_CFG0);
	cfg |= E1000_PCS_CFG_PCS_EN;
	wr32(E1000_PCS_CFG0, cfg);

	// set link up, clear forcing of speed and duplex;

	ctrl = rd32(E1000_CTRL);
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl |= E1000_CTRL_SLU;
	wr32(E1000_CTRL, ctrl);
	vcdbg("ctrl 0x%x\n", ctrl);

	// setup sgmii link for autoneg;

	lctl = rd32(E1000_PCS_LCTL);
	lctl &= ~(E1000_PCS_LCTL_AN_TIMEOUT
		| E1000_PCS_LCTL_FLV_LINK_UP
		| E1000_PCS_LCTL_FSD
		| E1000_PCS_LCTL_FORCE_FCTRL
		| E1000_PCS_LCTL_FORCE_LINK);
	lctl |= (E1000_PCS_LCTL_AN_ENABLE
		| E1000_PCS_LCTL_AN_RESTART);

	// configure flow-control for autoneg;

	an = rd32(E1000_PCS_ANADV);
	an &= ~(E1000_TXCW_ASM_DIR | E1000_TXCW_PAUSE);

	switch(hw->fc.requested_mode) {
	case e1000_fc_full:
	case e1000_fc_rx_pause:
		an |= E1000_TXCW_ASM_DIR;
		an |= E1000_TXCW_PAUSE;
		break;
	case e1000_fc_tx_pause:
		an |= E1000_TXCW_ASM_DIR;
		break;
	default:
		break;
	}
	wr32(E1000_PCS_ANADV, an);
	vcdbg("an 0x%x\n", an);

	wr32(E1000_PCS_LCTL, lctl);
	vcdbg("lctl 0x%x\n", lctl);

	ctrl = rd32(E1000_PCS_LSTAT);
	vcdbg("lsts 0x%x\n", ctrl);

	return(E1000_SUCCESS);
}

// enable forwarding on port;

static s32 igb_vc_m88sw_port_fwd(struct e1000_hw *hw, u8 port){
	s32 ret;
	u16 ctrl;

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(port, M88_SW_PORT_CONTROL), &ctrl);
	if(ret)
		goto out;
	ctrl |= M88_SW_PORT_CONTROL_FWD;
	ret = igb_vc_mdio_write(hw,
		M88_SW_PORT_REG(port, M88_SW_PORT_CONTROL), ctrl);
out:
	return(ret);
}

// setup link to a Marvell switch 88e6320;
// switch is used as dual copper PHY;

static s32 igb_vc_setup_link_m88e6320(struct e1000_hw *hw){
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret;
	u16 fid, old, val;

	// setup the sgmii link;

	ret = igb_vc_setup_sgmii_link_forced(hw);
	if(ret)
		goto out;

	// the NO_CPU pin is pulled low to disable switch after reset,
	// to make sure no packets flow between WAN ports;
	// reset the copper and serdes PHYs;
	// this takes them out of power-down mode;

	ret = phy->ops.reset(hw); 
	if(ret)
		goto out;

	// manage 88e6320 analog VDD for copper side drivers;
	// unfortunately, this is also the vdd for the sgmii links;
	// t-mark certification requires certain limits;

	if(avdd & AVDD_6320_SET) {
		// set pointer;
		ret = igb_vc_mdio_write(hw,
			M88_SW_REG(M88_SW_GLOBAL2, M88_GL2_SRATCH_MISC),
			0x0600);
		if(ret)
			goto out;
		// read old;
		ret = igb_vc_mdio_read(hw,
			M88_SW_REG(M88_SW_GLOBAL2, M88_GL2_SRATCH_MISC),
			&old);
		if(ret)
			goto out;
		val = avdd & AVDD_6320_MASK;
		switch(val) {
		case AVDD_6320_1_35V:
		case AVDD_6320_1_40V:
		case AVDD_6320_1_45V:
		case AVDD_6320_1_50V:
			val = 0x8600 | (val << 4) | (old & 0xf);
			break;
		default:
			pr_err("igb: illegal avdd parameter: 0x%x\n", avdd);
		}
		if(val) {
			ret = igb_vc_mdio_write(hw,
				M88_SW_REG(M88_SW_GLOBAL2, M88_GL2_SRATCH_MISC),
				val);
			if(ret)
				goto out;
		}
	}

	// setup port-based routing;
	// the two WAN ports are different networks;
	// cpu can send to phy port;
	// phy can send to cpu port;
	// set different filter ID per vlan for MAC addr handling;

	fid = hw->bus.func << 12;

	ret = igb_vc_mdio_write(hw,
		M88_SW_PORT_REG(phy->ports[e1000_port_cpu], M88_SW_PORT_VLAN_MAP),
		fid | (1 << phy->ports[e1000_port_phy]));
	if(ret)
		goto out;
	ret = igb_vc_mdio_write(hw,
		M88_SW_PORT_REG(phy->ports[e1000_port_phy], M88_SW_PORT_VLAN_MAP),
		fid | (1 << phy->ports[e1000_port_cpu]));
	if(ret)
		goto out;

	// above reset took the PHYs out of power-down;
	// the ports are still disabled, so enable forwarding;

	ret = igb_vc_m88sw_port_fwd(hw, phy->ports[e1000_port_cpu]);
	if(ret)
		goto out;
	ret = igb_vc_m88sw_port_fwd(hw, phy->ports[e1000_port_phy]);
	if(ret)
		goto out;

#ifdef IGB_VC_DUMP_6320
{
	u32 ctrl;

	//igb_dump_sw(hw, M88_SW_GLOBAL1);
	igb_dump_sw(hw, M88_SW_PORT_BASE + phy->ports[e1000_port_cpu]);
	igb_dump_swphy(hw, (1 << 16) | (phy->ports[e1000_port_sgmii] << 5));
	igb_dump_sw(hw, M88_SW_PORT_BASE + phy->ports[e1000_port_phy]);
	igb_dump_swphy(hw, (0 << 16) | (phy->ports[e1000_port_phy] << 5));

	ctrl = rd32(E1000_STATUS);
	printk("%s status 0x%x\n", __func__, ctrl);
}
#endif

out:
	vcdbg("ret %d\n", ret);
	return(ret);
}

// setup link to a Marvell switch 88e6176;
// switch is a SGMII attached switch to 4 copper PHYs;

static s32 igb_vc_setup_link_m88e6176(struct e1000_hw *hw){
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret;
	u16 data;

	// setup the sgmii link;

	ret = igb_vc_setup_sgmii_link_forced(hw);
	if(ret)
		goto out;

	// reset the serdes PHYs;

	ret = phy->ops.reset(hw); 
	if(ret)
		goto out;

	// clear the sw port PHYDetect bit;

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(phy->ports[e1000_port_cpu], M88_SW_PORT_STATUS),
		&data);
	if(ret)
		goto out;
	data &= ~M88_SW_PORT_STATUS_PHYDET;
	ret = igb_vc_mdio_write(hw,
		M88_SW_PORT_REG(phy->ports[e1000_port_cpu], M88_SW_PORT_STATUS),
		data);
	if(ret)
		goto out;

#ifdef IGB_VC_DUMP_6176
{
	u32 ctrl;

	//igb_dump_sw(hw, M88_SW_GLOBAL1);
	igb_dump_sw(hw, M88_SW_PORT_BASE + 4);
	igb_dump_swphy(hw, (1 << 16) | (0xf << 5));
	igb_dump_swphy(hw, (0 << 16) | (0 << 5));
	igb_dump_swphy(hw, (0 << 16) | (1 << 5));
	igb_dump_swphy(hw, (0 << 16) | (2 << 5));
	igb_dump_swphy(hw, (0 << 16) | (3 << 5));

	ctrl = rd32(E1000_STATUS);
	printk("%s status 0x%x\n", __func__, ctrl);
}
#endif

out:
	vcdbg("ret %d\n", ret);
	return(ret);
}

// setup link to a Marvell switch 88e1112;
// PHY to SFP cage, which is always powered;

static s32 igb_vc_setup_link_m88e1112(struct e1000_hw *hw){
	u16 data;
	s32 ret;
	struct e1000_mac_info *mac = &hw->mac;

	// setup the sgmii link;

	ret = igb_vc_setup_sgmii_link_aneg(hw);
	if(ret)
		goto out;

	// set polarity of SIGDET;
	// this is retained across a sw reset;

	ret = igb_vc_m88phy_sfp_mdio_read(hw, M88E1000_PHY_SPEC_CTRL, &data);
	if(ret)
		goto out;
	data |= M88E1000_SPEC_CTRL_SIGDET;
	ret = igb_vc_m88phy_sfp_mdio_write(hw, M88E1000_PHY_SPEC_CTRL, data);
	if(ret)
		goto out;

	// reset the PHYs;

	ret = hw->phy.ops.reset(hw); 
	if(ret)
		goto out;

	// set sgmii mode;
	// after reset, as it is not retained;

	ret = igb_vc_m88phy_sgmii_mdio_read(hw, M88E1000_PHY_SPEC_CTRL, &data);
	if(ret)
		goto out;
	data &= ~(M88E1000_SPEC_CTRL_PREFER
		| M88E1000_SPEC_CTRL_MODE);
	if(mac->forced_speed_duplex & (E1000_ALL_100_SPEED | E1000_ALL_10_SPEED))
		data |= M88E1000_SPEC_CTRL_MODE_100BASEFX;
	else
		data |= M88E1000_SPEC_CTRL_MODE_1000BASEX_ONLY;
	data |= (M88E1000_SPEC_CTRL_PWR_UP | M88E1000_SPEC_CTRL_ENH_SGMII);
	ret = igb_vc_m88phy_sgmii_mdio_write(hw, M88E1000_PHY_SPEC_CTRL, data);
	if(ret)
		goto out;

#ifdef IGB_VC_DUMP_1112
{
	u32 ctrl;

	igb_dump_phy(hw, 2);
	igb_dump_phy(hw, 1);

	ctrl = rd32(E1000_PCS_LCTL);
	printk("%s lctl 0x%x\n", __func__, ctrl);
	ctrl = rd32(E1000_PCS_LSTAT);
	printk("%s lsts 0x%x\n", __func__, ctrl);
	ctrl = rd32(E1000_STATUS);
	printk("%s status 0x%x\n", __func__, ctrl);
}
#endif

out:
	vcdbg("ret %d\n", ret);
	return(ret);
}

// setup mode for m88e1514;

static s32 igb_vc_setup_m88e1514(struct e1000_hw *hw){
	s32 ret;
	u16 page, ctrl;


	// save page;

	ret = hw->phy.ops.read_reg(hw, M88E1000_PHY_PAGE22, &page);
	if(ret)
		goto fail;

	// LED configuration;
	// LED0 is orange for gigabit link;
	// LED1 is green for 10/100 and activity;

	ret = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE22, 3);
	if(ret)
		goto fail;
	ret = hw->phy.ops.write_reg(hw, 16, 0x0817);
	if(ret)
		goto fail;
	ret = hw->phy.ops.write_reg(hw, 17, 0x0005);
	if(ret)
		goto fail;

	// page 18, general ctrl register;
	// set sgmii system mode;

	ret = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE22, 18);
	if(ret)
		goto fail;
	ret = hw->phy.ops.read_reg(hw, 20, &ctrl);
	if(ret)
		goto fail;
	ctrl &= 0x0380;
	ctrl |= 0x8011;
	ret = hw->phy.ops.write_reg(hw, 20, ctrl);
	if(ret)
		goto fail;

	// restore page;

	ret = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE22, page);
fail:
	vcdbg("ret %d\n", ret);
	return(ret);
}

// setup m88e1514 link;

static s32 igb_vc_setup_link_m88e1514(struct e1000_hw *hw){
	s32 ret;

	ret = igb_vc_setup_m88e1514(hw);
	if(ret)
		goto fail;

	ret = igb_setup_copper_link_82575(hw);

#ifdef IGB_VC_DUMP_1514
{
	u32 ctrl;

	igb_dump_phy_bb(hw, 0);
	igb_dump_phy_bb(hw, 1);

	ctrl = rd32(E1000_PCS_LCTL);
	printk("%s lctl 0x%x\n", __func__, ctrl);
	ctrl = rd32(E1000_PCS_LSTAT);
	printk("%s lsts 0x%x\n", __func__, ctrl);
	ctrl = rd32(E1000_STATUS);
	printk("%s status 0x%x\n", __func__, ctrl);
}

#endif
fail:
	vcdbg("ret %d\n", ret);
	return(ret);
}

// get phy info of Marvell switch 88e6176;
// the switch is SGMII attached, so there isn't really a PHY;
// return the info from the port registers;

static s32 igb_vc_m88sw_get_phy_info(struct e1000_hw *hw){
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret;
	u16 status;

	// read switch's port status;

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(phy->ports[e1000_port_cpu], M88_SW_PORT_STATUS),
		&status);
	vcdbg("ret %d\n", ret);
	if(ret)
		goto out;

	phy->is_mdix = false;
	if(status & M88_SW_PORT_STATUS_LINK_UP) {
		phy->local_rx = e1000_1000t_rx_status_ok;
		phy->remote_rx = e1000_1000t_rx_status_ok;
	} else {
		phy->local_rx = e1000_1000t_rx_status_not_ok;
		phy->remote_rx = e1000_1000t_rx_status_not_ok;
	}

out:
	return(ret);
}

// check that sgmii link is up;
// returns <0 for error, 0 no link, 1 has link;

static s32 igb_vc_sgmii_check_for_link(struct e1000_hw *hw){
	s32 ret;
	u16 speed, duplex;

	// need to check the PCS, not a PHY;

	ret = igb_get_pcs_speed_and_duplex_82575(hw, &speed, &duplex);
	if(ret)
		goto out;

#if 0 //XXX
                /* Configure Flow Control now that Auto-Neg has completed.
                 * First, we need to restore the desired flow control
                 * settings because we may have had to re-autoneg with a
                 * different link partner.
                 */
                ret_val = igb_config_fc_after_link_up(hw);
                if (ret_val)
                        hw_dbg("Error configuring flow control\n");
#endif

	ret = hw->mac.serdes_has_link;
	vcdbg("serdes %d\n", ret);

out:
	return(ret);
}

// check for link to a marvell switch;
// only checks for the sgmii link to the switch,
// not any media connections on other switch ports;
// for m88e6176, MAC interrupt turns on link checking when sgmii is down;

static s32 igb_vc_m88sw_check_for_link(struct e1000_hw *hw){
	s32 ret = 0;
	u16 portsts;

	// only check link when we want;

	if( !hw->mac.get_link_status)
		goto out;

	// check if sgmii link is up;

	ret = igb_vc_sgmii_check_for_link(hw);
	if(ret <= 0)
		goto out;

	// read switch port status register;

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(hw->phy.ports[e1000_port_cpu], M88_SW_PORT_STATUS),
		&portsts);
	vcdbg("port status %d 0x%x\n", ret, portsts);
	if(ret)
		goto out;

	if( !(portsts & M88_SW_PORT_STATUS_LINK_UP))
		goto out;
	hw->mac.get_link_status = false;

out:
	return(ret);
}

// check for link on m88e6320;
// need to always check the copper PHYs, because we have no intr from the chip;
// then check the sgmii link, only if link is down;

static s32 igb_vc_m88sw_check_for_link_m88e6320(struct e1000_hw *hw){
	s32 ret;
	u16 portsts;

	// always check port status;

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(hw->phy.ports[e1000_port_phy], M88_SW_PORT_STATUS),
		&portsts);
	vcdbg("port status %d 0x%x\n", ret, portsts);
	if(ret)
		goto out;

	// re-poll when copper link is down;

	if( !(portsts & M88_SW_PORT_STATUS_LINK_UP)) {
		hw->mac.get_link_status = true;
		goto out;
	}

	// now, check sgmii;

	ret = igb_vc_m88sw_check_for_link(hw);
out:
	return(ret);
}

// read a block of bytes from sfp i2c;

static int igb_vc_i2c_read(struct i2c_client *client, u32 off, u32 len, u8 *buf){
	int ret = 0;

	for(; len; off++, len--) {
		ret = i2c_smbus_read_byte_data(client, off);
		if(ret < 0)
			break;
		*buf++ = (u8)ret;
	}
	return((ret < 0)? ret : 0);
}

// strip trailing spaces from strings;

static void igb_vc_strip_tspc(u32 len, u8 *str){
	u8 *s = str + len;

	for(*s = 0; len-- && (*--s == ' '); *s = 0);
}

// probe sfp cage i2c;
// called for each i2c device after detecting, by the i2c core;
// much of the probing has been done in detect;

static int igb_vc_sfp_probe(struct e1000_hw *hw){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	struct i2c_client *client = sfp->client[SFP_EEPROM];
	struct igb_adapter *adapter = sfp->adapter;
	struct pci_dev *pdev = adapter->pdev;
	int ret;

	// re-probe i2c if no device detected yet;

	sfp->plugged = false;
	if( !client) {
		igb_vc_i2c_exit(adapter);
		ret = igb_vc_i2c_init(adapter);
		if(ret < 0)
			return(ret);
		client = sfp->client[SFP_EEPROM];
	}
	if( !client)
		return(-ENODEV);

	// read sfp/sff identifier;

	ret = i2c_smbus_read_byte_data(client, SFP_OFF_IDENTIFIER);
	if(ret < 0)
		return(ret);
	if(ret != SFP_IDENTIFIER_SFP) {
		dev_err(&pdev->dev, "non-SFP identifier: 0x%x\n", ret);
		return(-ENODEV);
	}

	// read vendor/oui/part;

	ret = igb_vc_i2c_read(client, SFP_OFF_VENDOR, SFP_SIZE_VENDOR, sfp->vendor);
	if(ret < 0)
		goto out;
	ret = igb_vc_i2c_read(client, SFP_OFF_OUI, SFP_SIZE_OUI, sfp->oui);
	if(ret < 0)
		goto out;
	ret = igb_vc_i2c_read(client, SFP_OFF_PARTNUM, SFP_SIZE_PARTNUM, sfp->partnum);
	if(ret < 0)
		goto out;

	// log sfp module info;

	igb_vc_strip_tspc(SFP_SIZE_VENDOR, sfp->vendor);
	igb_vc_strip_tspc(SFP_SIZE_PARTNUM, sfp->partnum);
	dev_info(&pdev->dev, "sfp vendor=%s oui=%02x:%02x:%02x part=%s\n",
		sfp->vendor,
		sfp->oui[0], sfp->oui[1], sfp->oui[2],
		sfp->partnum);
	sfp->plugged = true;
	ret = 0;
out:
	return(ret);
}

// check for link on m88e1112 SFP;

static s32 igb_vc_m88phy_check_for_link_m88e1112(struct e1000_hw *hw){
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;
	s32 ret;
	u16 status;

	// check if sfp side has link;
	// some PHYs require double read to update;
	// force re-probing of sfp i2c when link is down;

	igb_vc_m88phy_sfp_mdio_read(hw, PHY_STATUS, &status);
	ret = igb_vc_m88phy_sfp_mdio_read(hw, PHY_STATUS, &status);
	if(ret)
		goto out;
	if( !(status & MII_SR_LINK_STATUS)) {
		hw->mac.get_link_status = true;
		dev_spec->module_plugged = false;
		goto out;
	}

	// check sfp i2c;
	// do this after sfp phy is up, so something must be plugged in;

	if(dev_spec->module_plugged == false) {
		ret = igb_vc_sfp_probe(hw);
		if(ret) {
			ret = -E1000_ERR_I2C;
			goto out;
		}
		dev_spec->module_plugged = true;
	}

	// check if sgmii link is up;

	ret = igb_vc_sgmii_check_for_link(hw);
	if(ret <= 0)
		goto out;

	hw->mac.get_link_status = false;
	ret = E1000_SUCCESS;
out:
	return(ret);
}

// dummy check polarity;

static s32 igb_vc_dummy_check_polarity(struct e1000_hw *hw){
	return(E1000_SUCCESS);
}

// dummy get cable length;

static s32 igb_vc_dummy_get_cable_length(struct e1000_hw *hw){
	hw->phy.cable_length = 1;
	return(E1000_SUCCESS);
}

// bug on force speed/duplex;

static s32 igb_vc_bugon_phy_force_speed_duplex(struct e1000_hw *hw){
	BUG_ON(1);
	return(-E1000_ERR_PHY);
}

// get speed and duplex for m88e6320 copper ports;
// only get link stat from phy, all others come from igb;

static s32 igb_m88sw_get_speed_and_duplex(struct e1000_hw *hw, u16 *speed, u16 *duplex){
	s32 ret;
	u16 sts;

	*speed = -1;
	*duplex = -1;

	// read port status;

	ret = igb_vc_mdio_read(hw,
		M88_SW_PORT_REG(hw->phy.ports[e1000_port_phy], M88_SW_PORT_STATUS),
		&sts);
	vcdbg("port status %d 0x%x\n", ret, sts);
	if(ret)
		goto out;
	ret = 0;
	if( !(sts & M88_SW_PORT_STATUS_LINK_UP))
		goto out;

	switch(sts & M88_SW_PORT_STATUS_SPEED) {
	case M88_SW_PORT_STATUS_SPEED_10:
		*speed = SPEED_10;
		break;
	case M88_SW_PORT_STATUS_SPEED_100:
		*speed = SPEED_100;
		break;
	case M88_SW_PORT_STATUS_SPEED_1000:
		*speed = SPEED_1000;
		break;
	}
	if(sts & M88_SW_PORT_STATUS_DUPLEX)
		*duplex = FULL_DUPLEX;
	else
		*duplex = HALF_DUPLEX;
	ret = sts;
out:
	return(ret);
}

// ethtool get interfaces;

int igb_m88sw_get_settings(struct igb_adapter *adapter, struct ethtool_cmd *ecmd){
	struct e1000_hw *hw = &adapter->hw;
	s32 ret;
	u16 speed, duplex;

	// set fixed values;

	ecmd->supported = hw->swphy.autoneg_supported;
	ecmd->advertising = hw->swphy.autoneg_advertised;
	if(hw->swphy.autoneg)
		ecmd->advertising |= ADVERTISED_Autoneg;

	ecmd->port = PORT_TP;
	ecmd->phy_address = hw->phy.addr;
	ecmd->transceiver = XCVR_INTERNAL;

	ecmd->speed = -1;
	ecmd->duplex = -1;
	ecmd->autoneg = AUTONEG_DISABLE;

	// get speed and duplex;

	ret = igb_m88sw_get_speed_and_duplex(hw, &speed, &duplex);
	if(ret < 0)
		goto out;
	ethtool_cmd_speed_set(ecmd, speed);
	if(duplex == FULL_DUPLEX)
		ecmd->duplex = DUPLEX_FULL;
	else if(duplex == HALF_DUPLEX)
		ecmd->duplex = DUPLEX_HALF;

	if(hw->swphy.autoneg)
		ecmd->autoneg = AUTONEG_ENABLE;

	return(0);

out:
	return(-EIO);
}

// ethtool set interfaces;
// only set link modes of phy;

int igb_m88sw_set_settings(struct igb_adapter *adapter, struct ethtool_cmd *ecmd){
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	s32 ret;
	u32 speed;

	// check valid configs;

	if(ecmd->advertising & ~hw->swphy.autoneg_supported) {
		dev_err(&pdev->dev, "Unsupported Advertise Modes (supported 0x%x)\n", hw->swphy.autoneg_supported);
		goto inval;
	}
	if(ecmd->duplex & ~DUPLEX_FULL) {
		dev_err(&pdev->dev, "Unsupported Duplex Mode\n");
		goto inval;
	}

	if(ecmd->autoneg == AUTONEG_ENABLE) {
		hw->swphy.autoneg = 1;
		hw->swphy.autoneg_advertised = ADVERTISED_TP
			| ADVERTISED_Autoneg | ecmd->advertising;
		ecmd->advertising = hw->swphy.autoneg_advertised;
	} else {
		speed = ethtool_cmd_speed(ecmd);
		switch(speed + ecmd->duplex) {
		case SPEED_10 + DUPLEX_HALF:
		case SPEED_10 + DUPLEX_FULL:
		case SPEED_100 + DUPLEX_HALF:
		case SPEED_100 + DUPLEX_FULL:
		case SPEED_1000 + DUPLEX_FULL:
			break;
		default:
			dev_err(&pdev->dev, "Unsupported Speed/Duplex Mode\n");
			goto inval;
		}
		hw->swphy.autoneg = 0;
		hw->swphy.speed = speed;
		hw->swphy.duplex = ecmd->duplex;
	}

	// force the switch port down;
	// reset the copper PHY;
	// unforce the switch port;

	ret = igb_vc_m88sw_port_force(hw, IGB_SW_PORT_DOWN);
	if(ret)
		goto out;
	msleep(1);
	ret = igb_vc_m88sw_phy_reset(hw);
	if(ret)
		goto out;
	msleep(1);
	ret = igb_vc_m88sw_port_force(hw, IGB_SW_PORT_UNFORCE);
	if(ret)
		goto out;
	return(0);
inval:
	return(-EINVAL);
out:
	return(-EIO);
}

// probe for velocloud board;
// using eeprom customer words 0x6/0x7;

#define VC_ID 0x5663	// "Vc"
#define VC_ID_EDGE500 0x6535 // "e5" - edge500;
#define VC_ID_EDGE5X0 0x3558 // "5X" - edge520/edge540;
#define VC_ID_EDGE510 0x3531 // "51" - edge510;

s32 igb_vc_probe(struct e1000_hw *hw){
	struct igb_adapter *adapter = hw->back;
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_phy_info *phy = &hw->phy;
	struct igb_vc_info *info = NULL;
	s32 ret = -E1000_ERR_PHY;
	u16 eeprom_offset, eeprom[2];

	// only bother about Rangeley MACs;
	// only support is for SGMII attached with MDIO;

	if(hw->mac.type != e1000_i354)
		goto out;
	if( !igb_sgmii_active_82575(hw))
		goto out;
	if( !igb_sgmii_uses_mdio_82575(hw))
		goto out;

	// do all the work of init_phy_param() here;

	hw->bus.func = (rd32(E1000_STATUS) & E1000_STATUS_FUNC_MASK) >> E1000_STATUS_FUNC_SHIFT;

	// check eeprom customer words;

	eeprom[0] = eeprom[1] = 0;
	hw->nvm.ops.read(hw, 0x06, 2, eeprom);
	if(eeprom[0] != VC_ID)
		goto out;

	switch(eeprom[1]) {
	case VC_ID_EDGE500:
		hw->vc_id = e1000_vc500;
		info = igb_vc500_info;
		break;
	case VC_ID_EDGE5X0:
		hw->vc_id = e1000_vc5x0;
		info = igb_vc5x0_info;

		if (hw->bus.func < E1000_FUNC_2)
			igb_vc5x0_reset(hw);
		else if (! vc_5x0_igb_inmem_reset[hw->bus.func]) {
			igb_vc5x0_reset(hw);
			vc_5x0_igb_inmem_reset[hw->bus.func] = 1;
		}

		break;
	case VC_ID_EDGE510:
		ret = igb_init_phy_params_82575(hw);
		goto out;
	default:
		goto out;
	}
	BUG_ON(info == NULL);
	dev_info(&adapter->pdev->dev, "found custom link: 0x%x\n", eeprom[1]);

	info += hw->bus.func;
	phy->ports[e1000_port_cpu] = info->ports[e1000_port_cpu];
	phy->ports[e1000_port_sgmii] = info->ports[e1000_port_sgmii];
	phy->ports[e1000_port_phy] = info->ports[e1000_port_phy];

	// get PHY address from EEPROM;
	// i354 reset does not re-read from EEPROM and caches
	// last value written before reset.  Always read from EEPROM
	// during device init; XXX Fixme: Investigate bit to make it possible
	// to read from EEPROM on soft reset.
	switch (hw->bus.func) {

	case E1000_FUNC_0:
		eeprom_offset = NVM_LAN_PORT_0_BAR + NVM_INIT_CTRL_4;
		break;
	case E1000_FUNC_1:
		eeprom_offset = NVM_LAN_PORT_1_BAR + NVM_INIT_CTRL_4;
		break;
	case E1000_FUNC_2:
		eeprom_offset = NVM_LAN_PORT_2_BAR + NVM_INIT_CTRL_4;
		break;
	case E1000_FUNC_3:
		eeprom_offset = NVM_LAN_PORT_3_BAR + NVM_INIT_CTRL_4;
		break;
	default:
		goto out;
	}
	ret = hw->nvm.ops.read(hw, eeprom_offset, 1, eeprom);
	if (ret > 0) {
		dev_err(&adapter->pdev->dev, 
			"error reading offset: 0x%x into EEPROM\n", eeprom_offset);
		goto out;
	}
	/* bits 1:5 contain addr at offset 0x13. */
	phy->addr = (eeprom[0] >> 1) & 0x1f;

	// assign alternate MDIO read/write functions;
	// needed for bitbang MDIO on edge5x0;

	if(info->owr)
		phy->addr = phy->ports[info->owr];
	if(info->read_reg)
		phy->ops.read_reg = info->read_reg;
	if(info->write_reg)
		phy->ops.write_reg = info->write_reg;

	// get the switch/PHY IDs;
	ret = info->getid(hw);
	vcdbg("id 0x%x rev 0x%x ret %d\n", phy->id, phy->revision, ret);
	if(ret)
		goto out;

	// set phy handlers;

	phy->reset_delay_us = 100;

	switch(phy->id) {

	// edge500 only;
	case M88E6320_ID:
		mac->autoneg = false;
		mac->ops.setup_physical_interface = igb_vc_setup_link_m88e6320;
		mac->ops.check_for_link = igb_vc_m88sw_check_for_link_m88e6320;
		mac->ops.get_speed_and_duplex = igb_m88sw_get_speed_and_duplex;

		phy->media_type = e1000_media_type_switch;
		phy->autoneg_mask = AUTONEG_ADVERTISE_SPEED_DEFAULT;
		phy->type = e1000_phy_m88sw;

		phy->ops.read_reg = igb_vc_m88sw_phy_mdio_read;
		phy->ops.write_reg = igb_vc_m88sw_phy_mdio_write;
		phy->ops.reset = igb_vc_m88sw_port_reset;
		phy->ops.check_polarity = igb_check_polarity_m88;
		phy->ops.get_cable_length = igb_get_cable_length_m88_gen2;
		phy->ops.force_speed_duplex = igb_vc_bugon_phy_force_speed_duplex;
		phy->ops.get_phy_info = igb_vc_m88sw_get_phy_info;

		// init switch-as-phy;

		hw->swphy.autoneg = 1;
		hw->swphy.autoneg_supported = SUPPORTED_M88E6320;
		hw->swphy.autoneg_advertised = ADVERTISED_TP
			| ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full
			| ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full
			| ADVERTISED_1000baseT_Full;
		hw->swphy.speed = SPEED_1000;
		hw->swphy.duplex = FULL_DUPLEX;
		break;

	// edge500 one switch;
	// edge520/540 two switches;
	case M88E6176_ID:
		mac->autoneg = false;
		mac->ops.setup_physical_interface = igb_vc_setup_link_m88e6176;
		mac->ops.check_for_link = igb_vc_m88sw_check_for_link;
		mac->ops.get_speed_and_duplex = igb_get_pcs_speed_and_duplex_82575;

		phy->media_type = e1000_media_type_switch;
		phy->autoneg_mask = 0;
		phy->type = e1000_phy_m88sw;

		phy->ops.read_reg = igb_vc_m88sw_sgmii_mdio_read;
		phy->ops.write_reg = igb_vc_m88sw_sgmii_mdio_write;
		phy->ops.reset = igb_vc_m88sw_sgmii_reset;
		phy->ops.check_polarity = igb_vc_dummy_check_polarity;
		phy->ops.get_cable_length = igb_vc_dummy_get_cable_length;
		phy->ops.force_speed_duplex = igb_vc_bugon_phy_force_speed_duplex;
		phy->ops.get_phy_info = NULL;
		break;

	// edge500 only, sfp;
	case M88E1112_ID:
		mac->autoneg = true;
		mac->ops.setup_physical_interface = igb_vc_setup_link_m88e1112;
		mac->ops.check_for_link = igb_vc_m88phy_check_for_link_m88e1112;
		mac->ops.get_speed_and_duplex = igb_get_pcs_speed_and_duplex_82575;

		phy->media_type = e1000_media_type_sfp;
		phy->autoneg_mask = AUTONEG_ADVERTISE_SPEED_DEFAULT;
		phy->type = e1000_phy_m88;

		phy->ops.read_reg = igb_vc_m88phy_sfp_mdio_read;
		phy->ops.write_reg = igb_vc_m88phy_sfp_mdio_write;
		phy->ops.reset = igb_vc_m88phy_sfp_reset;
		phy->ops.check_polarity = NULL;
		phy->ops.get_cable_length = NULL;
		phy->ops.force_speed_duplex = igb_vc_bugon_phy_force_speed_duplex;
		phy->ops.get_phy_info = NULL;
		break;

	// edge520/540 only;
	// cannot use get_phy_info on mdiobb;
	case M88E1514_ID:
		mac->autoneg = true;
		mac->ops.setup_physical_interface = igb_vc_setup_link_m88e1514;
		mac->ops.check_for_link = igb_check_for_link_82575;
		mac->ops.get_speed_and_duplex = igb_get_pcs_speed_and_duplex_82575;

		phy->media_type = e1000_media_type_copper;
		phy->autoneg_mask = AUTONEG_ADVERTISE_SPEED_DEFAULT;
		phy->type = e1000_phy_m88;

		phy->ops.read_reg = igb_vc_mdiobb_read;
		phy->ops.write_reg = igb_vc_mdiobb_write;
		phy->ops.reset = igb_phy_hw_reset_sgmii_82575;
		phy->ops.check_polarity = igb_check_polarity_m88;
		phy->ops.get_cable_length = igb_get_cable_length_m88_gen2;
		phy->ops.force_speed_duplex = igb_phy_force_speed_duplex_m88;
		phy->ops.get_phy_info = NULL;
		break;

	default:
		ret = -E1000_ERR_PHY;
	}
out:
	return(ret);
}

// show sfp attributes;

static ssize_t igb_vc_sfp_vendor_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	ssize_t ret = -ENODEV;

	if(sfp->client[SFP_EEPROM] && sfp->plugged)
		ret = scnprintf(buf, PAGE_SIZE, "%s\n", sfp->vendor);
	return(ret);
}

static ssize_t igb_vc_sfp_oui_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	ssize_t ret = -ENODEV;

	if(sfp->client[SFP_EEPROM] && sfp->plugged)
		ret = scnprintf(buf, PAGE_SIZE, "%02x:%02x:%02x\n", sfp->oui[0], sfp->oui[1], sfp->oui[2]);
	return(ret);
}

static ssize_t igb_vc_sfp_partnum_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	ssize_t ret = -ENODEV;

	if(sfp->client[SFP_EEPROM] && sfp->plugged)
		ret = scnprintf(buf, PAGE_SIZE, "%s\n", sfp->partnum);
	return(ret);
}

// all sfp attributes;

static DEVICE_ATTR(vendor, S_IRUGO, igb_vc_sfp_vendor_show, NULL);
static DEVICE_ATTR(oui, S_IRUGO, igb_vc_sfp_oui_show, NULL);
static DEVICE_ATTR(partnum, S_IRUGO, igb_vc_sfp_partnum_show, NULL);

static struct attribute *igb_vc_sfp_attrs[] = {
	&dev_attr_vendor.attr,
	&dev_attr_oui.attr,
	&dev_attr_partnum.attr,
	NULL,
};

static struct attribute_group igb_vc_sfp_group = {
	.name = IGB_VC_SFP,
	.attrs = igb_vc_sfp_attrs,
};

// detect i2c devices;
// called for each i2c device before probing, by the i2c core;
// device addresses are in struct i2c_driver.address_list;

static int igb_vc_i2c_detect(struct i2c_client *client, struct i2c_board_info *info){
	struct i2c_adapter *adapter = client->adapter;

	if( !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
		| I2C_FUNC_SMBUS_WRITE_BYTE)) {
		pr_err("igb: i2c adaptor does not support required smbus/i2c modes\n");
		return(-ENODEV);
	}

	// success;

	strlcpy(info->type, IGB_VC_SFP, I2C_NAME_SIZE);
	return(0);
}

// probe sfp cage i2c;
// called for each i2c device after detecting, by the i2c core;
// the sfp may not be plugegd in yet, not much to do here;

static int igb_vc_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	struct igb_adapter *adapter = sfp->adapter;
	struct pci_dev *pdev = adapter->pdev;
	int ret = -ENODEV;

	// EEPROM probe;
	// create sysfs entries for sfp info;

	if(client->addr == igb_vc_i2c_addrs[0]) {
		sfp->client[SFP_EEPROM] = client;
		ret = sysfs_create_group(&pdev->dev.kobj, &igb_vc_sfp_group);
		if(ret < 0)
			dev_err(&pdev->dev, "couldn't register sfp sysfs group\n");
	}

	// DMI probe;

	if(client->addr == igb_vc_i2c_addrs[1]) {
		sfp->client[SFP_DMI] = client;
		ret = 0;
	}

	return(ret);
}

// remove driver;

static int igb_vc_i2c_remove(struct i2c_client *client){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	struct igb_adapter *adapter = sfp->adapter;

	// remove sysfs entries;

	sysfs_remove_group(&adapter->pdev->dev.kobj, &igb_vc_sfp_group);

	sfp->client[SFP_EEPROM] = NULL;
	sfp->client[SFP_DMI] = NULL;
	return(0);
}

// i2c device id;

static const struct i2c_device_id igb_vc_i2c_id[] = {
	{ IGB_VC_SFP, 0 },
	{},
};

// i2c driver struct;
// can't have module_i2c_driver(vc_i2c_driver);

static struct i2c_driver igb_vc_i2c_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = IGB_VC_SFP,
	},
	.address_list = igb_vc_i2c_addrs,
	.probe = igb_vc_i2c_probe,
	.remove = igb_vc_i2c_remove,
	.id_table = igb_vc_i2c_id,
	.detect = igb_vc_i2c_detect,
};

// init i2c to sfp;
// sfp is on port 3;
// only on edge500;

s32 igb_vc_i2c_init(struct igb_adapter *adapter){
	struct vc_i2c_sfp *sfp = &igb_vc_i2c_sfp;
	struct e1000_hw *hw = &adapter->hw;
	int ret;

	if(hw->vc_id != e1000_vc500)
		return(0);

	if(hw->bus.func != IGB_VC_SFP_BUS_FUNC)
		return(0);

	sfp->adapter = adapter;

	ret = i2c_add_driver(&igb_vc_i2c_driver);
	return(ret);
}

// free i2c to sfp;
// only on edge500;

void igb_vc_i2c_exit(struct igb_adapter *adapter){
	struct e1000_hw *hw = &adapter->hw;

	if(hw->vc_id != e1000_vc500)
		return;

	if(hw->bus.func != IGB_VC_SFP_BUS_FUNC)
		return;

	i2c_del_driver(&igb_vc_i2c_driver);
}

// dsa interface to m88e7176 switch;
// cannot use mv88e6xxx driver as mdio bus access, mdio locking,
// and m88e6176 phy register access are different;
// much code copied from the marvell driver;

#define REG_DIRECT	0x10
#define REG_PORT(p)	(REG_DIRECT + (p))
#define REG_GLOBAL	0x1b
#define REG_GLOBAL2	0x1c

struct m88e6176_priv {
	struct mutex stats;	// serialize stats access;
	int id;			// switch id;
};

struct m88e6176_hw_stat {
	char string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int reg;
};

// dsa mdio read;
static int igb_vc_dsa_read(struct mii_bus *bus, int addr, int reg){
	struct vc_dsa_priv *priv = bus->priv;
	struct e1000_hw *hw = priv->hw;
	int adreg = (addr << 5) | reg;
	u16 data;
	int ret;

	if(addr >= REG_DIRECT)
		ret = igb_vc_mdio_read(hw, adreg, &data);
	else
		ret = igb_vc_smi_mdio_read(hw, adreg, &data);
	//printk("%s: %d phy %d reg %d data 0x%x\n", __func__, ret, addr, reg, data);
	return(ret? ret : data);
}

// dsa mdio write;

static int igb_vc_dsa_write(struct mii_bus *bus, int addr, int reg, u16 val){
	struct vc_dsa_priv *priv = bus->priv;
	struct e1000_hw *hw = priv->hw;
	int adreg = (addr << 5) | reg;
	int ret;

	if(addr >= REG_DIRECT)
		ret = igb_vc_mdio_write(hw, adreg, val);
	else
		ret = igb_vc_smi_mdio_write(hw, adreg, val);
	//printk("%s: %d phy %d reg %d data 0x%x\n", __func__, ret, addr, reg, val);
	return(ret);
}

// init dsa interface;

s32 igb_vc_dsa_init(struct igb_adapter *adapter){
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct mii_bus *bus;
	struct vc_dsa_priv *priv;
	struct platform_device *pdev;
	struct dsa_platform_data *dpd;
	struct dsa_chip_data *dcd;
	s32 ret;
	int i;

	if( !(dsa & DSA_ENABLE))
		return(0);

	// get per-platform config;

	switch(hw->vc_id) {
	case e1000_vc500:
		dpd = vc_dsa_500[hw->bus.func];
		break;
	case e1000_vc5x0:
		dpd = vc_dsa_5x0[hw->bus.func];
		break;
	default:
		dpd = NULL;
		break;
	}
	if( !dpd)
		return(0);		// no dsa switch;

	// allocate and register mdio bus;

	bus = mdiobus_alloc_size(sizeof(struct vc_dsa_priv));
	if( !bus)
		return(-ENOMEM);

	adapter->mii_dsa = bus;
	priv = bus->priv;
	priv->hw = hw;

	pdev = &priv->pdev;
	pdev->name = VC_DSA;
	pdev->id = hw->bus.func;
	pdev->num_resources = 0;
	pdev->dev.platform_data = dpd;

	bus->name = "mdio-dsa";
	bus->phy_mask = ~0;
	memcpy(bus->irq,priv->irqs,(PHY_MAX_ADDR * sizeof(priv->irqs[0])));
	bus->parent = &netdev->dev;

	bus->read = igb_vc_dsa_read;
	bus->write = igb_vc_dsa_write;

	for(i = 0; i < PHY_MAX_ADDR; i++) {
		if( !bus->irq[i])
			bus->irq[i] = PHY_POLL;
	}
	snprintf(bus->id, MII_BUS_ID_SIZE, VC_DSA "-sw%d", hw->bus.func);

	ret = mdiobus_register(bus);
	if(ret < 0)
		goto fail_mdio_reg;

	// register dsa switch;

	dcd = dpd->chip;
	dpd->netdev = &netdev->dev;
	dcd->host_dev = &bus->dev;
	ret = platform_device_register(pdev);
	if(ret < 0)
		goto fail_dsa_reg;

	return(0);

fail_dsa_reg:
	mdiobus_unregister(bus);
fail_mdio_reg:
	mdiobus_free(bus);
	return(ret);
}

// free dsa interface;
void igb_vc_release(struct device *dev){
	/* Stub function to quiet stacktrace warning. */
	return;
}


void igb_vc_dsa_exit(struct igb_adapter *adapter){
	struct mii_bus *bus = adapter->mii_dsa;
	struct vc_dsa_priv *priv;

	if( !bus)
		return;

	priv = bus->priv;

	priv->pdev.dev.release = igb_vc_release;
	platform_device_unregister(&priv->pdev);

	mdiobus_unregister(bus);
	mdiobus_free(bus);
}
