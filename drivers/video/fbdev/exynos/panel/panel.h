/*
 * linux/drivers/video/fbdev/exynos/panel/panel.h
 *
 * Header file for Samsung Common LCD Driver.
 *
 * Copyright (c) 2016 Samsung Electronics
 * Gwanghui Lee <gwanghui.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PANEL_H__
#define __PANEL_H__

#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include "panel_bl.h"
#include "dimming.h"
#include "panel_dimming.h"
#include "mdnie.h"
#include "copr.h"

#ifdef CONFIG_SEC_FACTORY
#define CONFIG_SUPPORT_PANEL_SWAP
#define CONFIG_SUPPORT_XTALK_MODE
#define CONFIG_DSC_DECODE_CRC
#endif
#define CONFIG_SUPPORT_MST
#define CONFIG_SUPPORT_GRAYSPOT_TEST



#define to_panel_device(_m_)	container_of(_m_, struct panel_device, _m_)

/* DUMMY PANEL NAME */
#define __pn_name__	xx_xx
#define __PN_NAME__	XX_XX

#define PN_CONCAT(a,b)  _PN_CONCAT(a,b)
#define _PN_CONCAT(a,b) a ## _ ## b

#define PANEL_ID_REG		(0x04)
#define PANEL_ID_LEN		(3)
#define PANEL_OCTA_ID_LEN	(20)
#define PANEL_POC_CHKSUM_LEN	(5)
#define PANEL_POC_CTRL_LEN	(4)
#define PANEL_CODE_LEN		(5)
#define PANEL_COORD_LEN		(4)
#define PANEL_DATE_LEN		(7)

#define NORMAL_TEMPERATURE			(25)
#define POWER_IS_ON(pwr)		(pwr <= FB_BLANK_NORMAL)
#define UNDER_MINUS_15(temperature)	(temperature <= -15)
#define UNDER_0(temperature)	(temperature <= 0)
#define CAPS_IS_ON(level)	(level >= 41)

struct maptbl;
struct mdnie_info;

struct common_panel_ops {
	int (*tx_cmds)(struct panel_info *, const void *, int);
	int (*rx_cmds)(struct panel_info *, u8 *, const void *, int);
#if CONFIG_OF
	int (*parse_dt)(const struct device_node *);
#endif
};

/*
 * [command types]
 * 1. delay
 * 2. external pin control
 * 3. packet write
 */

/* common cmdinfo */
struct cmdinfo {
	u32 type;
	char *name;
};

/* delay command */
struct delayinfo {
	u32 type;
	char *name;
	u32 usec;
};

#define DLYINFO(_name_) (_name_)
#define DEFINE_PANEL_UDELAY_NO_SLEEP(_name_, _usec_)	\
struct delayinfo DLYINFO(_name_) = {	\
	.name = #_name_,					\
	.type = CMD_TYPE_DELAY_NO_SLEEP,				\
	.usec = (_usec_),					\
}

#define DEFINE_PANEL_MDELAY_NO_SLEEP(_name_, _msec_)	\
struct delayinfo DLYINFO(_name_) = {	\
	.name = #_name_,					\
	.type = CMD_TYPE_DELAY_NO_SLEEP,				\
	.usec = (_msec_) * 1000,			\
}

#define DEFINE_PANEL_UDELAY(_name_, _usec_)	\
struct delayinfo DLYINFO(_name_) = {	\
	.name = #_name_,					\
	.type = CMD_TYPE_DELAY,				\
	.usec = (_usec_),					\
}

#define DEFINE_PANEL_MDELAY(_name_, _msec_)	\
struct delayinfo DLYINFO(_name_) = {	\
	.name = #_name_,					\
	.type = CMD_TYPE_DELAY,				\
	.usec = (_msec_) * 1000,			\
}

/* external pin control command */
struct pininfo {
	u32 type;
	char *name;
	int pin;
	int onoff;
};

