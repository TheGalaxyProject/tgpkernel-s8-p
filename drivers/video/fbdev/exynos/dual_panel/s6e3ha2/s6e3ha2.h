/*
 * linux/drivers/video/fbdev/exynos/panel/s6e3ha2/s6e3ha2.h
 *
 * Header file for S6E3HA2 Dimming Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __S6E3HA2_H__
#define __S6E3HA2_H__

#include <linux/types.h>
#include <linux/kernel.h>


/*
 * OFFSET ==> OFS means N-param - 1
 * <example>
 * XXX 1st param => S6E3HA2_XXX_OFS (0)
 * XXX 2nd param => S6E3HA2_XXX_OFS (1)
 * XXX 36th param => S6E3HA2_XXX_OFS (35)
 */

#define AID_INTERPOLATION
#define GAMMA_CMD_CNT 36

#define S6E3HA2_ADDR_OFS	(0)
#define S6E3HA2_ADDR_LEN	(1)
#define S6E3HA2_DATA_OFS	(S6E3HA2_ADDR_OFS + S6E3HA2_ADDR_LEN)

#ifdef CONFIG_LCD_EXTEND_HBM
#define S6E3HA2_MAX_BRIGHTNESS (EXTEND_HBM_MAX_BRIGHTNESS)
#else
#define S6E3HA2_MAX_BRIGHTNESS (HBM_MAX_BRIGHTNESS)
#endif

#define S6E3HA2_MTP_REG				0xC8
#define S6E3HA2_MTP_OFS				0
#define S6E3HA2_MTP_LEN 			35

#define S6E3HA2_DATE_REG			0xC8
#define S6E3HA2_DATE_OFS			40
#define S6E3HA2_DATE_LEN			(PANEL_DATE_LEN)

#define S6E3HA2_COORDINATE_REG		0xA1
#define S6E3HA2_COORDINATE_OFS		0
#define S6E3HA2_COORDINATE_LEN		(PANEL_COORD_LEN)

#define S6E3HA2_ID_REG				0x04
#define S6E3HA2_ID_OFS				0
#define S6E3HA2_ID_LEN				(PANEL_ID_LEN)

#define S6E3HA2_CODE_REG			0xD6
#define S6E3HA2_CODE_OFS			0
#define S6E3HA2_CODE_LEN			(PANEL_CODE_LEN)

#define S6E3HA2_ELVSS_REG			0xB6
#define S6E3HA2_ELVSS_OFS			0
#define S6E3HA2_ELVSS_LEN			22

#define S6E3HA2_ELVSS_TEMP_REG		0xB6
#define S6E3HA2_ELVSS_TEMP_OFS		21
#define S6E3HA2_ELVSS_TEMP_LEN		1

#ifdef CONFIG_SEC_FACTORY
#define S6E3HA2_OCTA_ID_REG			0xC9
#define S6E3HA2_OCTA_ID_OFS			1
#define S6E3HA2_OCTA_ID_LEN			(PANEL_OCTA_ID_LEN)
#endif

#define S6E3HA2_CHIP_ID_REG			0xD6
#define S6E3HA2_CHIP_ID_OFS			0
#define S6E3HA2_CHIP_ID_LEN			5

/* for panel dump */
#define S6E3HA2_RDDPM_REG			0x0A
#define S6E3HA2_RDDPM_OFS			0
#define S6E3HA2_RDDPM_LEN			3

#define S6E3HA2_RDDSM_REG			0x0E
#define S6E3HA2_RDDSM_OFS			0
#define S6E3HA2_RDDSM_LEN			3

#define S6E3HA2_ERR_REG				0xEA
#define S6E3HA2_ERR_OFS				0
#define S6E3HA2_ERR_LEN				5

#define S6E3HA2_ERR_FG_REG			0xEE
#define S6E3HA2_ERR_FG_OFS			0
#define S6E3HA2_ERR_FG_LEN			1

#define S6E3HA2_DSI_ERR_REG			0x05
#define S6E3HA2_DSI_ERR_OFS			0
#define S6E3HA2_DSI_ERR_LEN			1

#define S6E3HA2_SELF_DIAG_REG			0x0F
#define S6E3HA2_SELF_DIAG_OFS			0
#define S6E3HA2_SELF_DIAG_LEN			1

