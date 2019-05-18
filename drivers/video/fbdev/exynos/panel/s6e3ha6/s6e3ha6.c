/*
 * linux/drivers/video/fbdev/exynos/panel/s6e3ha6/s6e3ha6.c
 *
 * S6E3HA6 Dimming Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/of_gpio.h>
#include <video/mipi_display.h>
/* TODO : remove dsim dependent code */
#include "../../dpu/decon.h"
#include "../../dpu/dsim.h"
#include "../panel.h"
#include "s6e3ha6.h"
#include "s6e3ha6_panel.h"
#ifdef CONFIG_PANEL_AID_DIMMING
#include "../dimming.h"
#include "../panel_dimming.h"
#endif
#include "../panel_drv.h"

#ifdef CONFIG_PANEL_AID_DIMMING
static int hbm_ctoi(s32 (*dst)[MAX_COLOR], u8 *hbm, int nr_tp)
{
	unsigned int i, c, pos = 1;

	for (i = nr_tp - 1; i > 0; i--) {
		for_each_color(c) {
			if (i == nr_tp - 1)
				dst[i][c] = ((hbm[0] >> c) & 0x01) << 8;
			dst[i][c] |= hbm[pos++];
		}
	}

	dst[0][RED] = (hbm[pos] >> 4) & 0xF;
	dst[0][GREEN] = hbm[pos] & 0xF;
	dst[0][BLUE] = hbm[pos + 1] & 0xF;

	return 0;
}

static void mtp_ctoi(s32 (*dst)[MAX_COLOR], char *src, int nr_tp)
{
	int i, c, pos = 0, sign;

	for (i = nr_tp - 1; i > 0; i--) {
		for_each_color(c) {
			if (i == nr_tp - 1) {
				sign = (src[pos] & 0x01) ? -1 : 1;
				dst[i][c] = sign * src[pos + 1];
				pos += 2;
			} else {
				sign = (src[pos] & 0x80) ? -1 : 1;
				dst[i][c] = sign * (src[pos] & 0x7F);
				pos += 1;
			}
		}
	}
	dst[0][RED] = (src[pos] >> 4) & 0xF;
	dst[0][GREEN] = src[pos] & 0xF;
	dst[0][BLUE] = src[pos + 1] & 0xF;
}

static int init_dimming(struct panel_info *panel_data, int id,
		s32 (*mtp)[MAX_COLOR], s32 (*hbm)[MAX_COLOR])
{
	int i, j, len, ret = 0;
	struct panel_dimming_info *panel_dim_info;
	char strbuf[1024];
	struct dimming_info *dim_info;
	struct maptbl *gamma_maptbl = NULL;
	u32 luminance, nr_luminance, nr_hbm_luminance, nr_extend_hbm_luminance;
	struct panel_device *panel = to_panel_device(panel_data);
	struct panel_bl_device *panel_bl;
	struct brightness_table *brt_tbl;
	struct panel_bl_sub_dev *subdev;

	panel_bl = &panel->panel_bl;
	if (id >= MAX_PANEL_BL_SUBDEV) {
		panel_err("%s panel_bl-%d invalid id\n", __func__, id);
		return -EINVAL;
	}

	subdev = &panel_bl->subdev[id];
	brt_tbl = &subdev->brt_tbl;

	panel_dim_info = panel_data->panel_dim_info[id];
	if (unlikely(!panel_dim_info)) {
		pr_err("%s panel_bl-%d panel_dim_info not found\n",
				__func__, id);
		return -EINVAL;
	}

#ifdef CONFIG_SUPPORT_PANEL_SWAP
	if (panel_data->dim_info[id]) {
		kfree(panel_data->dim_info[id]);
		panel_data->dim_info[id] = NULL;
		pr_info("%s panel_bl-%d free dimming info\n", __func__, id);
	}
#endif

	if (panel_data->dim_info[id]) {
		pr_warn("%s panel_bl-%d already initialized\n", __func__, id);
		return -EINVAL;
	}

	dim_info = (struct dimming_info *)kzalloc(sizeof(struct dimming_info), GFP_KERNEL);
	if (unlikely(!dim_info)) {
		pr_err("%s panel_bl-%d failed to alloc dimming_info\n", __func__, id);
		return -ENOMEM;
	}

	ret = init_dimming_info(dim_info, &panel_dim_info->dim_init_info);
	if (unlikely(ret)) {
		pr_err("%s panel_bl-%d failed to init_dimming_info\n", __func__, id);
		kfree(dim_info);
		return ret;
	}

	memcpy(brt_tbl, panel_dim_info->brt_tbl, sizeof(struct brightness_table));
	nr_luminance = panel_dim_info->nr_luminance;
	nr_hbm_luminance = panel_dim_info->nr_hbm_luminance;
	nr_extend_hbm_luminance = panel_dim_info->nr_extend_hbm_luminance;

	if (id == PANEL_BL_SUBDEV_TYPE_HMD) {
#ifdef CONFIG_SUPPORT_HMD
		gamma_maptbl = find_panel_maptbl_by_index(panel_data, HMD_GAMMA_MAPTBL);
#endif
	} else {
		gamma_maptbl = find_panel_maptbl_by_index(panel_data, GAMMA_MAPTBL);
	}
	if (unlikely(!gamma_maptbl)) {
		pr_err("%s panel_bl-%d gamma_maptbl not found\n", __func__, id);
		kfree(dim_info);
		return -EINVAL;
	}

	init_dimming_mtp(dim_info, mtp);
	if (hbm && (panel_dim_info->hbm_target_luminance >
			panel_dim_info->target_luminance))
		init_dimming_hbm_info(dim_info,
				hbm, panel_dim_info->hbm_target_luminance);
	process_dimming(dim_info);

	for (i = 0; i < brt_tbl->sz_lum; i++) {
		if (i >= nr_luminance + nr_hbm_luminance)
			luminance = brt_tbl->lum[nr_luminance + nr_hbm_luminance - 1];
		else
			luminance = brt_tbl->lum[i];
		get_dimming_gamma_compact(dim_info, luminance,
				(u8 *)&gamma_maptbl->arr[i * gamma_maptbl->ncol]);
	}

	panel_data->dim_info[id] = dim_info;

	for (i = 0; i < brt_tbl->sz_lum; i++) {
		luminance = brt_tbl->lum[i];
		len = snprintf(strbuf, 1024, "gamma[%3d] : ", luminance);
		for (j = 0; j < GAMMA_CMD_CNT - 1; j++)
			len += snprintf(strbuf + len, max(1024 - len, 0),
					"%02X ", gamma_maptbl->arr[i * gamma_maptbl->ncol + j]);
		pr_info("%s\n", strbuf);
	}

	return 0;
}

