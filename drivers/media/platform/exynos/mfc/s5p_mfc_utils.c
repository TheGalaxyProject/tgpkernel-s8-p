/*
 * drivers/media/platform/exynos/mfc/s5p_mfc_utils.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/smc.h>

#include "s5p_mfc_utils.h"

#include "s5p_mfc_mem.h"

int s5p_mfc_check_vb_with_fmt(struct s5p_mfc_fmt *fmt, struct vb2_buffer *vb)
{
	int i;

	if (!fmt)
		return -EINVAL;

	if (fmt->mem_planes != vb->num_planes) {
		mfc_err_dev("plane number is different (%d != %d)\n",
				fmt->mem_planes, vb->num_planes);
		return -EINVAL;
	}

	for (i = 0; i < vb->num_planes; i++) {
		if (!s5p_mfc_mem_get_daddr_vb(vb, i)) {
			mfc_err_dev("failed to get plane cookie\n");
			return -ENOMEM;
		}

		mfc_debug(2, "index: %d, plane[%d] cookie: 0x%08llx\n",
				vb->index, i,
				s5p_mfc_mem_get_daddr_vb(vb, i));
	}

	return 0;
}

int s5p_mfc_stream_buf_prot(struct s5p_mfc_ctx *ctx,
				struct s5p_mfc_buf *buf, bool en)
{
	return 0;
}

int s5p_mfc_raw_buf_prot(struct s5p_mfc_ctx *ctx,
				struct s5p_mfc_buf *buf, bool en)
{
	return 0;
}

static int mfc_calc_plane(int width, int height, int is_tiled)
{
	int mbX, mbY;

	mbX = (width + 15)/16;
	mbY = (height + 15)/16;

	/* Alignment for interlaced processing */
	if (is_tiled)
		mbY = (mbY + 1) / 2 * 2;

	return (mbX * 16) * (mbY * 16);
}

static void mfc_set_linear_stride_size(struct s5p_mfc_ctx *ctx,
				struct s5p_mfc_fmt *fmt)
{
	struct s5p_mfc_raw_info *raw;
	int i;

	raw = &ctx->raw_buf;

	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YUV420N:
	case V4L2_PIX_FMT_YVU420M:
		raw->stride[0] = ALIGN(ctx->img_width, 16);
		raw->stride[1] = ALIGN(raw->stride[0] >> 1, 16);
		raw->stride[2] = ALIGN(raw->stride[0] >> 1, 16);
		break;
	case V4L2_PIX_FMT_NV12MT_16X16:
	case V4L2_PIX_FMT_NV12MT:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV12N:
	case V4L2_PIX_FMT_NV12N_10B:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		raw->stride[0] = ALIGN(ctx->img_width, 16);
		raw->stride[1] = ALIGN(ctx->img_width, 16);
		raw->stride[2] = 0;
		break;
	case V4L2_PIX_FMT_RGB24:
		ctx->raw_buf.stride[0] = ctx->img_width * 3;
		ctx->raw_buf.stride[1] = 0;
		ctx->raw_buf.stride[2] = 0;
		break;
	case V4L2_PIX_FMT_RGB565:
		ctx->raw_buf.stride[0] = ctx->img_width * 2;
		ctx->raw_buf.stride[1] = 0;
		ctx->raw_buf.stride[2] = 0;
		break;
	case V4L2_PIX_FMT_RGB32X:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_ARGB32:
		ctx->raw_buf.stride[0] = (ctx->buf_stride > ctx->img_width) ?
			(ALIGN(ctx->img_width, 16) * 4) : (ctx->img_width * 4);
		ctx->raw_buf.stride[1] = 0;
		ctx->raw_buf.stride[2] = 0;
		break;
	default:
		break;
	}

	/* Decoder needs multiple of 16 alignment for stride */
	if (ctx->type == MFCINST_DECODER) {
		for (i = 0; i < 3; i++)
			ctx->raw_buf.stride[i] =
				ALIGN(ctx->raw_buf.stride[i], 16);
	}
}