#ifdef CONFIG_EXYNOS_DECON_MDNIE_LITE
#define NR_S6E3HA2_MDNIE_REG	(2)

#define S6E3HA2_MDNIE_0_REG		(0xEC)
#define S6E3HA2_MDNIE_0_OFS		(0)
#define S6E3HA2_MDNIE_0_LEN		(156)

#define S6E3HA2_MDNIE_1_REG		(0xEB)
#define S6E3HA2_MDNIE_1_OFS		(S6E3HA2_MDNIE_0_OFS + S6E3HA2_MDNIE_0_LEN)
#define S6E3HA2_MDNIE_1_LEN		(3)

#define S6E3HA2_MDNIE_LEN		(S6E3HA2_MDNIE_0_LEN + S6E3HA2_MDNIE_1_LEN)

#define S6E3HA2_SCR_CR_OFS	(132)
#define S6E3HA2_SCR_WR_OFS	(150)
#define S6E3HA2_SCR_WG_OFS	(152)
#define S6E3HA2_SCR_WB_OFS	(154)
#define S6E3HA2_NIGHT_MODE_OFS	(S6E3HA2_SCR_CR_OFS)
#define S6E3HA2_NIGHT_MODE_LEN	(24)

#define S6E3HA2_TRANS_MODE_OFS	(16)
#define S6E3HA2_TRANS_MODE_LEN	(1)
#endif /* CONFIG_EXYNOS_DECON_MDNIE_LITE */

enum {
	GAMMA_MAPTBL,
	AOR_MAPTBL,
	MPS_MAPTBL,
	TSET_MAPTBL,
	ELVSS_MAPTBL,
	ELVSS_TEMP_MAPTBL,
#ifdef CONFIG_SUPPORT_XTALK_MODE
	VGH_MAPTBL,
#endif
	VINT_MAPTBL,
	ACL_ONOFF_MAPTBL,
	ACL_FRAME_AVG_MAPTBL,
	ACL_START_POINT_MAPTBL,
	ACL_OPR_MAPTBL,
	IRC_MAPTBL,
	DD_SEL_MAPTBL,
	DSC_MAPTBL,
	PPS_MAPTBL,
	SCALER_MAPTBL,
	CASET_MAPTBL,
	PASET_MAPTBL,
	LPM_NIT_MAPTBL,
	LPM_MODE_MAPTBL,
	MAX_MAPTBL,
};

enum {
	READ_ID,
	READ_COORDINATE,
	READ_CODE,
	READ_ELVSS,
	READ_ELVSS_TEMP,
	READ_MTP,
	READ_DATE,
#ifdef CONFIG_SEC_FACTORY
	READ_OCTA_ID,
#endif
	READ_CHIP_ID,
	READ_RDDPM,
	READ_RDDSM,
	READ_ERR,
	READ_ERR_FG,
	READ_DSI_ERR,
	READ_SELF_DIAG,
};

enum {
	RES_ID,
	RES_COORDINATE,
	RES_CODE,
	RES_ELVSS,
	RES_ELVSS_TEMP,
	RES_MTP,
	RES_DATE,
#ifdef CONFIG_SEC_FACTORY
	RES_OCTA_ID,
#endif
	RES_CHIP_ID,
	RES_RDDPM,
	RES_RDDSM,
	RES_ERR,
	RES_ERR_FG,
	RES_DSI_ERR,
	RES_SELF_DIAG,
};

static u8 S6E3HA2_ID[S6E3HA2_ID_LEN];
static u8 S6E3HA2_COORDINATE[S6E3HA2_COORDINATE_LEN];
static u8 S6E3HA2_CODE[S6E3HA2_CODE_LEN];
static u8 S6E3HA2_ELVSS[S6E3HA2_ELVSS_LEN];
static u8 S6E3HA2_ELVSS_TEMP[S6E3HA2_ELVSS_TEMP_LEN];
static u8 S6E3HA2_MTP[S6E3HA2_MTP_LEN];
static u8 S6E3HA2_DATE[S6E3HA2_DATE_LEN];
#ifdef CONFIG_SEC_FACTORY
static u8 S6E3HA2_OCTA_ID[S6E3HA2_OCTA_ID_LEN];
#endif