static int init_subdev_gamma_table(struct maptbl *tbl, int id)
{
	struct resinfo *res;
	int ret, nr_tp;
	s32 (*mtp_offset)[MAX_COLOR] = NULL;
	s32 (*hbm_gamma_tbl)[MAX_COLOR] = NULL;
	struct panel_info *panel_data;
	struct panel_device *panel;
	struct panel_dimming_info *panel_dim_info;

	if (unlikely(!tbl || !tbl->pdata)) {
		panel_err("%s panel_bl-%d invalid param (tbl %p, pdata %p)\n",
				__func__, id, tbl, tbl ? tbl->pdata : NULL);
		return -EINVAL;
	}

	if (id >= MAX_PANEL_BL_SUBDEV) {
		panel_err("%s panel_bl-%d invalid id\n", __func__, id);
		return -EINVAL;
	}

	panel = tbl->pdata;
	panel_data = &panel->panel_data;
	panel_dim_info = panel_data->panel_dim_info[id];
	if (unlikely(!panel_dim_info)) {
		pr_err("%s panel_bl-%d panel_dim_info is null\n", __func__, id);
		return -EINVAL;
	}

	nr_tp = panel_dim_info->dim_init_info.nr_tp;

	res = find_panel_resource(panel_data, "mtp");
	if (unlikely(!res)) {
		pr_warn("%s panel_bl-%d resource(mtp) not found\n", __func__, id);
		return -EINVAL;
	}

	mtp_offset = kzalloc(sizeof(s32) * nr_tp * MAX_COLOR, GFP_KERNEL);
	mtp_ctoi(mtp_offset, res->data, nr_tp);

	if (panel_dim_info->nr_hbm_luminance > 0) {
		res = find_panel_resource(panel_data, "hbm_gamma");
		if (unlikely(!res)) {
			pr_warn("%s panel_bl-%d hbm_gamma not found\n", __func__, id);
			kfree(mtp_offset);
			return -EIO;
		}

		hbm_gamma_tbl = kzalloc(sizeof(s32) * nr_tp * MAX_COLOR, GFP_KERNEL);
		hbm_ctoi(hbm_gamma_tbl, res->data, nr_tp);
	}

	ret = init_dimming(panel_data, id, mtp_offset, hbm_gamma_tbl);
	if (unlikely(ret)) {
		pr_err("%s panel_bl-%d failed to init_dimming\n", __func__, id);
		kfree(mtp_offset);
		if (hbm_gamma_tbl)
			kfree(hbm_gamma_tbl);
		return -ENODEV;
	}

	kfree(mtp_offset);
	if (hbm_gamma_tbl)
		kfree(hbm_gamma_tbl);

	panel_info("%s panel_bl-%d gamma_table initialized\n", __func__, id);

	return 0;
}
#else
static int init_subdev_gamma_table(struct maptbl *tbl, int id)
{
	panel_err("%s aid dimming unspported\n", __func__);
	return -ENODEV;
}
#endif /* CONFIG_PANEL_AID_DIMMING */

int init_common_table(struct maptbl *tbl)
{
	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	return 0;
}

int init_gamma_table(struct maptbl *tbl)
{
	return init_subdev_gamma_table(tbl, PANEL_BL_SUBDEV_TYPE_DISP);
}

int getidx_common_maptbl(struct maptbl *tbl)
{
	return 0;
}

int getidx_dimming_maptbl(struct maptbl *tbl)
{
	int row;
	struct panel_bl_device *panel_bl;
	struct panel_device *panel = (struct panel_device *)tbl->pdata;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}
	panel_bl = &panel->panel_bl;
	row = panel_bl->props.actual_brightness_index;

	return tbl->ncol * row;
}

#ifdef CONFIG_SUPPORT_HMD
int init_hmd_gamma_table(struct maptbl *tbl)
{
	return init_subdev_gamma_table(tbl, PANEL_BL_SUBDEV_TYPE_HMD);
}

int getidx_hmd_dimming_mtptbl(struct maptbl *tbl)
{
	int row = 0;
	struct panel_bl_device *panel_bl;
	struct panel_device *panel = (struct panel_device *)tbl->pdata;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		goto exit;
	}
	panel_bl = &panel->panel_bl;
	row = panel_bl->props.actual_brightness_index;

exit:
	return tbl->ncol * row;
}
#endif /* CONFIG_SUPPORT_HMD */

int getidx_brt_tbl(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_bl_device *panel_bl;
	int index;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_bl = &panel->panel_bl;
	index = get_brightness_pac_step(panel_bl, panel_bl->props.brightness);

	if (index < 0) {
		panel_err("PANEL:ERR:%s:invalid brightness %d, index %d\n",
				__func__, panel_bl->props.brightness, index);
		return -EINVAL;
	}

	if (index > S6E3HA6_TOTAL_PAC_STEPS - 1)
		index = S6E3HA6_TOTAL_PAC_STEPS - 1;

	return sizeof_row(tbl) * index;
}

int getidx_aor_table(struct maptbl *tbl)
{
	return getidx_brt_tbl(tbl);
}

int getidx_irc_table(struct maptbl *tbl)
{
	return getidx_brt_tbl(tbl);
}

int getidx_poc_onoff_table(struct maptbl *tbl)
{
	struct panel_info *panel_data;
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	int idx;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_data = &panel->panel_data;

	idx = sizeof_row(tbl) * (panel_data->props.poc_onoff ? POC_ONOFF_ON : POC_ONOFF_OFF);

	return idx;
}

int getidx_irc_onoff_table(struct maptbl *tbl)
{
	struct panel_info *panel_data;
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	int idx;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_data = &panel->panel_data;

	idx = sizeof_row(tbl) * (panel_data->props.irc_onoff ? IRC_ONOFF_ON : IRC_ONOFF_OFF);

	return idx;
}

int getidx_mps_table(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_bl_device *panel_bl;
	int row;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_bl = &panel->panel_bl;
	row = (get_actual_brightness(panel_bl, panel_bl->props.brightness) <= 39) ? 0 : 1;

	return tbl->ncol * row;
}

int init_elvss_temp_table(struct maptbl *tbl)
{
	struct panel_info *panel_data;
	struct panel_device *panel;
	int i, layer, ret;
	u8 elvss_temp_0_otp_value = 0;
	u8 elvss_temp_1_otp_value = 0;

	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	panel = tbl->pdata;
	panel_data = &panel->panel_data;

	ret = resource_copy_by_name(panel_data, &elvss_temp_0_otp_value, "elvss_temp_0");
	if (unlikely(ret)) {
		pr_err("%s, elvss_temp not found in panel resource\n", __func__);
		return -EINVAL;
	}

	ret = resource_copy_by_name(panel_data, &elvss_temp_1_otp_value, "elvss_temp_1");
	if (unlikely(ret)) {
		pr_err("%s, elvss_temp not found in panel resource\n", __func__);
		return -EINVAL;
	}

	for_each_layer(tbl, layer) {
		for_each_row(tbl, i) {
			tbl->arr[layer * sizeof_layer(tbl) + i] =
			(i < S6E3HA6_NR_LUMINANCE) ?
			elvss_temp_0_otp_value : elvss_temp_1_otp_value;
		}
	}

	return 0;
}