void s5p_mfc_dec_calc_dpb_size(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev;
	struct s5p_mfc_dec *dec;
	struct s5p_mfc_raw_info *raw, *tiled_ref;
	int i;

	if (!ctx) {
		mfc_err_dev("no mfc context to run\n");
		return;
	}

	dev = ctx->dev;
	raw = &ctx->raw_buf;
	raw->total_plane_size = 0;

	dec = ctx->dec_priv;
	tiled_ref = &dec->tiled_ref;

	switch (ctx->dst_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12MT_16X16:
		raw->plane_size[0] =
			mfc_calc_plane(ctx->img_width, ctx->img_height, 1);
		raw->plane_size[1] =
			mfc_calc_plane(ctx->img_width, (ctx->img_height >> 1), 1);
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
		raw->plane_size[0] =
			mfc_calc_plane(ctx->img_width, ctx->img_height, 0);
		raw->plane_size[1] =
			mfc_calc_plane(ctx->img_width, (ctx->img_height >> 1), 0);
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_NV12N:
	case V4L2_PIX_FMT_NV12N_10B:
		raw->plane_size[0] = NV12N_Y_SIZE(ctx->img_width, ctx->img_height);
		raw->plane_size[1] = NV12N_CBCR_SIZE(ctx->img_width, ctx->img_height);
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
		raw->plane_size[0] =
			mfc_calc_plane(ctx->img_width, ctx->img_height, 0);
		raw->plane_size[1] =
			mfc_calc_plane(ctx->img_width >> 1, ctx->img_height >> 1, 0);
		raw->plane_size[2] =
			mfc_calc_plane(ctx->img_width >> 1, ctx->img_height >> 1, 0);
		break;
	case V4L2_PIX_FMT_YUV420N:
		raw->plane_size[0] = YUV420N_Y_SIZE(ctx->img_width, ctx->img_height);
		raw->plane_size[1] = YUV420N_CB_SIZE(ctx->img_width, ctx->img_height);
		raw->plane_size[2] = YUV420N_CR_SIZE(ctx->img_width, ctx->img_height);
		break;
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		raw->plane_size[0] =
			mfc_calc_plane(ctx->img_width, ctx->img_height, 0) + 64;
		raw->plane_size[1] =
			mfc_calc_plane(ctx->img_width, ctx->img_height, 0) + 64;
		raw->plane_size[2] = 0;
		break;
	default:
		raw->plane_size[0] = 0;
		raw->plane_size[1] = 0;
		raw->plane_size[2] = 0;
		mfc_err_ctx("Invalid pixelformat : %s\n", ctx->dst_fmt->name);
		break;
	}

	mfc_set_linear_stride_size(ctx, ctx->dst_fmt);

	switch (ctx->dst_fmt->fourcc) {
		case V4L2_PIX_FMT_NV12M:
		case V4L2_PIX_FMT_NV21M:
			raw->plane_size[0] += 64;
			raw->plane_size[1] =
				(((ctx->img_width + 15)/16)*16)
				*(((ctx->img_height + 15)/16)*8) + 64;
			break;
		case V4L2_PIX_FMT_YUV420M:
		case V4L2_PIX_FMT_YVU420M:
			raw->plane_size[0] += 64;
			raw->plane_size[1] =
				(((ctx->img_width + 15)/16)*16)
				*(((ctx->img_height + 15)/16)*8) + 64;
			raw->plane_size[2] =
				(((ctx->img_width + 15)/16)*16)
				*(((ctx->img_height + 15)/16)*8) + 64;
			break;
		default:
			break;
	}

	for (i = 0; i < raw->num_planes; i++) {
		if (raw->plane_size[i] < ctx->min_dpb_size[i]) {
			mfc_info_dev("plane[%d] size is changed %d -> %d\n",
					i, raw->plane_size[i], ctx->min_dpb_size[i]);
			raw->plane_size[i] = ctx->min_dpb_size[i];
		}
	}

	if (ctx->is_10bit) {
		switch (ctx->dst_fmt->fourcc) {
			case V4L2_PIX_FMT_NV12M:
			case V4L2_PIX_FMT_NV21M:
				raw->stride_2bits[0] = ALIGN(ctx->img_width / 4, 16);
				raw->stride_2bits[1] = ALIGN(ctx->img_width / 4, 16);
				raw->stride_2bits[2] = 0;
				raw->plane_size_2bits[0] =
					ALIGN(ctx->img_width / 4, 16) * ctx->img_height + 64;
				raw->plane_size_2bits[1] =
					ALIGN(ctx->img_width / 4, 16) * (ctx->img_height / 2) + 64;
				raw->plane_size_2bits[2] = 0;
				break;
			case V4L2_PIX_FMT_NV12N_10B:
				raw->stride_2bits[0] = ALIGN(ctx->img_width / 4, 16);
				raw->stride_2bits[1] = ALIGN(ctx->img_width / 4, 16);
				raw->stride_2bits[2] = 0;
				raw->plane_size_2bits[0] = NV12N_10B_Y_2B_SIZE(ctx->img_width, ctx->img_height);
				raw->plane_size_2bits[1] = NV12N_10B_CBCR_2B_SIZE(ctx->img_width, ctx->img_height);
				raw->plane_size_2bits[2] = 0;
				break;
			default:
				mfc_err_ctx("HEVC 10bit: not supported format: %s\n",
						ctx->dst_fmt->name);
				break;
		}
	}

	for (i = 0; i < raw->num_planes; i++) {
		raw->total_plane_size += raw->plane_size[i];
		mfc_debug(2, "Plane[%d] size = %d, stride = %d\n",
			i, raw->plane_size[i], raw->stride[i]);
	}
	if (ctx->is_10bit) {
		for (i = 0; i < raw->num_planes; i++) {
			raw->total_plane_size += raw->plane_size_2bits[i];
			mfc_debug(2, "Plane[%d] 2bit size = %d, stride = %d\n",
					i, raw->plane_size_2bits[i],
					raw->stride_2bits[i]);
		}
	}
	mfc_debug(2, "total plane size: %d\n", raw->total_plane_size);

	if (IS_H264_DEC(ctx) || IS_H264_MVC_DEC(ctx)) {
		ctx->mv_size = DEC_MV_SIZE_MB(ctx->img_width, ctx->img_height);
		ctx->mv_size = ALIGN(ctx->mv_size, 32);
	} else if (IS_HEVC_DEC(ctx)) {
		ctx->mv_size = DEC_HEVC_MV_SIZE(ctx->img_width, ctx->img_height);
		ctx->mv_size = ALIGN(ctx->mv_size, 32);
	} else {
		ctx->mv_size = 0;
	}

}