static u8 S6E3HA2_CHIP_ID[S6E3HA2_CHIP_ID_LEN];
static u8 S6E3HA2_RDDPM[S6E3HA2_RDDPM_LEN];
static u8 S6E3HA2_RDDSM[S6E3HA2_RDDSM_LEN];
static u8 S6E3HA2_ERR[S6E3HA2_ERR_LEN];
static u8 S6E3HA2_ERR_FG[S6E3HA2_ERR_FG_LEN];
static u8 S6E3HA2_DSI_ERR[S6E3HA2_DSI_ERR_LEN];
static u8 S6E3HA2_SELF_DIAG[S6E3HA2_SELF_DIAG_LEN];

static struct rdinfo s6e3ha2_rditbl[] = {
	[READ_ID] = RDINFO_INIT(id, DSI_PKT_TYPE_RD, S6E3HA2_ID_REG, S6E3HA2_ID_OFS, S6E3HA2_ID_LEN),
	[READ_COORDINATE] = RDINFO_INIT(coordinate, DSI_PKT_TYPE_RD, S6E3HA2_COORDINATE_REG, S6E3HA2_COORDINATE_OFS, S6E3HA2_COORDINATE_LEN),
	[READ_CODE] = RDINFO_INIT(code, DSI_PKT_TYPE_RD, S6E3HA2_CODE_REG, S6E3HA2_CODE_OFS, S6E3HA2_CODE_LEN),
	[READ_ELVSS] = RDINFO_INIT(elvss, DSI_PKT_TYPE_RD, S6E3HA2_ELVSS_REG, S6E3HA2_ELVSS_OFS, S6E3HA2_ELVSS_LEN),
	[READ_ELVSS_TEMP] = RDINFO_INIT(elvss_temp, DSI_PKT_TYPE_RD, S6E3HA2_ELVSS_TEMP_REG, S6E3HA2_ELVSS_TEMP_OFS, S6E3HA2_ELVSS_TEMP_LEN),
	[READ_MTP] = RDINFO_INIT(mtp, DSI_PKT_TYPE_RD, S6E3HA2_MTP_REG, S6E3HA2_MTP_OFS, S6E3HA2_MTP_LEN),
	[READ_DATE] = RDINFO_INIT(date, DSI_PKT_TYPE_RD, S6E3HA2_DATE_REG, S6E3HA2_DATE_OFS, S6E3HA2_DATE_LEN),
#ifdef CONFIG_SEC_FACTORY
	[READ_OCTA_ID] = RDINFO_INIT(octa_id, DSI_PKT_TYPE_RD, S6E3HA2_OCTA_ID_REG, S6E3HA2_OCTA_ID_OFS, S6E3HA2_OCTA_ID_LEN),
#endif
	[READ_CHIP_ID] = RDINFO_INIT(chip_id, DSI_PKT_TYPE_RD, S6E3HA2_CHIP_ID_REG, S6E3HA2_CHIP_ID_OFS, S6E3HA2_CHIP_ID_LEN),
	[READ_RDDPM] = RDINFO_INIT(rddpm, DSI_PKT_TYPE_RD, S6E3HA2_RDDPM_REG, S6E3HA2_RDDPM_OFS, S6E3HA2_RDDPM_LEN),
	[READ_RDDSM] = RDINFO_INIT(rddsm, DSI_PKT_TYPE_RD, S6E3HA2_RDDSM_REG, S6E3HA2_RDDSM_OFS, S6E3HA2_RDDSM_LEN),
	[READ_ERR] = RDINFO_INIT(err, DSI_PKT_TYPE_RD, S6E3HA2_ERR_REG, S6E3HA2_ERR_OFS, S6E3HA2_ERR_LEN),
	[READ_ERR_FG] = RDINFO_INIT(err_fg, DSI_PKT_TYPE_RD, S6E3HA2_ERR_FG_REG, S6E3HA2_ERR_FG_OFS, S6E3HA2_ERR_FG_LEN),
	[READ_DSI_ERR] = RDINFO_INIT(dsi_err, DSI_PKT_TYPE_RD, S6E3HA2_DSI_ERR_REG, S6E3HA2_DSI_ERR_OFS, S6E3HA2_DSI_ERR_LEN),
	[READ_SELF_DIAG] = RDINFO_INIT(self_diag, DSI_PKT_TYPE_RD, S6E3HA2_SELF_DIAG_REG, S6E3HA2_SELF_DIAG_OFS, S6E3HA2_SELF_DIAG_LEN),
};