int getidx_elvss_temp_table(struct maptbl *tbl)
{
	int row, plane;
	struct panel_info *panel_data;
	struct panel_bl_device *panel_bl;
	struct panel_device *panel = (struct panel_device *)tbl->pdata;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_data = &panel->panel_data;
	panel_bl = &panel->panel_bl;

	plane = (UNDER_MINUS_15(panel_data->props.temperature) ? UNDER_MINUS_FIFTEEN :
			(UNDER_0(panel_data->props.temperature) ? UNDER_ZERO : OVER_ZERO));
	row = panel_bl->props.actual_brightness_index;

	return maptbl_index(tbl, plane, row, 0);
}

#ifdef CONFIG_SUPPORT_XTALK_MODE
int getidx_vgh_table(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_info *panel_data;
	int row = 0;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_data = &panel->panel_data;

	row = ((panel_data->props.xtalk_mode) ? 1 : 0);
	panel_info("%s xtalk_mode %d\n", __func__, row);

	return row;
}
#endif

int getidx_vint_table(struct maptbl *tbl)
{
	int row, nit;
	int vint_index[] = { 5, 6, 7, 8, 9, 10, 11, 12, 13 };
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_bl_device *panel_bl;
	struct panel_info *panel_data;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_data = &panel->panel_data;
	panel_bl = &panel->panel_bl;

	nit = get_actual_brightness(panel_bl, panel_bl->props.brightness);

	if (UNDER_MINUS_15(panel_data->props.temperature))
		return sizeof_maptbl(tbl) - 1;

	for (row = 0; row < ARRAY_SIZE(vint_index); row++)
		if (nit <= vint_index[row])
			break;
	return row;
}

int getidx_acl_onoff_table(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	return panel_bl_get_acl_pwrsave(&panel->panel_bl);
}

int getidx_hbm_onoff_table(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_bl_device *panel_bl;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	panel_bl = &panel->panel_bl;

	return is_hbm_brightness(panel_bl, panel_bl->props.brightness);
}

int getidx_acl_opr_table(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;

	if (panel == NULL) {
		panel_err("PANEL:ERR:%s:panel is null\n", __func__);
		return -EINVAL;
	}

	return maptbl_index(tbl, 0, panel_bl_get_acl_opr(&panel->panel_bl), 0);
}

int getidx_dsc_table(struct maptbl *tbl)
{
	struct decon_lcd *lcd_info;
	struct panel_device *panel = (struct panel_device *)tbl->pdata;

	lcd_info = &panel->lcd_info;

	return (lcd_info->dsc_enabled ? 1 : 0);
}

int getidx_resolution_table(struct maptbl *tbl)
{
	int row = 0;
#ifdef CONFIG_SUPPORT_DSU
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct decon_lcd *lcd_info = &panel->lcd_info;

	panel_info("%s : dsu_mode : %d\n", __func__, lcd_info->dsu_mode);
	switch (lcd_info->dsu_mode) {
		case DSU_MODE_1:
			row = 0;
			break;
		case DSU_MODE_2:
			row = 1;
			break;
		case DSU_MODE_3:
			row = 2;
			break;
		default:
			panel_err("PANEL:ERR:%s:Invalid dsu mode : %d\n", __func__, lcd_info->dsu_mode);
			row = 0;
			break;
	}
#endif

	return tbl->ncol * row;
}

int getidx_lpm_table(struct maptbl *tbl)
{
	int row = 0;
#ifdef CONFIG_SUPPORT_DOZE
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_properties *props = &panel->panel_data.props;
		switch (props->alpm_mode) {
			case ALPM_LOW_BR:
				row = 0;
				break;
			case HLPM_LOW_BR:
				row = 1;
				break;
			case ALPM_HIGH_BR:
				row = 2;
				break;
			case HLPM_HIGH_BR:
				row = 3;
				break;
			default :
				panel_err("PANEL:ERR:%s:Invalid alpm mode : %d\n",
				__func__, props->alpm_mode);
				row = 0;
				break;
	}
	props->cur_alpm_mode = props->alpm_mode;
#endif
	return tbl->ncol * row;
}

#ifdef CONFIG_SUPPORT_POC_FLASH
void copy_poc_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct panel_info *panel_data;
	u8 octa_id[PANEL_OCTA_ID_LEN] = { 0, };
	u8 poc_ctrl[PANEL_POC_CTRL_LEN] = { 0, };
	u8 poc, gray;
	int ret;

	if (!tbl || !dst)
		return;

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	panel_data = &panel->panel_data;

	ret = resource_copy_by_name(panel_data, octa_id, "octa_id");
	if (unlikely(ret < 0)) {
		pr_err("%s failed to copy resource(octa_id)\n", __func__);
		return;
	}

	ret = resource_copy_by_name(panel_data, poc_ctrl, "poc_ctrl");
	if (unlikely(ret < 0)) {
		pr_err("%s failed to copy resource(poc_ctrl)\n", __func__);
		return;
	}

	poc = octa_id[1] & 0x0F;
	gray = poc_ctrl[3];

	if (poc == 0x00)
		*dst = 0xFF;
	else
		*dst = (gray == 0x33) ? 0x64 : gray;

	pr_info("%s poc %d, gray %02x->%02x\n", __func__, poc, gray, *dst);
}
#endif

#ifdef CONFIG_SUPPORT_GRAM_CHECKSUM
static u8 s6e3ha6_gct_pattern[2][192 * 3];
static u8 s6e3ha6_gct_pattern_line[2][192 * 3 * 5];
int s6e3ha6_getidx_vddm_table(struct maptbl *tbl)
{
	struct panel_device *panel = (struct panel_device *)tbl->pdata;
	struct panel_properties *props = &panel->panel_data.props;

	pr_info("%s vddm %d\n", __func__, props->gct_vddm);

	return tbl->ncol * props->gct_vddm;
}