void s5p_mfc_enc_calc_src_size(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev;
	struct s5p_mfc_raw_info *raw;
	unsigned int mb_width, mb_height, default_size;
	int i, add_size;

	if (!ctx) {
		mfc_err_dev("no mfc context to run\n");
		return;
	}

	dev = ctx->dev;
	raw = &ctx->raw_buf;
	raw->total_plane_size = 0;
	mb_width = WIDTH_MB(ctx->img_width);
	mb_height = HEIGHT_MB(ctx->img_height);
	add_size = MFC_LINEAR_BUF_SIZE;
	default_size = mb_width * mb_height * 256;

	switch (ctx->src_fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YUV420N:
	case V4L2_PIX_FMT_YVU420M:
		ctx->raw_buf.plane_size[0] = ALIGN(default_size, 256);
		ctx->raw_buf.plane_size[1] = ALIGN(default_size >> 2, 256);
		ctx->raw_buf.plane_size[2] = ALIGN(default_size >> 2, 256);
		break;
	case V4L2_PIX_FMT_NV12MT_16X16:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV12N:
	case V4L2_PIX_FMT_NV21M:
		raw->plane_size[0] = ALIGN(default_size, 256);
		raw->plane_size[1] = ALIGN(default_size / 2, 256);
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		raw->plane_size[0] = ALIGN(default_size, 256);
		raw->plane_size[1] = ALIGN(default_size, 256);
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_RGB565:
		raw->plane_size[0] = ALIGN(default_size * 2, 256);
		raw->plane_size[1] = 0;
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_RGB24:
		raw->plane_size[0] = ALIGN(default_size * 3, 256);
		raw->plane_size[1] = 0;
		raw->plane_size[2] = 0;
		break;
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_RGB32X:
	case V4L2_PIX_FMT_ARGB32:
		raw->plane_size[0] = ALIGN(default_size * 4, 256);
		raw->plane_size[1] = 0;
		raw->plane_size[2] = 0;
		break;
	default:
		raw->plane_size[0] = 0;
		raw->plane_size[1] = 0;
		raw->plane_size[2] = 0;
		mfc_err_ctx("Invalid pixel format(%d)\n", ctx->src_fmt->fourcc);
		break;
	}

	/* Add extra if necessary */
	for (i = 0; i < raw->num_planes; i++)
		raw->plane_size[i] += add_size;

	for (i = 0; i < raw->num_planes; i++) {
		raw->total_plane_size += raw->plane_size[i];
		mfc_debug(2, "Plane[%d] size = %d, stride = %d\n",
			i, raw->plane_size[i], raw->stride[i]);
	}
	mfc_debug(2, "total plane size: %d\n", raw->total_plane_size);

	mfc_set_linear_stride_size(ctx, ctx->src_fmt);
}

