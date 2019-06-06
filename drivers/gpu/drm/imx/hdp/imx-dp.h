/*
 * Copyright 2017-2018 NXP
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 */
#ifndef _IMX_DP_H_
#define _IMX_DP_H_

void dp_fw_load(state_struct *state);
int dp_fw_init(state_struct *state);
void dp_mode_set(state_struct *state,
		 struct drm_display_mode *mode,
		 int format, int color_depth,
		 int max_link_rate);
int dp_phy_init(state_struct *state,
		struct drm_display_mode *mode,
		int format,
		int color_depth);
int dp_phy_init_t28hpc(state_struct *state,
		       struct drm_display_mode *mode,
		       int format,
		       int color_depth);
int dp_get_edid_block(void *data, u8 *buf, u32 block, size_t len);
int dp_get_hpd_state(state_struct *state, u8 *hpd);
int dp_read_dpcd(state_struct *state, unsigned int offset,
		  void *buffer, size_t size);
int dp_write_dpcd(state_struct *state, unsigned int offset,
		  void *buffer, size_t size);

static inline int dp_readb_dpcd(state_struct *state, unsigned int offset,
				u8 *val)
{
	return dp_read_dpcd(state, offset, val, 1);
}

static inline int dp_writeb_dpcd(state_struct *state, unsigned int offset,
				 u8 val)
{
	return dp_read_dpcd(state, offset, &val, 1);
}

#endif
