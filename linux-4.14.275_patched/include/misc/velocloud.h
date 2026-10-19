/*
 * velocloud.h
 * Copyright (C) 2015 VeloCloud Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _VELOCLOUD_H_
#define _VELOCLOUD_H_

// edge board enums;

enum vc_edge_id {
	VC_NONE = 0,
	VC_EDGE_NEXCOM,
	VC_EDGE500,
	VC_EDGE510,
	VC_EDGE520,
	VC_EDGE540,
	VC_EDGE520B,
	VC_EDGE540B,
	VC_EDGE800SM,
	VC_EDGE1000IFM,
	VC_EDGE1000SM,
	VC_EDGE1000QAT,
	// add new before here,
	VC_N_BOARDS,
};

#endif /* _VELOCLOUD_H_ */