/* packet command */
enum {
	CMD_TYPE_NONE,
	CMD_TYPE_DELAY,
	CMD_TYPE_DELAY_NO_SLEEP,
	CMD_TYPE_PINCTL,
	CMD_TYPE_TX_PKT_START,
	CMD_PKT_TYPE_NONE = CMD_TYPE_TX_PKT_START,
	SPI_PKT_TYPE_WR,
	DSI_PKT_TYPE_WR,
	DSI_PKT_TYPE_COMP,
#ifdef CONFIG_ACTIVE_CLOCK
/*Command to write ddi side ram */
	DSI_PKT_TYPE_WR_SR,
#endif
	DSI_PKT_TYPE_WR_MEM,
	DSI_PKT_TYPE_PPS,
	CMD_TYPE_TX_PKT_END = DSI_PKT_TYPE_PPS,
	CMD_TYPE_RD_PKT_START,
	SPI_PKT_TYPE_RD = CMD_TYPE_RD_PKT_START,
	DSI_PKT_TYPE_RD,
	CMD_TYPE_RX_PKT_END = DSI_PKT_TYPE_RD,
	CMD_TYPE_RES,
	CMD_TYPE_SEQ,
	CMD_TYPE_KEY,
	CMD_TYPE_MAP,
	CMD_TYPE_DMP,
	MAX_CMD_TYPE,
};

enum {
	RES_UNINITIALIZED,
	RES_INITIALIZED,
};

enum {
	KEY_NONE,
	KEY_DISABLE,
	KEY_ENABLE,
};

enum cmd_level {
	CMD_LEVEL_NONE,
	CMD_LEVEL_1,
	CMD_LEVEL_2,
	CMD_LEVEL_3,
	MAX_CMD_LEVEL,
};

/* key level command */
struct keyinfo {
	u32 type;
	char *name;
	enum cmd_level level;
	u32 en;
	struct pktinfo *packet;
};

#define KEYINFO(_name_) PN_CONCAT(key, _name_)
#define KEYINFO_INIT(_name_, _lv_, _en_, _pkt_)	\
{									\
	.name = #_name_,				\
	.type = (CMD_TYPE_KEY),			\
	.level = (_lv_),				\
	.en = (_en_),				\
	.packet = (_pkt_),				\
}

#define DEFINE_PANEL_KEY(_name_, _lv_, _en_, _pkt_)	\
struct keyinfo KEYINFO(_name_) = KEYINFO_INIT(_name_, _lv_, _en_, _pkt_)

enum cmd_mode {
	CMD_MODE_NONE,
	CMD_MODE_RO,
	CMD_MODE_WO,
	CMD_MODE_RW,
	MAX_CMD_MODE,
};

struct ldi_reg_desc {
	u8 addr;
	char *name;
	enum cmd_mode mode;
	enum cmd_level level;
	bool dirty;
};

#define MAX_LDI_REG		(0x100)
#define LDI_REG_DESC(_addr_, _name_, _mode_, _level_)	\
[(_addr_)] = {											\
	.addr = (_addr_),									\
	.name = (_name_),									\
	.mode = (_mode_),									\
	.level = (_level_),									\
	.dirty = (0),										\
}

struct pkt_update_info {
	u32 offset;
	struct maptbl *maptbl;
	u32 nr_maptbl;
	int (*getidx)(struct pkt_update_info *pktui);
	void *pdata;
};

struct pktinfo {
	u32 type;
	char *name;
	u32 addr;
	u8 *data;
	u32 dlen;
	struct pkt_update_info *pktui;
	u32 nr_pktui;
};

struct rdinfo {
	u32 type;
	char *name;
	u32 addr;
	u32 offset;
	u32 len;
	u8 data[128];
};

struct res_update_info {
	u32 offset;
	struct rdinfo *rditbl;
};

struct resinfo {
	u32 type;
	char *name;
	int state;
	u8 *data;
	u32 dlen;
	struct res_update_info *resui;
	u32 nr_resui;
};

struct dumpinfo {
	u32 type;
	char *name;
	struct resinfo *res;
	void (*callback)(struct dumpinfo *info);
};