static void generate_gct_pattern(u8 *dst, int pattern, int size)
{
	static u8 s6e3ha6_gct_value[2] = { 0x5A, 0xA5 };
	static bool initialized = false;
	int i;

	if (size != 192 * 3 * 5 * 1480) {
		pr_err("%s, invalid size %d\n", __func__, size);
		return;
	}

	if (!initialized) {
		/* generate gct pattern */
		for (i = 0; i < 2; i++) {
			memset(s6e3ha6_gct_pattern[i % 2], s6e3ha6_gct_value[i % 2], sizeof(u8) * 192 * 3);
			pr_info("%s %02X pattern\n", __func__, s6e3ha6_gct_value[i % 2]);
			print_data(s6e3ha6_gct_pattern[i % 2], 128);
		}

		/* generate gct pattern line */
		for (i = 0; i < 960 / 192; i++) {
			memcpy(&s6e3ha6_gct_pattern_line[i % 2][i * sizeof(s6e3ha6_gct_pattern[0])],
					s6e3ha6_gct_pattern[i % 2], sizeof(s6e3ha6_gct_pattern[0]));
			memcpy(&s6e3ha6_gct_pattern_line[(i + 1) % 2][i * sizeof(s6e3ha6_gct_pattern[0])],
					s6e3ha6_gct_pattern[(i + 1) % 2], sizeof(s6e3ha6_gct_pattern[0]));
		}
		print_data(s6e3ha6_gct_pattern_line[i % 2], 128);
		initialized = true;
	}

	/* generate gct pattern img */
	for (i = 0; i < 1480; i++) {
		if (pattern == GCT_PATTERN_1)
			memcpy(&dst[i * sizeof(s6e3ha6_gct_pattern_line[i % 2])], 
					s6e3ha6_gct_pattern_line[i % 2],
					sizeof(s6e3ha6_gct_pattern_line[i % 2]));
		else if (pattern == GCT_PATTERN_2)
			memcpy(&dst[i * sizeof(s6e3ha6_gct_pattern_line[(i + 1) % 2])], 
					s6e3ha6_gct_pattern_line[(i + 1) % 2],
					sizeof(s6e3ha6_gct_pattern_line[(i + 1) % 2]));
	}

	pr_info("%s generate gct pattern(%d) %d done!!\n",
			__func__, pattern, size);
}

void copy_gram_img_pattern_0(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct panel_info *panel_data;
	struct panel_properties *props;

	if (!tbl || !dst)
		return;

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	panel_data = &panel->panel_data;
	props = &panel_data->props;

	props->gct_valid_chksum = S6E3HA6_GRAM_CHECKSUM_VALID;

	generate_gct_pattern(dst, props->gct_pattern == GCT_PATTERN_1 ?
			GCT_PATTERN_1 : GCT_PATTERN_2, S6E3HA6_GRAM_IMG_SIZE);
}

void copy_gram_img_pattern_1(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct panel_info *panel_data;
	struct panel_properties *props;

	if (!tbl || !dst)
		return;

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	panel_data = &panel->panel_data;
	props = &panel_data->props;

	generate_gct_pattern(dst, props->gct_pattern == GCT_PATTERN_1 ?
			GCT_PATTERN_2 : GCT_PATTERN_1, S6E3HA6_GRAM_IMG_SIZE);
}
#endif

void copy_common_maptbl(struct maptbl *tbl, u8 *dst)
{
	int idx;
	if (!tbl || !dst) {
		pr_err("%s, invalid parameter (tbl %p, dst %p\n",
				__func__, tbl, dst);
		return;
	}

	idx = maptbl_getidx(tbl);
	if (idx < 0) {
		pr_err("%s, failed to getidx %d\n", __func__, idx);
		return;
	}

	memcpy(dst, &(tbl->arr)[idx], sizeof(u8) * tbl->sz_copy);
#ifdef DEBUG_PANEL
	panel_dbg("%s copy from %s %d %d\n",
			__func__, tbl->name, idx, tbl->sz_copy);
	print_data(dst, tbl->sz_copy);
#endif
}

void copy_tset_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct panel_info *panel_data;
	if (!tbl || !dst)
		return;

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	panel_data = &panel->panel_data;

	*dst = (panel_data->props.temperature < 0) ?
		BIT(7) | abs(panel_data->props.temperature) :
		panel_data->props.temperature;
}

void copy_copr_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct copr_info *copr;
	struct copr_reg *reg;

	if (!tbl || !dst)
		return;

	copr = (struct copr_info *)tbl->pdata;
	if (unlikely(!copr))
		return;

	reg = &copr->props.reg;

	dst[0] = (reg->copr_gamma << 1) | reg->copr_en;
	dst[1] = reg->copr_er;
	dst[2] = reg->copr_eg;
	dst[3] = reg->copr_eb;
	dst[4] = ((reg->roi_on << 4) | ((reg->roi_xs >> 8) & 0xF));
	dst[5] = reg->roi_xs & 0xFF;
	dst[6] = (reg->roi_ys >> 8) & 0xF;
	dst[7] = reg->roi_ys & 0xFF;
	dst[8] = (reg->roi_xe >> 8) & 0xF;
	dst[9] = reg->roi_xe & 0xFF;
	dst[10] = (reg->roi_ye >> 8) & 0xF;
	dst[11] = reg->roi_ye & 0xFF;

#ifdef DEBUG_PANEL
	print_data(dst, 12);
#endif
}
#ifdef CONFIG_ACTIVE_CLOCK

#define SELF_CLK_ANA_CLOCK_EN 	0x10
#define SELF_CLK_BLINK_EN		0x01
#define TIME_UPDATE				0x80

void copy_self_clk_update_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct act_clk_info *act_info;

	if (!tbl || !dst)
		return;

	panel_info("active clock update\n");

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	act_info = &panel->act_clk_dev.act_info;

	dst[1] = 0;

	if (act_info->en) {
		dst[1] = SELF_CLK_ANA_CLOCK_EN;
		panel_info("PANEL:INFO:%s:active clock enable\n", __func__);
		if (act_info->interval == 100) {
			dst[8] = 0x03;
			dst[9] = 0x41;
		} else {
			dst[8] = 0x1E;
			dst[9] = 0x4A;
		}
	}
}
void copy_self_clk_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct act_clk_info *act_info;
	struct act_blink_info *blink_info;

	if (!tbl || !dst)
		return;

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	act_info = &panel->act_clk_dev.act_info;
	blink_info = &panel->act_clk_dev.blink_info;

	dst[1] = 0;

	if (act_info->en) {
		dst[1] = SELF_CLK_ANA_CLOCK_EN;
		panel_info("PANEL:INFO:%s:active clock enable\n", __func__);
		if (act_info->interval == 100) {
			dst[8] = 0x03;
			dst[9] = 0x41;
		} else {
			dst[8] = 0x1E;
			dst[9] = 0x4A;
		}

		dst[15] = act_info->time_hr % 12;
		dst[16] = act_info->time_min % 60;
		dst[17] = act_info->time_sec % 60;
		dst[18] = act_info->time_ms;

		panel_info("PANEL:INFO:%s:active clock %d:%d:%d\n", __func__,
			dst[15], dst[16], dst[17]);

		if (act_info->update_time) {
			dst[9] |= TIME_UPDATE;
		}

		dst[19] = (act_info->pos_x >> 8) & 0xff;
		dst[20] = act_info->pos_x & 0xff;

		dst[21] = (act_info->pos_y >> 8) & 0xff;
		dst[22] = act_info->pos_y & 0xff;
		panel_info("PANEL:INFO:%s:active clock pos %d,%d\n", __func__,
			act_info->pos_x, act_info->pos_y);
	}

	if (blink_info->en) {
		dst[1] = SELF_CLK_BLINK_EN;

		dst[32] = blink_info->interval;
		dst[33] = blink_info->radius;
		/* blink color : Red */
		dst[34] = (unsigned char)(blink_info->color >> 16) & 0xff;
		/* blink color : Blue */
		dst[35] = (unsigned char)(blink_info->color >> 8) & 0xff;
		/* blink color : Green */
		dst[36] = (unsigned char)(blink_info->color & 0xff);

		dst[37] = 0x05;

		dst[38] = (blink_info->pos1_x >> 8) & 0xff;
		dst[39] = (blink_info->pos1_x & 0xff);

		dst[40] = (blink_info->pos1_y >> 8) & 0xff;
		dst[41] = (blink_info->pos1_y & 0xff);

		dst[42] = (blink_info->pos2_x >> 8) & 0xff;
		dst[43] = (blink_info->pos2_x & 0xff);

		dst[44] = (blink_info->pos2_y >> 8) & 0xff;
		dst[45] = (blink_info->pos2_y & 0xff);

	}
	print_data(dst, 46);
}