static DEFINE_RESUI(id, &s6e3ha2_rditbl[READ_ID], 0);
static DEFINE_RESUI(coordinate, &s6e3ha2_rditbl[READ_COORDINATE], 0);
static DEFINE_RESUI(code, &s6e3ha2_rditbl[READ_CODE], 0);
static DEFINE_RESUI(elvss, &s6e3ha2_rditbl[READ_ELVSS], 0);
static DEFINE_RESUI(elvss_temp, &s6e3ha2_rditbl[READ_ELVSS_TEMP], 0);
static DEFINE_RESUI(mtp, &s6e3ha2_rditbl[READ_MTP], 0);
static DEFINE_RESUI(date, &s6e3ha2_rditbl[READ_DATE], 0);
#ifdef CONFIG_SEC_FACTORY
static DEFINE_RESUI(octa_id, &s6e3ha2_rditbl[READ_OCTA_ID], 0);
#endif
static DEFINE_RESUI(chip_id, &s6e3ha2_rditbl[READ_CHIP_ID], 0);
static DEFINE_RESUI(rddpm, &s6e3ha2_rditbl[READ_RDDPM], 0);
static DEFINE_RESUI(rddsm, &s6e3ha2_rditbl[READ_RDDSM], 0);
static DEFINE_RESUI(err, &s6e3ha2_rditbl[READ_ERR], 0);
static DEFINE_RESUI(err_fg, &s6e3ha2_rditbl[READ_ERR_FG], 0);
static DEFINE_RESUI(dsi_err, &s6e3ha2_rditbl[READ_DSI_ERR], 0);
static DEFINE_RESUI(self_diag, &s6e3ha2_rditbl[READ_SELF_DIAG], 0);

static struct resinfo s6e3ha2_restbl[] = {
	[RES_ID] = RESINFO_INIT(id, S6E3HA2_ID, RESUI(id)),
	[RES_COORDINATE] = RESINFO_INIT(coordinate, S6E3HA2_COORDINATE, RESUI(coordinate)),
	[RES_CODE] = RESINFO_INIT(code, S6E3HA2_CODE, RESUI(code)),
	[RES_ELVSS] = RESINFO_INIT(elvss, S6E3HA2_ELVSS, RESUI(elvss)),
	[RES_ELVSS_TEMP] = RESINFO_INIT(elvss_temp, S6E3HA2_ELVSS_TEMP, RESUI(elvss_temp)),
	[RES_MTP] = RESINFO_INIT(mtp, S6E3HA2_MTP, RESUI(mtp)),
	[RES_DATE] = RESINFO_INIT(date, S6E3HA2_DATE, RESUI(date)),
#ifdef CONFIG_SEC_FACTORY
	[RES_OCTA_ID] = RESINFO_INIT(octa_id, S6E3HA2_OCTA_ID, RESUI(octa_id)),
#endif
	[RES_CHIP_ID] = RESINFO_INIT(chip_id, S6E3HA2_CHIP_ID, RESUI(chip_id)),
	[RES_RDDPM] = RESINFO_INIT(rddpm, S6E3HA2_RDDPM, RESUI(rddpm)),
	[RES_RDDSM] = RESINFO_INIT(rddsm, S6E3HA2_RDDSM, RESUI(rddsm)),
	[RES_ERR] = RESINFO_INIT(err, S6E3HA2_ERR, RESUI(err)),
	[RES_ERR_FG] = RESINFO_INIT(err_fg, S6E3HA2_ERR_FG, RESUI(err_fg)),
	[RES_DSI_ERR] = RESINFO_INIT(dsi_err, S6E3HA2_DSI_ERR, RESUI(dsi_err)),
	[RES_SELF_DIAG] = RESINFO_INIT(self_diag, S6E3HA2_SELF_DIAG, RESUI(self_diag)),
};

enum {
	DUMP_RDDPM = 0,
	DUMP_RDDSM,
	DUMP_ERR,
	DUMP_ERR_FG,
	DUMP_DSI_ERR,
	DUMP_SELF_DIAG,
};