#define DUMPINFO_INIT(_name_, _res_, _callback_)	\
{											\
	.name = #_name_,						\
	.type = (CMD_TYPE_DMP),					\
	.res = (_res_),							\
	.callback = (_callback_),				\
}

#define RESUI(_name_) PN_CONCAT(resui, _name_)
#define RESINFO(_name_) PN_CONCAT(res, _name_)
#define RDINFO(_name_) PN_CONCAT(rd, _name_)

#define RDINFO_INIT(_name_, _type_, _addr_, _offset_, _len_)	\
{											\
	.name = #_name_,						\
	.type = (_type_),						\
	.addr = (_addr_),						\
	.offset = (_offset_),					\
	.len = (_len_),							\
}

#define DEFINE_RDINFO(_name_, _type_, _addr_, _offset_, _len_)	\
struct rdinfo RDINFO(_name_) = RDINFO_INIT(_name_, _type_, _addr_, _offset_, _len_)

#define RESUI_INIT(_rditbl_, _offset_)\
{												\
	.offset = (_offset_),						\
	.rditbl = (_rditbl_),						\
}

#define DEFINE_RESUI(_name_, _rditbl_, _offset_)	\
struct res_update_info RESUI(_name_)[] =			\
{													\
	RESUI_INIT(_rditbl_, _offset_),		\
}

#define RESINFO_INIT(_name_, _arr_, _resui_)	\
{												\
	.name = #_name_,							\
	.type = (CMD_TYPE_RES),						\
	.state = (RES_UNINITIALIZED),				\
	.data = (_arr_),							\
	.dlen = ARRAY_SIZE((_arr_)),				\
	.resui = (_resui_),							\
	.nr_resui = (ARRAY_SIZE(_resui_)),			\
}

#define DEFINE_RESOURCE(_name_, _arr_, _resui_)	\
struct resinfo RESINFO(_name_) = RESINFO_INIT(_name_, _arr_, _resui_)

#define PKTUI(_name_) PN_CONCAT(pktui, _name_)
#define PKTINFO(_name_) PN_CONCAT(pkt, _name_)

#define DEFINE_PKTUI(_name_, _maptbl_, _offset_)	\
struct pkt_update_info PKTUI(_name_)[] =			\
{													\
	{												\
		.offset = (_offset_),						\
		.maptbl = (_maptbl_),						\
		.nr_maptbl = 1,								\
		.getidx = NULL,								\
	},												\
}

#define PKTINFO_INIT(_name_, _type_, _arr_, _pktui_, _nr_pktui_)	\
{												\
	.name = #_name_,							\
	.type = (_type_),							\
	.data = (_arr_),							\
	.dlen = ARRAY_SIZE((_arr_)),				\
	.pktui = (_pktui_),							\
	.nr_pktui = (_nr_pktui_),					\
}

#define DEFINE_PACKET(_name_, _type_, _arr_, _pktui_, _nr_pktui_)	\
struct pktinfo PKTINFO(_name_) = PKTINFO_INIT(_name_, _type_, _arr_, _pktui_, _nr_pktui_)

#define DEFINE_VARIABLE_PACKET(_name_, _type_, _arr_, _maptbl_, _offset_)	\
static DEFINE_PKTUI(_name_, _maptbl_, _offset_);						\
static DEFINE_PACKET(_name_, _type_, _arr_, PKTUI(_name_), ARRAY_SIZE(PKTUI(_name_)))

#define DEFINE_STATIC_PACKET(_name_, _type_, _arr_)		\
static DEFINE_PACKET(_name_, _type_, _arr_, NULL, 0)

#define PN_MAP_DATA_TABLE(_name_)	PN_CONCAT(PN_CONCAT(__pn_name__, _name_), table)
#define PN_CMD_DATA_TABLE(_name_)	PN_CONCAT(__PN_NAME__, _name_)

#define DEFINE_PN_STATIC_PACKET(_name_, _type_, _arr_)			\
static DEFINE_PACKET(PN_CONCAT(__pn_name__, _name_), _type_,	\
		PN_CONCAT(__PN_NAME__, _arr_), NULL, 0)