void copy_self_drawer(struct maptbl *tbl, u8 *dst)
{
	struct panel_device *panel;
	struct act_clk_info *act_info;
	struct act_drawer_info *draw_info;

	if (!tbl || !dst)
		return;

	panel = (struct panel_device *)tbl->pdata;
	if (unlikely(!panel))
		return;

	draw_info = &panel->act_clk_dev.draw_info;
	act_info = &panel->act_clk_dev.act_info;

	dst[1] = 0;

	if (!act_info->en) {
		panel_info("ACT-CLK:INFO:disable act_clk\n");
		return;
	}

	dst[1] = 0x01;
	panel_info("PANEL:INFO:%s:self drawer\n", __func__);

	dst[29] = (act_info->pos_x >> 8) & 0xff;
	dst[30] = act_info->pos_x & 0xff;

	dst[31] = (act_info->pos_y >> 8) & 0xff;
	dst[32] = act_info->pos_y & 0xff;
	panel_info("PANEL:INFO:%s:active clock pos %d,%d\n", __func__,
		act_info->pos_x, act_info->pos_y);

	/* SD color : Red */
	dst[39] = (unsigned char)(draw_info->sd_line_color>> 16) & 0xff;
	/* SD color : Blue */
	dst[40] = (unsigned char)(draw_info->sd_line_color >> 8) & 0xff;
	/* SD color : Green */
	dst[41] = (unsigned char)(draw_info->sd_line_color & 0xff);

	/* SD2 color : Red */
	dst[42] = (unsigned char)(draw_info->sd2_line_color>> 16) & 0xff;
	/* SD2 color : Blue */
	dst[43] = (unsigned char)(draw_info->sd2_line_color >> 8) & 0xff;
	/* SD2 color : Green */
	dst[44] = (unsigned char)(draw_info->sd2_line_color & 0xff);

	dst[45] = (unsigned char)(draw_info->sd_radius & 0xff);
}
#endif

#ifdef CONFIG_EXYNOS_DECON_MDNIE_LITE
int init_color_blind_table(struct maptbl *tbl)
{
	struct mdnie_info *mdnie;

	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	mdnie = tbl->pdata;

	if (S6E3HA6_SCR_CR_OFS + mdnie->props.sz_scr > sizeof_maptbl(tbl)) {
		panel_err("%s invalid size (maptbl_size %d, sz_scr %d)\n",
			__func__, sizeof_maptbl(tbl), mdnie->props.sz_scr);
		return -EINVAL;
	}

	memcpy(&tbl->arr[S6E3HA6_SCR_CR_OFS],
			mdnie->props.scr, mdnie->props.sz_scr);

	return 0;
}

int getidx_mdnie_scenario_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	return tbl->ncol * (mdnie->props.mode);
}

int getidx_mdnie_hmd_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	return tbl->ncol * (mdnie->props.hmd);
}

int getidx_mdnie_hdr_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	return tbl->ncol * (mdnie->props.hdr);
}

int getidx_mdnie_trans_mode_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	if (mdnie->props.trans_mode == TRANS_OFF)
		panel_dbg("%s mdnie trans_mode off\n", __func__);
	return tbl->ncol * (mdnie->props.trans_mode);
}

int getidx_mdnie_night_mode_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	return tbl->ncol * (mdnie->props.night_level);
}

int init_mdnie_night_mode_table(struct maptbl *tbl)
{
	struct mdnie_info *mdnie;
	struct maptbl *night_maptbl;

	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	mdnie = tbl->pdata;

	night_maptbl = mdnie_find_etc_maptbl(mdnie, MDNIE_ETC_NIGHT_MAPTBL);
	if (!night_maptbl) {
		panel_err("%s, NIGHT_MAPTBL not found\n", __func__);
		return -EINVAL;
	}

	if (sizeof_maptbl(tbl) < (S6E3HA6_NIGHT_MODE_OFS +
			sizeof_row(night_maptbl))) {
		panel_err("%s invalid size (maptbl_size %d, night_maptbl_size %d)\n",
			__func__, sizeof_maptbl(tbl), sizeof_row(night_maptbl));
		return -EINVAL;
	}

	maptbl_copy(night_maptbl, &tbl->arr[S6E3HA6_NIGHT_MODE_OFS]);

	return 0;
}

int init_mdnie_color_lens_table(struct maptbl *tbl)
{
	struct mdnie_info *mdnie;
	struct maptbl *color_lens_maptbl;

	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	mdnie = tbl->pdata;

	color_lens_maptbl = mdnie_find_etc_maptbl(mdnie, MDNIE_ETC_COLOR_LENS_MAPTBL);
	if (!color_lens_maptbl) {
		panel_err("%s, COLOR_LENS_MAPTBL not found\n", __func__);
		return -EINVAL;
	}

	if (sizeof_maptbl(tbl) < (S6E3HA6_COLOR_LENS_OFS +
			sizeof_row(color_lens_maptbl))) {
		panel_err("%s invalid size (maptbl_size %d, color_lens_maptbl_size %d)\n",
			__func__, sizeof_maptbl(tbl), sizeof_row(color_lens_maptbl));
		return -EINVAL;
	}

	maptbl_copy(color_lens_maptbl, &tbl->arr[S6E3HA6_COLOR_LENS_OFS]);

	return 0;
}

void update_current_scr_white(struct maptbl *tbl, u8 *dst)
{
	struct mdnie_info *mdnie;

	if (!tbl || !tbl->pdata) {
		pr_err("%s, invalid param\n", __func__);
		return;
	}

	mdnie = (struct mdnie_info *)tbl->pdata;
	mdnie->props.cur_wrgb[0] = *dst;
	mdnie->props.cur_wrgb[1] = *(dst + 2);
	mdnie->props.cur_wrgb[2] = *(dst + 4);
}

