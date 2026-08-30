/*
 * Jegadish D (jegadish@velocloud.net)
 * Copyright (C) 2013, Velocloud Inc.
 * 
 * IOCTL interface to access Marvell DSA 88E6123-61-65 switch 
 *	- Read/Write port/global registers of the switch
 *	- Read/Load/Purge MAC addresses from the switch memory 
 *	- Read/Load/Purge/Flush VTU entries from the switch memory 
 */

#ifndef DSA_MV_IOCTL_H
#define DSA_MV_IOCTL_H

#include <linux/netdevice.h>

/* Marvell DSA Device private ioctl commands */
#define SIOCGMVREG 0x89F1
#define SIOCSMVREG 0x89F2
#define SIOCGMVMAC 0x89F3
#define SIOCSMVMAC 0x89F4
#define SIOCDMVMAC 0x89F5
#define SIOCFMVMAC 0x89F6
#define SIOCSMV1XM 0x89F7
#define SIOCGMVVTU 0x89F8
#define SIOCSMVVTU 0x89F9
#define SIOCDMVVTU 0x89FA
#define SIOCFMVVTU 0x89FB
#define SIOCATUINFO 0x89FC
#define SIOCGETATU 0x89FD
#define SIOCSETATU 0x89FE


/* Marvell DSA register types */
#define MV_REG_PORT	0
#define MV_REG_GLOBAL	1
#define MV_REG_GLOBAL2	2
#define MV_REG_XPORT	3
#define MV_REG_PHY	4
#define MV_REG_SERDES	5

/* Marvell DSA register addr */
#define MV_REG_PORT_ADDR	0x10
#define MV_REG_GLOBAL_ADDR	0x1b
#define MV_REG_GLOBAL2_ADDR     0x1c

/* Marvel DSA ATU reg */
#define MV_ATU_OPS_REG	0xB
#define MV_ATU_DATA_REG 0xC
#define MV_ATU_MAC_REG1 0xD
#define MV_ATU_MAC_REG2 0xE
#define MV_ATU_MAC_REG3 0xF

#define MV_PORT_AV_REG	0xB
#define MV_PORT_CR_REG	0x4

/* Marvell DSA Device private ioctl data */

/* 802.1X */
struct dsa_mv_reg {
	uint16_t reg_type;
	uint16_t reg_port;
	uint16_t reg_num;
	uint16_t reg_value;
};

struct dsa_mv_atu_opdata {
	uint16_t ops_reg;
	uint16_t data_reg;
	uint8_t mac_addr[ETH_ALEN];
	uint8_t reserved[6];
};

struct dsa_mv_atu_opdesc {
	uint8_t mac_addr[ETH_ALEN];
	uint16_t fid;
	uint8_t entry_state;
	uint8_t flush_all_flag;
	uint8_t reserved[6];
};

struct dsa_mv_atu_reg {
	union {
		struct dsa_mv_atu_opdata out;
		struct dsa_mv_atu_opdesc in;
	} atu_op;
};

// all are output args only;
struct dsa_mv_atuinfo {
	uint16_t nmacs;		// max # of atu macs;
	uint16_t fid;		// max fid;
	uint16_t ports;		// all ports mask;
	uint16_t cpu;		// cpu ports mask;
	uint16_t cross;		// switch interconnect ports mask;
	uint16_t lan;		// lan ports mask;
	uint16_t age;		// max age;
};

// atu entry;
struct dsa_mv_atuent {
	uint8_t mac[ETH_ALEN];
	uint8_t portvec;
	uint8_t state;
};

// ops on lists of atu entries;
struct dsa_mv_atuop {
	uint16_t nent;		// in: number of allocated entries, out: max # of entries;
	uint16_t fid;		// in: filtering id, out: max fid;
	uint16_t pad[2];
	struct dsa_mv_atuent *ent;	// out: buf for entries;
};

// op in individual atu entry;
struct dsa_mv_atuset {
	struct dsa_mv_atuent ent;
	uint16_t fid;
};

/* VLAN */
struct dsa_mv_vtu_opdesc {

	uint16_t vid;
	uint16_t fid;
	uint8_t pol_flag;
	uint16_t sid;
	uint16_t egress_flag;
	uint8_t priority;
	uint8_t reserved[6];
};

struct dsa_mv_vtu_opdata {

	uint16_t ops_reg;
	uint16_t vid;
	uint16_t egress_flag;
	uint8_t valid_flag;
	uint8_t reserved[9];
};

struct dsa_mv_vtu_reg {
	union {
		struct dsa_mv_vtu_opdata out;
		struct dsa_mv_vtu_opdesc in;
	} vtu_op;
};

#endif