/* structure of mapping table */
struct maptbl_ops {
	int (*init)(struct maptbl *tbl);
	int (*getidx)(struct maptbl *tbl);
	void (*copy)(struct maptbl *tbl, u8 *dst);
};

struct maptbl {
	u32 type;
	char *name;
	int nlayer;
	int nrow;
	int ncol;
	int sz_copy;
	u8 *arr;
	void *pdata;
	struct maptbl_ops ops;
};

#define MAPINFO(_name_) (_name_)

#define sizeof_maptbl(tbl) \
	((tbl)->nlayer * (tbl)->nrow * (tbl)->ncol)

#define sizeof_layer(tbl) \
	((tbl)->nrow * (tbl)->ncol)

#define sizeof_row(tbl) \
	((tbl)->ncol)

#define for_each_maptbl(tbl, __pos__)		\
	for ((__pos__) = 0;						\
		(__pos__) < (sizeof_maptbl(tbl));	\
		(__pos__)++)

#define for_each_layer(tbl, __pos__)		\
	for ((__pos__) = 0;						\
		(__pos__) < ((tbl)->nlayer);		\
		(__pos__)++)

#define for_each_row(tbl, __pos__)			\
	for ((__pos__) = 0;						\
		(__pos__) < ((tbl)->nrow);			\
		(__pos__)++)

#define for_each_col(tbl, __pos__)			\
	for ((__pos__) = 0;						\
		(__pos__) < ((tbl)->ncol);			\
		(__pos__)++)

#define DEFINE_MAPTBL(_name_, _arr_, _nlayer_, _nrow_, _ncol_, _sz_copy_, f_init, f_getidx, f_copy)  \
{                                       \
	.type = (CMD_TYPE_MAP),				\
	.name = _name_,                     \
	.arr = (u8 *)(_arr_),               \
	.nlayer = (_nlayer_),               \
	.nrow = (_nrow_),                   \
	.ncol = (_ncol_),                   \
	.sz_copy = (_sz_copy_),             \
	.pdata = NULL,                      \
	.ops = {                            \
		.init = (f_init),               \
		.getidx = (f_getidx),           \
		.copy = (f_copy),               \
	},                                  \
}

#define DEFINE_0D_MAPTBL(_name_, f_init, f_getidx, f_copy)  \
{                                       \
	.type = (CMD_TYPE_MAP),				\
	.name = #_name_,                    \
	.arr = NULL,                        \
	.nlayer = 0,                        \
	.nrow = 0,                          \
	.ncol = 0,                          \
	.sz_copy = 0,                       \
	.pdata = NULL,                      \
	.ops = {                            \
		.init = (f_init),               \
		.getidx = (f_getidx),           \
		.copy = (f_copy),               \
	},                                  \
}

#define DEFINE_1D_MAPTBL(_name_, _size_, f_init, f_getidx, f_copy)  \
{                                       \
	.type = (CMD_TYPE_MAP),				\
	.name = #_name_,                    \
	.arr = (u8 *)(_name_),              \
	.nlayer = 1,                        \
	.nrow = ARRAY_SIZE((_name_)),       \
	.ncol = (_size_),                   \
	.sz_copy = (_size_),                \
	.pdata = NULL,                      \
	.ops = {                            \
		.init = (f_init),               \
		.getidx = (f_getidx),           \
		.copy = (f_copy),               \
	},                                  \
}

#define DEFINE_2D_MAPTBL(_name_, f_init, f_getidx, f_copy)  \
{                                       \
	.type = (CMD_TYPE_MAP),				\
	.name = #_name_,                    \
	.arr = (u8 *)_name_,                \
	.nlayer = 1,                        \
	.nrow = ARRAY_SIZE(_name_),         \
	.ncol = ARRAY_SIZE((_name_)[0]),    \
	.sz_copy = ARRAY_SIZE((_name_)[0]), \
	.pdata = NULL,                      \
	.ops = {                            \
		.init = f_init,                 \
		.getidx = f_getidx,             \
		.copy = f_copy,                 \
	},                                  \
}