int init_color_coordinate_table(struct maptbl *tbl)
{
	struct mdnie_info *mdnie;
	int type, color;

	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	mdnie = tbl->pdata;

	if (sizeof_row(tbl) != ARRAY_SIZE(mdnie->props.coord_wrgb[0])) {
		panel_err("%s invalid maptbl size %d\n", __func__, tbl->ncol);
		return -EINVAL;
	}

	for_each_row(tbl, type) {
		for_each_col(tbl, color) {
			tbl->arr[sizeof_row(tbl) * type + color] =
				mdnie->props.coord_wrgb[type][color];
		}
	}

	return 0;
}

int init_sensor_rgb_table(struct maptbl *tbl)
{
	struct mdnie_info *mdnie;
	int i;

	if (tbl == NULL) {
		panel_err("PANEL:ERR:%s:maptbl is null\n", __func__);
		return -EINVAL;
	}

	if (tbl->pdata == NULL) {
		panel_err("PANEL:ERR:%s:pdata is null\n", __func__);
		return -EINVAL;
	}

	mdnie = tbl->pdata;

	if (tbl->ncol != ARRAY_SIZE(mdnie->props.ssr_wrgb)) {
		panel_err("%s invalid maptbl size %d\n", __func__, tbl->ncol);
		return -EINVAL;
	}

	for (i = 0; i < tbl->ncol; i++)
		tbl->arr[i] = mdnie->props.ssr_wrgb[i];

	return 0;
}

int getidx_color_coordinate_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	static int wcrd_type[MODE_MAX] = {
		WCRD_TYPE_D65, WCRD_TYPE_D65, WCRD_TYPE_D65,
		WCRD_TYPE_ADAPTIVE, WCRD_TYPE_ADAPTIVE,
	};
	if ((mdnie->props.mode < 0) || (mdnie->props.mode >= MODE_MAX)) {
		pr_err("%s, out of mode range %d\n", __func__, mdnie->props.mode);
		return -EINVAL;
	}
	return maptbl_index(tbl, 0, wcrd_type[mdnie->props.mode], 0);
}

int getidx_adjust_ldu_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;
	static int wcrd_type[MODE_MAX] = {
		WCRD_TYPE_D65, WCRD_TYPE_D65, WCRD_TYPE_D65,
		WCRD_TYPE_ADAPTIVE, WCRD_TYPE_ADAPTIVE,
	};

	if (!IS_LDU_MODE(mdnie))
		return -EINVAL;

	if ((mdnie->props.mode < 0) || (mdnie->props.mode >= MODE_MAX)) {
		pr_err("%s, out of mode range %d\n", __func__, mdnie->props.mode);
		return -EINVAL;
	}
	if ((mdnie->props.ldu < 0) || (mdnie->props.ldu >= MAX_LDU_MODE)) {
		pr_err("%s, out of ldu mode range %d\n", __func__, mdnie->props.ldu);
		return -EINVAL;
	}
	return maptbl_index(tbl, wcrd_type[mdnie->props.mode], mdnie->props.ldu, 0);
}

int getidx_color_lens_maptbl(struct maptbl *tbl)
{
	struct mdnie_info *mdnie = (struct mdnie_info *)tbl->pdata;

	if (!IS_COLOR_LENS_MODE(mdnie))
		return -EINVAL;

	if ((mdnie->props.color_lens_color < 0) || (mdnie->props.color_lens_color >= COLOR_LENS_COLOR_MAX)) {
		pr_err("%s, out of color lens color range %d\n", __func__, mdnie->props.color_lens_color);
		return -EINVAL;
	}
	if ((mdnie->props.color_lens_level < 0) || (mdnie->props.color_lens_level >= COLOR_LENS_LEVEL_MAX)) {
		pr_err("%s, out of color lens level range %d\n", __func__, mdnie->props.color_lens_level);
		return -EINVAL;
	}
	return maptbl_index(tbl, mdnie->props.color_lens_color, mdnie->props.color_lens_level, 0);
}

void copy_color_coordinate_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct mdnie_info *mdnie;
	int i, idx;
	u8 value;

	if (unlikely(!tbl || !dst))
		return;

	mdnie = (struct mdnie_info *)tbl->pdata;
	idx = maptbl_getidx(tbl);
	if (idx < 0 || (idx + MAX_COLOR > sizeof_maptbl(tbl))) {
		panel_err("%s invalid index %d\n", __func__, idx);
		return;
	}

	if (tbl->ncol != MAX_COLOR) {
		panel_err("%s invalid maptbl size %d\n", __func__, tbl->ncol);
		return;
	}

	for (i = 0; i < tbl->ncol; i++, dst += 2) {
		mdnie->props.def_wrgb[i] = tbl->arr[idx + i];
		value = mdnie->props.def_wrgb[i] +
			(char)((mdnie->props.mode == AUTO) ?
				mdnie->props.def_wrgb_ofs[i] : 0);
		mdnie->props.cur_wrgb[i] = value;
		*dst = value;

#ifdef DEBUG_PANEL
		if (mdnie->props.mode == AUTO) {
			panel_info("%s cur_wrgb[%d] %d(%02X) def_wrgb[%d] %d(%02X), def_wrgb_ofs[%d] %d\n",
					__func__, i, mdnie->props.cur_wrgb[i], mdnie->props.cur_wrgb[i],
					i, mdnie->props.def_wrgb[i], mdnie->props.def_wrgb[i],
					i, mdnie->props.def_wrgb_ofs[i]);
		} else {
			panel_info("%s cur_wrgb[%d] %d(%02X) def_wrgb[%d] %d(%02X), def_wrgb_ofs[%d] none\n",
					__func__, i, mdnie->props.cur_wrgb[i], mdnie->props.cur_wrgb[i],
					i, mdnie->props.def_wrgb[i], mdnie->props.def_wrgb[i], i);
		}
#endif
	}
}

void copy_scr_white_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct mdnie_info *mdnie;
	int i, idx;

	if (unlikely(!tbl || !dst))
		return;

	mdnie = (struct mdnie_info *)tbl->pdata;
	idx = maptbl_getidx(tbl);
	if (idx < 0 || (idx + MAX_COLOR > sizeof_maptbl(tbl))) {
		panel_err("%s invalid index %d\n", __func__, idx);
		return;
	}

	if (tbl->ncol != MAX_COLOR) {
		panel_err("%s invalid maptbl size %d\n", __func__, tbl->ncol);
		return;
	}

	for (i = 0; i < tbl->ncol; i++, dst += 2) {
		mdnie->props.cur_wrgb[i] = tbl->arr[idx + i];
		*dst = tbl->arr[idx + i];
#ifdef DEBUG_PANEL
		panel_info("%s cur_wrgb[%d] %d(%02X)\n",
				__func__, i, mdnie->props.cur_wrgb[i], mdnie->props.cur_wrgb[i]);
#endif
	}
}