void show_rddpm(struct dumpinfo *info);
void show_rddsm(struct dumpinfo *info);
void show_err(struct dumpinfo *info);
void show_err_fg(struct dumpinfo *info);
void show_dsi_err(struct dumpinfo *info);
void show_self_diag(struct dumpinfo *info);

static struct dumpinfo s6e3ha2_dmptbl[] = {
	[DUMP_RDDPM] = DUMPINFO_INIT(rddpm, &s6e3ha2_restbl[RES_RDDPM], show_rddpm),
	[DUMP_RDDSM] = DUMPINFO_INIT(rddsm, &s6e3ha2_restbl[RES_RDDSM], show_rddsm),
	[DUMP_ERR] = DUMPINFO_INIT(err, &s6e3ha2_restbl[RES_ERR], show_err),
	[DUMP_ERR_FG] = DUMPINFO_INIT(err_fg, &s6e3ha2_restbl[RES_ERR_FG], show_err_fg),
	[DUMP_DSI_ERR] = DUMPINFO_INIT(dsi_err, &s6e3ha2_restbl[RES_DSI_ERR], show_dsi_err),
	[DUMP_SELF_DIAG] = DUMPINFO_INIT(self_diag, &s6e3ha2_restbl[RES_SELF_DIAG], show_self_diag),
};

int init_common_table(struct maptbl *);
int getidx_common_maptbl(struct maptbl *);
int init_gamma_table(struct maptbl *);
int getidx_dimming_maptbl(struct maptbl *);
int getidx_brt_tbl(struct maptbl *tbl);
int getidx_aor_table(struct maptbl *tbl);
int getidx_irc_table(struct maptbl *tbl);
int getidx_mps_table(struct maptbl *);
int getidx_elvss_temp_table(struct maptbl *);
#ifdef CONFIG_SUPPORT_XTALK_MODE
int getidx_vgh_table(struct maptbl *);
#endif
int getidx_vint_table(struct maptbl *);
int getidx_hbm_onoff_table(struct maptbl *);
int getidx_acl_onoff_table(struct maptbl *);
int getidx_acl_opr_table(struct maptbl *);
int getidx_dsc_table(struct maptbl *);
int getidx_resolution_table(struct maptbl *);
int getidx_dd_sel_table(struct maptbl *);
int getidx_lpm_table(struct maptbl *);
void copy_common_maptbl(struct maptbl *, u8 *);
void copy_elvss_temp_maptbl(struct maptbl *, u8 *);
void copy_tset_maptbl(struct maptbl *, u8 *);
void copy_copr_maptbl(struct maptbl *, u8 *);
#ifdef CONFIG_EXYNOS_DECON_MDNIE_LITE
int init_color_blind_table(struct maptbl *tbl);
int getidx_mdnie_scenario_maptbl(struct maptbl *tbl);
int getidx_mdnie_hmd_maptbl(struct maptbl *tbl);
int getidx_mdnie_hdr_maptbl(struct maptbl *tbl);
int getidx_mdnie_trans_mode_maptbl(struct maptbl *tbl);
int init_mdnie_night_mode_table(struct maptbl *tbl);
int getidx_mdnie_night_mode_maptbl(struct maptbl *tbl);
int init_color_coordinate_table(struct maptbl *);
int init_sensor_rgb_table(struct maptbl *tbl);
int getidx_adjust_ldu_maptbl(struct maptbl *tbl);
int getidx_color_coordinate_maptbl(struct maptbl *tbl);
void copy_color_coordinate_maptbl(struct maptbl *tbl, u8 *dst);
void copy_scr_white_maptbl(struct maptbl *tbl, u8 *dst);
int getidx_trans_maptbl(struct pkt_update_info *pktui);
int getidx_mdnie_0_maptbl(struct pkt_update_info *pktui);
int getidx_mdnie_1_maptbl(struct pkt_update_info *pktui);
int getidx_mdnie_2_maptbl(struct pkt_update_info *pktui);
int getidx_mdnie_scr_white_maptbl(struct pkt_update_info *pktui);
void update_current_scr_white(struct maptbl *tbl, u8 *dst);
#endif /* CONFIG_EXYNOS_DECON_MDNIE_LITE */
#endif /* __S6E3HA2_H__ */