#define DEFINE_3D_MAPTBL(_name_, f_init, f_getidx, f_copy)  \
{                                       \
	.type = (CMD_TYPE_MAP),				\
	.name = #_name_,                    \
	.arr = (u8 *)_name_,                \
	.nlayer = ARRAY_SIZE(_name_),       \
	.nrow = ARRAY_SIZE((_name_)[0]),      \
	.ncol = ARRAY_SIZE((_name_)[0][0]),   \
	.sz_copy = ARRAY_SIZE((_name_)[0][0]),\
	.pdata = NULL,                      \
	.ops = {                            \
		.init = f_init,                 \
		.getidx = f_getidx,             \
		.copy = f_copy,                 \
	},                                  \
}

u32 maptbl_index(struct maptbl *, int, int, int);
int maptbl_init(struct maptbl *);
int maptbl_getidx(struct maptbl *);
void maptbl_copy(struct maptbl *, u8 *);
u8 *maptbl_getptr(struct maptbl *tbl);

enum PANEL_SEQ {
	PANEL_INIT_SEQ,
	PANEL_EXIT_SEQ,
	PANEL_RES_INIT_SEQ,
	PANEL_MAP_INIT_SEQ,
	PANEL_DISPLAY_ON_SEQ,
	PANEL_DISPLAY_OFF_SEQ,
	PANEL_ALPM_ENTER_SEQ,
	PANEL_ALPM_DELAY_SEQ,
	PANEL_ALPM_EXIT_SEQ,
	PANEL_SET_BL_SEQ,
#ifdef CONFIG_SUPPORT_HMD
	PANEL_HMD_ON_SEQ,
	PANEL_HMD_OFF_SEQ,
	PANEL_HMD_BL_SEQ,
#endif
	PANEL_HBM_ON_SEQ,
	PANEL_HBM_OFF_SEQ,
#ifdef CONFIG_SUPPORT_DSU
	PANEL_DSU_SEQ,
#endif
	PANEL_MCD_ON_SEQ,
	PANEL_MCD_OFF_SEQ,
#ifdef CONFIG_ACTIVE_CLOCK
	PANEL_ACTIVE_CLK_IMG_SEQ,
	PANEL_ACTIVE_CLK_CTRL_SEQ,
	PANEL_ACTIVE_CLK_UPDATE_SEQ,
#endif
#ifdef CONFIG_SUPPORT_MST
	PANEL_MST_ON_SEQ,
	PANEL_MST_OFF_SEQ,
#endif
#ifdef CONFIG_SUPPORT_GRAM_CHECKSUM
	PANEL_GCT_ENTER_SEQ,
	PANEL_GCT_VDDM_SEQ,
	PANEL_GCT_IMG_UPDATE_SEQ,
	PANEL_GCT_EXIT_SEQ,
#endif
#ifdef CONFIG_SUPPORT_GRAYSPOT_TEST
	PANEL_GRAYSPOT_ON_SEQ,
	PANEL_GRAYSPOT_OFF_SEQ,
#endif
#ifdef CONFIG_DSC_DECODE_CRC
	PANEL_DSC_DECODE_CRC,
#endif
	PANEL_DUMP_SEQ,
	PANEL_DUMMY_SEQ,
	MAX_PANEL_SEQ,
};

/* structure of sequence table */
struct seqinfo {
	u32 type;
	char *name;
	void **cmdtbl;
	u32 size;
};

#define SEQINFO(_name_)	(_name_)
#define SEQINFO_INIT(_name_, _cmdtbl_)	\
{										\
	.type = (CMD_TYPE_SEQ),				\
	.name = (_name_),					\
	.cmdtbl = (_cmdtbl_),				\
	.size = ARRAY_SIZE((_cmdtbl_)),		\
}

struct brt_map {
	int brt;
	int lum;
};