void copy_adjust_ldu_maptbl(struct maptbl *tbl, u8 *dst)
{
	struct mdnie_info *mdnie;
	int i, idx;
	u8 value;

	if (unlikely(!tbl || !dst))
		return;

	mdnie = (struct mdnie_info *)tbl->pdata;
	idx = maptbl_getidx(tbl);
	if (idx < 0 || (idx + MAX_COLOR > sizeof_maptbl(tbl))) {
		panel_err("%s invalid index %d\n", __func__, idx);
		return;
	}

	if (tbl->ncol != MAX_COLOR) {
		panel_err("%s invalid maptbl size %d\n", __func__, tbl->ncol);
		return;
	}

	for (i = 0; i < tbl->ncol; i++, dst += 2) {
		value = tbl->arr[idx + i] +
			(((mdnie->props.mode == AUTO) && (mdnie->props.scenario != EBOOK_MODE)) ?
				mdnie->props.def_wrgb_ofs[i] : 0);
		mdnie->props.cur_wrgb[i] = value;
		*dst = value;

#ifdef DEBUG_PANEL
		panel_info("%s cur_wrgb[%d] %d(%02X) (orig:0x%02X offset:%d)\n",
				__func__, i, mdnie->props.cur_wrgb[i], mdnie->props.cur_wrgb[i],
				tbl->arr[idx + i], mdnie->props.def_wrgb_ofs[i]);
#endif
	}
}

int getidx_trans_maptbl(struct pkt_update_info *pktui)
{
	struct panel_device *panel = pktui->pdata;
	struct mdnie_info *mdnie = &panel->mdnie;

	return (mdnie->props.trans_mode == TRANS_ON) ?
		MDNIE_ETC_NONE_MAPTBL : MDNIE_ETC_TRANS_MAPTBL;
}

static int getidx_mdnie_maptbl(struct pkt_update_info *pktui, int offset)
{
	struct panel_device *panel = pktui->pdata;
	struct mdnie_info *mdnie = &panel->mdnie;
	int row = mdnie_get_maptbl_index(mdnie);
	int index;

	if (row < 0) {
		pr_err("%s, invalid row %d\n", __func__, row);
		return -EINVAL;
	}

	index = row * mdnie->nr_reg + offset;
	if (index >= mdnie->nr_maptbl) {
		panel_err("%s exceeded index %d row %d offset %d\n",
				__func__, index, row, offset);
		return -EINVAL;
	}
	return index;
}

int getidx_mdnie_0_maptbl(struct pkt_update_info *pktui)
{
	return getidx_mdnie_maptbl(pktui, 0);
}

int getidx_mdnie_1_maptbl(struct pkt_update_info *pktui)
{
	return getidx_mdnie_maptbl(pktui, 1);
}

int getidx_mdnie_2_maptbl(struct pkt_update_info *pktui)
{
	return getidx_mdnie_maptbl(pktui, 2);
}

int getidx_mdnie_scr_white_maptbl(struct pkt_update_info *pktui)
{
	struct panel_device *panel = pktui->pdata;
	struct mdnie_info *mdnie = &panel->mdnie;

	if (mdnie->props.scr_white_mode < 0 ||
			mdnie->props.scr_white_mode >= MAX_SCR_WHITE_MODE) {
		pr_warn("%s, out of range %d\n",
				__func__, mdnie->props.scr_white_mode);
		return -1;
	}

	if (mdnie->props.scr_white_mode == SCR_WHITE_MODE_COLOR_COORDINATE) {
		pr_debug("%s, coordinate maptbl\n", __func__);
		return MDNIE_COLOR_COORDINATE_MAPTBL;
	} else if (mdnie->props.scr_white_mode == SCR_WHITE_MODE_ADJUST_LDU) {
		pr_debug("%s, adjust ldu maptbl\n", __func__);
		return MDNIE_ADJUST_LDU_MAPTBL;
	} else if (mdnie->props.scr_white_mode == SCR_WHITE_MODE_SENSOR_RGB) {
		pr_debug("%s, sensor rgb maptbl\n", __func__);
		return MDNIE_SENSOR_RGB_MAPTBL;
	} else {
		pr_debug("%s, empty maptbl\n", __func__);
		return MDNIE_SCR_WHITE_NONE_MAPTBL;
	}
}
#endif /* CONFIG_EXYNOS_DECON_MDNIE_LITE */


#ifdef CONFIG_LOGGING_BIGDATA_BUG
static unsigned int g_rddpm = 0xff;
static unsigned int g_rddsm = 0xff;

unsigned int get_panel_bigdata(void)
{
	unsigned int val = 0;

	val = (g_rddsm << 8) | g_rddpm;

	return val;
}
#endif


static void show_rddpm(struct dumpinfo *info)
{
	int ret;
	struct resinfo *res = info->res;
	u8 rddpm[S6E3HA6_RDDPM_LEN] = { 0, };

	if (!res || ARRAY_SIZE(rddpm) != res->dlen) {
		pr_err("%s invalid resource\n", __func__);
		return;
	}

	ret = resource_copy(rddpm, info->res);
	if (unlikely(ret < 0)) {
		pr_err("%s, failed to copy rddpm resource\n", __func__);
		return;
	}

	panel_info("========== SHOW PANEL [0Ah:RDDPM] INFO ==========\n");
	panel_info("* Reg Value : 0x%02x, Result : %s\n",
			rddpm[0], (rddpm[0] == 0x9C) ? "GOOD" : "NG");
	panel_info("* Bootster Mode : %s\n", rddpm[0] & 0x80 ? "ON (GD)" : "OFF (NG)");
	panel_info("* Idle Mode     : %s\n", rddpm[0] & 0x40 ? "ON (NG)" : "OFF (GD)");
	panel_info("* Partial Mode  : %s\n", rddpm[0] & 0x20 ? "ON" : "OFF");
	panel_info("* Sleep Mode    : %s\n", rddpm[0] & 0x10 ? "OUT (GD)" : "IN (NG)");
	panel_info("* Normal Mode   : %s\n", rddpm[0] & 0x08 ? "OK (GD)" : "SLEEP (NG)");
	panel_info("* Display ON    : %s\n", rddpm[0] & 0x04 ? "ON (GD)" : "OFF (NG)");
	panel_info("=================================================\n");
#ifdef CONFIG_LOGGING_BIGDATA_BUG
	g_rddpm = (unsigned int)rddpm[0];
#endif
}