void s5p_mfc_cleanup_assigned_dpb(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dec *dec;
	struct s5p_mfc_buf *dst_mb;
	int i;

	if (!ctx) {
		mfc_err_dev("no mfc context to run\n");
		return;
	}

	dec = ctx->dec_priv;
	if (!dec) {
		mfc_err_dev("no mfc decoder to run\n");
		return;
	}

	if (ctx->is_drm && ctx->raw_protect_flag) {
		mfc_debug(2, "raw_protect_flag(%#lx) will be released\n",
				ctx->raw_protect_flag);
		for (i = 0; i < MFC_MAX_DPBS; i++) {
			if (test_bit(i, &ctx->raw_protect_flag)) {
				dst_mb = dec->assigned_dpb[i];
				if (s5p_mfc_raw_buf_prot(ctx, dst_mb, false))
					mfc_err_ctx("failed to CFW_UNPROT\n");
				else
					clear_bit(i, &ctx->raw_protect_flag);
				mfc_debug(2, "[%d] dec dst buf un-prot_flag: %#lx\n",
						i, ctx->raw_protect_flag);
			}
		}
		s5p_mfc_clear_assigned_dpb(ctx);
	}
}

void s5p_mfc_unprotect_released_dpb(struct s5p_mfc_ctx *ctx, unsigned int released_flag)
{
	struct s5p_mfc_dec *dec;
	struct s5p_mfc_buf *dst_mb;
	int i;

	if (!ctx) {
		mfc_err_dev("no mfc context to run\n");
		return;
	}

	dec = ctx->dec_priv;
	if (!dec) {
		mfc_err_dev("no mfc decoder to run\n");
		return;
	}

	if (ctx->is_drm) {
		for (i = 0; i < MFC_MAX_DPBS; i++) {
			if (released_flag & (1 << i)) {
				dst_mb = dec->assigned_dpb[i];
				if (test_bit(i, &ctx->raw_protect_flag)) {
					if (s5p_mfc_raw_buf_prot(ctx, dst_mb, false))
						mfc_err_ctx("failed to CFW_UNPROT\n");
					else
						clear_bit(i, &ctx->raw_protect_flag);
				}
				mfc_debug(2, "[%d] dec dst buf un-prot_flag: %#lx\n",
						i, ctx->raw_protect_flag);
			}
		}
	}

}