struct common_panel_info {
	char *ldi_name;
	char *name;
	char *model;
	char *vendor;
	u32 id;
	u32 rev;
	struct maptbl *maptbl;
	int nr_maptbl;
	struct seqinfo *seqtbl;
	int nr_seqtbl;
	struct rdinfo *rditbl;
	int nr_rditbl;
	struct resinfo *restbl;
	int nr_restbl;
	struct dumpinfo *dumpinfo;
	int nr_dumpinfo;
	struct mdnie_tune *mdnie_tune;
	struct panel_dimming_info *panel_dim_info[2];
#ifdef CONFIG_EXYNOS_DECON_LCD_COPR
	struct panel_copr_data *copr_data;
#endif
};

enum {
	OVER_ZERO,
	UNDER_ZERO,
	UNDER_MINUS_FIFTEEN,
	DIM_OVER_ZERO,
	DIM_UNDER_ZERO,
	DIM_UNDER_MINUS_FIFTEEN,
	TEMP_MAX,
};

enum {
	POC_ONOFF_OFF,
	POC_ONOFF_ON,
	POC_ONOFF_MAX
};

enum {
	IRC_ONOFF_OFF,
	IRC_ONOFF_ON,
	IRC_ONOFF_MAX
};

enum {
	ACL_PWRSAVE_OFF,
	ACL_PWRSAVE_ON,
	MAX_ACL_PWRSAVE,
};

enum {
	ACL_OPR_OFF,
	ACL_OPR_03P,
	ACL_OPR_06P,
	ACL_OPR_08P,
	ACL_OPR_12P,
	ACL_OPR_15P,
	ACL_OPR_MAX
};

enum {
	ALPM_OFF = 0,
	ALPM_LOW_BR,
	HLPM_LOW_BR,
	ALPM_HIGH_BR,
	HLPM_HIGH_BR,
};

#ifdef CONFIG_SUPPORT_XTALK_MODE
enum {
	XTALK_OFF,
	XTALK_ON,
};
#endif

#ifdef CONFIG_SUPPORT_GRAM_CHECKSUM
enum {
	GRAM_CHKSUM_OFF,
	GRAM_CHKSUM_LV_TEST_1,
	GRAM_CHKSUM_LV_TEST_2,
	GRAM_CHKSUM_HV_TEST_1,
	GRAM_CHKSUM_HV_TEST_2,
	MAX_GRAM_CHKSUM,
};

enum {
	GCT_PATTERN_NONE,
	GCT_PATTERN_1,
	GCT_PATTERN_2,
	MAX_GCT_PATTERN,
};

enum {
	VDDM_ORIG,
	VDDM_LV,
	VDDM_HV,
	MAX_VDDM,
};

enum {
	GRAM_TEST_OFF,
	GRAM_TEST_ON
};
#endif

#define MAX_PANEL (32)
#define MAX_PANEL_LUT (128)

enum {
	PANEL_ID,
	PANEL_MASK,
	PANEL_INDEX,
};

struct panel_lut {
	u32 id;
	u32 mask;
	u32 index;
};

struct panel_lut_info {
	const char *names[MAX_PANEL];
	int nr_panel;
	struct panel_lut lut[MAX_PANEL_LUT];
	int nr_lut;
};

struct panel_properties {
	s32 temperature;
	u32 siop_enable;
	u32 adaptive_control;
	u32 alpm_mode;
	u32 cur_alpm_mode;
	u32 mcd_on;
#ifdef CONFIG_SUPPORT_MST
	u32 mst_on;
#endif
	u32 hmd_on;
	u32 hmd_brightness;
	u32 lux;
#ifdef CONFIG_SUPPORT_XTALK_MODE
	u32 xtalk_mode;
#endif
#ifdef CONFIG_SUPPORT_POC_FLASH
	u32 poc_op;
#endif
#ifdef CONFIG_SUPPORT_GRAM_CHECKSUM
	u32 gct_on;
	u32 gct_vddm;
	u32 gct_pattern;
	u8 gct_valid_chksum;
#endif
#ifdef CONFIG_SUPPORT_GRAYSPOT_TEST
	u32 grayspot;
#endif
	u32 poc_onoff;
	u32 irc_onoff;
	u32 key[MAX_CMD_LEVEL];
};