static void show_rddsm(struct dumpinfo *info)
{
	int ret;
	struct resinfo *res = info->res;
	u8 rddsm[S6E3HA6_RDDSM_LEN] = { 0, };

	if (!res || ARRAY_SIZE(rddsm) != res->dlen) {
		pr_err("%s invalid resource\n", __func__);
		return;
	}

	ret = resource_copy(rddsm, info->res);
	if (unlikely(ret < 0)) {
		pr_err("%s, failed to copy rddsm resource\n", __func__);
		return;
	}

	panel_info("========== SHOW PANEL [0Eh:RDDSM] INFO ==========\n");
	panel_info("* Reg Value : 0x%02x, Result : %s\n",
			rddsm[0], (rddsm[0] == 0x80) ? "GOOD" : "NG");
	panel_info("* TE Mode : %s\n", rddsm[0] & 0x80 ? "ON(GD)" : "OFF(NG)");
	panel_info("=================================================\n");
#ifdef CONFIG_LOGGING_BIGDATA_BUG
	g_rddsm = (unsigned int)rddsm[0];
#endif
}

static void show_err(struct dumpinfo *info)
{
	int ret;
	struct resinfo *res = info->res;
	u8 err[S6E3HA6_ERR_LEN] = { 0, }, err_15_8, err_7_0;

	if (!res || ARRAY_SIZE(err) != res->dlen) {
		pr_err("%s invalid resource\n", __func__);
		return;
	}

	ret = resource_copy(err, info->res);
	if (unlikely(ret < 0)) {
		pr_err("%s, failed to copy err resource\n", __func__);
		return;
	}

	err_15_8 = err[0];
	err_7_0 = err[1];

	panel_info("========== SHOW PANEL [EAh:DSIERR] INFO ==========\n");
	panel_info("* Reg Value : 0x%02x%02x, Result : %s\n", err_15_8, err_7_0,
			(err[0] || err[1] || err[2] || err[3] || err[4]) ? "NG" : "GOOD");

	if (err_15_8 & 0x80)
		panel_info("* DSI Protocol Violation\n");

	if (err_15_8 & 0x40)
		panel_info("* Data P Lane Contention Detetion\n");

	if (err_15_8 & 0x20)
		panel_info("* Invalid Transmission Length\n");

	if (err_15_8 & 0x10)
		panel_info("* DSI VC ID Invalid\n");

	if (err_15_8 & 0x08)
		panel_info("* DSI Data Type Not Recognized\n");

	if (err_15_8 & 0x04)
		panel_info("* Checksum Error\n");

	if (err_15_8 & 0x02)
		panel_info("* ECC Error, multi-bit (detected, not corrected)\n");

	if (err_15_8 & 0x01)
		panel_info("* ECC Error, single-bit (detected and corrected)\n");

	if (err_7_0 & 0x80)
		panel_info("* Data Lane Contention Detection\n");

	if (err_7_0 & 0x40)
		panel_info("* False Control Error\n");

	if (err_7_0 & 0x20)
		panel_info("* HS RX Timeout\n");

	if (err_7_0 & 0x10)
		panel_info("* Low-Power Transmit Sync Error\n");

	if (err_7_0 & 0x08)
		panel_info("* Escape Mode Entry Command Error");

	if (err_7_0 & 0x04)
		panel_info("* EoT Sync Error\n");

	if (err_7_0 & 0x02)
		panel_info("* SoT Sync Error\n");

	if (err_7_0 & 0x01)
		panel_info("* SoT Error\n");

	panel_info("* CRC Error Count : %d\n", err[2]);
	panel_info("* ECC1 Error Count : %d\n", err[3]);
	panel_info("* ECC2 Error Count : %d\n", err[4]);

	panel_info("==================================================\n");
}

static void show_err_fg(struct dumpinfo *info)
{
	int ret;
	u8 err_fg[S6E3HA6_ERR_FG_LEN] = { 0, };
	struct resinfo *res = info->res;

	if (!res || ARRAY_SIZE(err_fg) != res->dlen) {
		pr_err("%s invalid resource\n", __func__);
		return;
	}

	ret = resource_copy(err_fg, res);
	if (unlikely(ret < 0)) {
		pr_err("%s, failed to copy err_fg resource\n", __func__);
		return;
	}

	panel_info("========== SHOW PANEL [EEh:ERR_FG] INFO ==========\n");
	panel_info("* Reg Value : 0x%02x, Result : %s\n",
			err_fg[0], (err_fg[0] & 0x4C) ? "NG" : "GOOD");

	if (err_fg[0] & 0x04) {
		panel_info("* VLOUT3 Error\n");
		inc_dpui_u32_field(DPUI_KEY_PNVLO3E, 1);
	}

	if (err_fg[0] & 0x08) {
		panel_info("* ELVDD Error\n");
		inc_dpui_u32_field(DPUI_KEY_PNELVDE, 1);
	}

	if (err_fg[0] & 0x40) {
		panel_info("* VLIN1 Error\n");
		inc_dpui_u32_field(DPUI_KEY_PNVLI1E, 1);
	}

	panel_info("==================================================\n");

	inc_dpui_u32_field(DPUI_KEY_PNESDE, (err_fg[0] & 0x4D) ? 1 : 0);
}

static void show_dsi_err(struct dumpinfo *info)
{
	int ret;
	struct resinfo *res = info->res;
	u8 dsi_err[S6E3HA6_DSI_ERR_LEN] = { 0, };

	if (!res || ARRAY_SIZE(dsi_err) != res->dlen) {
		pr_err("%s invalid resource\n", __func__);
		return;
	}

	ret = resource_copy(dsi_err, res);
	if (unlikely(ret < 0)) {
		pr_err("%s, failed to copy dsi_err resource\n", __func__);
		return;
	}

	panel_info("========== SHOW PANEL [05h:DSIE_CNT] INFO ==========\n");
	panel_info("* Reg Value : 0x%02x, Result : %s\n",
			dsi_err[0], (dsi_err[0]) ? "NG" : "GOOD");
	if (dsi_err[0])
		panel_info("* DSI Error Count : %d\n", dsi_err[0]);
	panel_info("====================================================\n");

	inc_dpui_u32_field(DPUI_KEY_PNDSIE, dsi_err[0]);
}

static void show_self_diag(struct dumpinfo *info)
{
	int ret;
	struct resinfo *res = info->res;
	u8 self_diag[S6E3HA6_SELF_DIAG_LEN] = { 0, };

	ret = resource_copy(self_diag, res);
	if (unlikely(ret < 0)) {
		pr_err("%s, failed to copy self_diag resource\n", __func__);
		return;
	}

	panel_info("========== SHOW PANEL [0Fh:SELF_DIAG] INFO ==========\n");
	panel_info("* Reg Value : 0x%02x, Result : %s\n",
			self_diag[0], (self_diag[0] & 0x80) ? "GOOD" : "NG");
	if ((self_diag[0] & 0x80) == 0)
		panel_info("* OTP Reg Loading Error\n");
	panel_info("=====================================================\n");

	inc_dpui_u32_field(DPUI_KEY_PNSDRE, (self_diag[0] & 0x80) ? 0 : 1);
}