void s5p_mfc_protect_dpb(struct s5p_mfc_ctx *ctx, struct s5p_mfc_buf *dst_mb)
{
	struct s5p_mfc_dec *dec;
	int dst_index;

	if (!ctx) {
		mfc_err_dev("no mfc context to run\n");
		return;
	}

	dec = ctx->dec_priv;
	if (!dec) {
		mfc_err_dev("no mfc decoder to run\n");
		return;
	}

	dst_index = dst_mb->vb.vb2_buf.index;

	if (ctx->is_drm) {
		dec->assigned_dpb[dst_index] = dst_mb;
		if (!test_bit(dst_index, &ctx->raw_protect_flag)) {
			if (s5p_mfc_raw_buf_prot(ctx, dst_mb, true))
				mfc_err_ctx("failed to CFW_PROT\n");
			else
				set_bit(dst_index, &ctx->raw_protect_flag);
		}
		mfc_debug(2, "[%d] dec dst buf prot_flag: %#lx\n",
				dst_index, ctx->raw_protect_flag);
	}
}

void s5p_mfc_watchdog_tick(unsigned long arg)
{
	struct s5p_mfc_dev *dev = (struct s5p_mfc_dev *)arg;

	if (!dev) {
		mfc_err_dev("no mfc device to run\n");
		return;
	}

	mfc_debug(5, "watchdog is ticking!\n");

	if (atomic_read(&dev->watchdog_tick_running))
		atomic_inc(&dev->watchdog_tick_cnt);
	else
		atomic_set(&dev->watchdog_tick_cnt, 0);

	if (atomic_read(&dev->watchdog_tick_cnt) >= WATCHDOG_TICK_CNT_TO_START_WATCHDOG) {
		/* This means that hw is busy and no interrupts were
		 * generated by hw for the Nth time of running this
		 * watchdog timer. This usually means a serious hw
		 * error. Now it is time to kill all instances and
		 * reset the MFC. */
		mfc_err_dev("[%d] Time out during waiting for HW\n",
				atomic_read(&dev->watchdog_tick_cnt));
		queue_work(dev->watchdog_wq, &dev->watchdog_work);
	}

	dev->watchdog_timer.expires = jiffies +
					msecs_to_jiffies(WATCHDOG_TICK_INTERVAL);
	add_timer(&dev->watchdog_timer);
}

void s5p_mfc_watchdog_start_tick(struct s5p_mfc_dev *dev)
{
	if (atomic_read(&dev->watchdog_tick_running)) {
		mfc_debug(2, "watchdog timer was already started!\n");
	} else {
		mfc_debug(2, "watchdog timer is now started!\n");
		atomic_set(&dev->watchdog_tick_running, 1);
	}

	/* Reset the timeout watchdog */
	atomic_set(&dev->watchdog_tick_cnt, 0);
}

void s5p_mfc_watchdog_stop_tick(struct s5p_mfc_dev *dev)
{
	if (atomic_read(&dev->watchdog_tick_running)) {
		mfc_debug(2, "watchdog timer is now stopped!\n");
		atomic_set(&dev->watchdog_tick_running, 0);
	} else {
		mfc_debug(2, "watchdog timer was already stopped!\n");
	}

	/* Reset the timeout watchdog */
	atomic_set(&dev->watchdog_tick_cnt, 0);
}

void s5p_mfc_watchdog_reset_tick(struct s5p_mfc_dev *dev)
{
	mfc_debug(2, "watchdog timer reset!\n");

	/* Reset the timeout watchdog */
	atomic_set(&dev->watchdog_tick_cnt, 0);
}