struct panel_info {
	const char *ldi_name;
	unsigned char id[3];
	struct panel_properties props;

	/* platform dependent data - ex> exynos : dsim_device */
	void *pdata;
	void *dim_info[2];
	struct maptbl *maptbl;
	int nr_maptbl;
	struct seqinfo *seqtbl;
	int nr_seqtbl;
	struct rdinfo *rditbl;
	int nr_rditbl;
	struct dumpinfo *dumpinfo;
	int nr_dumpinfo;
	struct resinfo *restbl;
	int nr_restbl;
	struct panel_dimming_info *panel_dim_info[2];
	struct panel_lut_info lut_info;
};

#define __PANEL_ATTR_RO(_name, _mode) __ATTR(_name, _mode,		\
			 PN_CONCAT(_name, show), NULL)

#define __PANEL_ATTR_WO(_name, _mode) __ATTR(_name, _mode,		\
			 NULL, PN_CONCAT(_name, store))

#define __PANEL_ATTR_RW(_name, _mode) __ATTR(_name, _mode,		\
			 PN_CONCAT(_name, show), PN_CONCAT(_name, store))

void print_data(char *data, int len);
int register_common_panel(struct common_panel_info *);
struct common_panel_info *find_panel(struct panel_device *, u32);
void print_panel_lut(struct panel_lut_info *);
struct seqinfo *find_panel_seqtbl(struct panel_info *, char *);
struct seqinfo *find_index_seqtbl(struct panel_info *, u32);
struct pktinfo *find_packet(struct seqinfo *seqtbl, char *name);
struct pktinfo *find_packet_suffix(struct seqinfo *seqtbl, char *name);
struct maptbl *find_panel_maptbl_by_index(struct panel_info *, int);
struct maptbl *find_panel_maptbl_by_name(struct panel_info *, char *);
struct panel_dimming_info *find_panel_dimming(struct panel_info *, char *);
int panel_do_seqtbl(struct panel_device *, struct seqinfo *);
int panel_do_seqtbl_by_name(struct panel_device *, char *name);
int panel_do_seqtbl_by_index(struct panel_device *, int);

int copy_panel_read_data(u8 *, u32, u32, u32);
struct resinfo *find_panel_resource(struct panel_info *panel, char *name);
int rescpy(u8 *dst, struct resinfo *res, u32 offset, u32 len);
int rescpy_by_name(struct panel_info *panel, u8 *dst, char *name, u32 offset, u32 len);
int resource_copy(u8 *dst, struct resinfo *res);
int resource_copy_by_name(struct panel_info *panel, u8 *dst, char *name);
int panel_resource_update(struct panel_device *panel, struct resinfo *res);
int panel_resource_update_by_name(struct panel_device *panel, char *name);
int panel_dumpinfo_update(struct panel_device *panel, struct dumpinfo *info);
int panel_rx_nbytes(struct panel_device *panel, u32 type, u8 *buf, u8 addr, u8 pos, u8 len);
int panel_verify_tx_packet(struct panel_device *panel, u8 *table, u8 len);

int panel_set_key(struct panel_device *panel, int level, bool on);
struct pktinfo *alloc_static_packet(char *, u32, u8 *, u32);
int check_panel_active(struct panel_device *panel, const char *caller);
int read_panel_id(struct panel_device *panel, u8 *buf);
int panel_resource_prepare(struct panel_device *panel);
void print_panel_resource(struct panel_device *panel);
#ifdef CONFIG_EXYNOS_DECON_LCD_SYSFS
int panel_sysfs_probe(struct panel_device *panel);
#else
static inline int panel_sysfs_probe(struct panel_device *panel) { return 0; }
#endif
#define IS_PANEL_ACTIVE(_panel) check_panel_active(_panel, __func__)
#endif /* __PANEL_H__ */
