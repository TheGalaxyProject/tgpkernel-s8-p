/*
 * Copyright (C) 2010 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/if_arp.h>
#include <linux/platform_device.h>
#include <linux/kallsyms.h>
#include <linux/suspend.h>
#include <linux/notifier.h>
#include <linux/smc.h>
#include <linux/skbuff.h>
#include <linux/mcu_ipc.h>
#include <linux/shm_ipc.h>

#include "modem_prj.h"
#include "modem_utils.h"
#include "modem_link_device_shmem.h"
#include "pmu-cp.h"

static void trigger_forced_cp_crash(struct shmem_link_device *shmd);

#ifdef CONFIG_DEBUG_MODEM_IF
static void save_mem_dump(struct shmem_link_device *shmd)
{
	struct link_device *ld = &shmd->ld;
	char *path = shmd->dump_path;
	struct file *fp;
	struct utc_time t;

	get_utc_time(&t);
	snprintf(path, MIF_MAX_PATH_LEN, "%s/%s_%d%02d%02d_%02d%02d%02d.dump",
		MIF_LOG_DIR, ld->name, t.year, t.mon, t.day, t.hour, t.min,
		t.sec);

	fp = mif_open_file(path);
	if (!fp) {
		mif_err("%s: ERR! %s open fail\n", ld->name, path);
		return;
	}
	mif_err("%s: %s opened\n", ld->name, path);

	mif_save_file(fp, (char *)shmd->base, shmd->size);

	mif_close_file(fp);
}

/**
 * mem_dump_work
 * @ws: pointer to an instance of work_struct structure
 *
 * Performs actual file operation for saving a DPRAM dump.
 */
static void mem_dump_work(struct work_struct *ws)
{
	struct shmem_link_device *shmd;

	shmd = container_of(ws, struct shmem_link_device, dump_dwork.work);
	if (!shmd) {
		mif_err("ERR! no shmd\n");
		return;
	}

	save_mem_dump(shmd);
}
#endif

/**
 * recv_int2ap
 * @shmd: pointer to an instance of shmem_link_device structure
 *
 * Returns the value of the CP-to-AP interrupt register.
 */
static inline u16 recv_int2ap(struct shmem_link_device *shmd)
{
	if (shmd->mbx2ap)
		return *shmd->mbx2ap;
	else
		return (u16)mbox_get_value(shmd->mbx_cp2ap_msg);
}

/**
 * send_int2cp
 * @shmd: pointer to an instance of shmem_link_device structure
 * @mask: value to be written to the AP-to-CP interrupt register
 */
static inline void send_int2cp(struct shmem_link_device *shmd, u16 mask)
{
	struct link_device *ld = &shmd->ld;
	struct modem_ctl *mc = ld->mc;

	if ((ld->mode != LINK_MODE_IPC) &&
			(mc->phone_state != STATE_CRASH_EXIT)) {
		mif_info("%s: <by %pf> mask 0x%04X\n", ld->name, CALLER, mask);
		return;
	}

	if (shmd->mbx2cp) {
		*shmd->mbx2cp = mask;
	} else {
		mbox_set_value(shmd->mbx_ap2cp_msg, mask);
		mbox_set_interrupt(shmd->int_ap2cp_msg);
	}
}

/**
 * get_shmem_status
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dir: direction of communication (TX or RX)
 * @mst: pointer to an instance of mem_status structure
 *
 * Takes a snapshot of the current status of a SHMEM.
 */
static void get_shmem_status(struct shmem_link_device *shmd,
			enum direction dir, struct mem_status *mst)
{
#ifdef CONFIG_DEBUG_MODEM_IF
	getnstimeofday(&mst->ts);
#endif

	mst->dir = dir;
	mst->magic = get_magic(shmd);
	mst->access = get_access(shmd);
	mst->head[IPC_FMT][TX] = get_txq_head(shmd, IPC_FMT);
	mst->tail[IPC_FMT][TX] = get_txq_tail(shmd, IPC_FMT);
	mst->head[IPC_FMT][RX] = get_rxq_head(shmd, IPC_FMT);
	mst->tail[IPC_FMT][RX] = get_rxq_tail(shmd, IPC_FMT);
	mst->head[IPC_RAW][TX] = get_txq_head(shmd, IPC_RAW);
	mst->tail[IPC_RAW][TX] = get_txq_tail(shmd, IPC_RAW);
	mst->head[IPC_RAW][RX] = get_rxq_head(shmd, IPC_RAW);
	mst->tail[IPC_RAW][RX] = get_rxq_tail(shmd, IPC_RAW);
	mst->int2ap = recv_int2ap(shmd);
	mst->int2cp = read_int2cp(shmd);
}

static inline void update_rxq_tail_status(struct shmem_link_device *shmd,
					  int dev, struct mem_status *mst)
{
	mst->tail[dev][RX] = get_rxq_tail(shmd, dev);
}

/**
 * handle_cp_crash
 * @shmd: pointer to an instance of shmem_link_device structure
 *
 * Actual handler for the CRASH_EXIT command from a CP.
 */
static void handle_cp_crash(struct shmem_link_device *shmd)
{
	struct link_device *ld = &shmd->ld;
	struct io_device *iod;
	int i;

	if (shmd->forced_cp_crash)
		shmd->forced_cp_crash = false;

	ld->mode = LINK_MODE_ULOAD;

	/* Stop network interfaces */
	mif_netif_stop(ld);

	/* Purge the skb_txq in every IPC device (IPC_FMT, IPC_RAW, etc.) */
	for (i = 0; i < MAX_EXYNOS_DEVICES; i++)
		skb_queue_purge(ld->skb_txq[i]);

	/* Change the modem state to STATE_CRASH_EXIT for the FMT IO device */
	iod = link_get_iod_with_format(ld, IPC_FMT);
	if (iod)
		iod->modem_state_changed(iod, STATE_CRASH_EXIT);

	/* time margin for taking state changes by rild */
	mdelay(100);

	/* Change the modem state to STATE_CRASH_EXIT for the BOOT IO device */
	iod = link_get_iod_with_format(ld, IPC_BOOT);
	if (iod)
		iod->modem_state_changed(iod, STATE_CRASH_EXIT);
}

/**
 * handle_no_cp_crash_ack
 * @arg: pointer to an instance of shmem_link_device structure
 *
 * Invokes handle_cp_crash() to enter the CRASH_EXIT state if there was no
 * CRASH_ACK from a CP in FORCE_CRASH_ACK_TIMEOUT.
 */
static void handle_no_cp_crash_ack(unsigned long arg)
{
	struct shmem_link_device *shmd = (struct shmem_link_device *)arg;
	struct link_device *ld = &shmd->ld;

	mif_err("%s: ERR! No CRASH_EXIT ACK from CP\n", ld->name);

	handle_cp_crash(shmd);
}

/**
 * trigger_forced_cp_crash
 * @shmd: pointer to an instance of shmem_link_device structure
 *
 * Triggers an enforced CP crash.
 */
static void trigger_forced_cp_crash(struct shmem_link_device *shmd)
{
	struct link_device *ld = &shmd->ld;
	struct utc_time t;

	if (shmd->forced_cp_crash) {
		mif_err("%s: <by %pf> ALREADY in progress\n", ld->name, CALLER);
		return;
	}
	shmd->forced_cp_crash = true;

	get_utc_time(&t);
	mif_err("%s: [%02d:%02d:%02d.%03d] <by %pf>\n",
		ld->name, t.hour, t.min, t.sec, t.msec, CALLER);

	if (!wake_lock_active(&shmd->wlock))
		wake_lock(&shmd->wlock);

#ifdef CONFIG_DEBUG_MODEM_IF
	if (in_interrupt())
		queue_delayed_work(system_wq, &shmd->dump_dwork, 0);
	else
		save_mem_dump(shmd);
#endif

	/* Send CRASH_EXIT command to a CP */
	send_int2cp(shmd, INT_CMD(INT_CMD_CRASH_EXIT));
	get_shmem_status(shmd, TX, msq_get_free_slot(&shmd->tx_msq));

	/* If there is no CRASH_ACK from a CP in FORCE_CRASH_ACK_TIMEOUT,
	   handle_no_cp_crash_ack() will be executed. */
	mif_add_timer(&shmd->crash_ack_timer, FORCE_CRASH_ACK_TIMEOUT,
			handle_no_cp_crash_ack, (unsigned long)shmd);

	return;
}

/**
 * cmd_crash_reset_handler
 * @shmd: pointer to an instance of shmem_link_device structure
 *
 * Handles the CRASH_RESET command from a CP.
 */
static void cmd_crash_reset_handler(struct shmem_link_device *shmd)
{
	struct link_device *ld = &shmd->ld;
	struct io_device *iod = NULL;
	int i;
	struct utc_time t;

	ld->mode = LINK_MODE_ULOAD;

	if (!wake_lock_active(&shmd->wlock))
		wake_lock(&shmd->wlock);

	get_utc_time(&t);
	mif_err("%s: ERR! [%02d:%02d:%02d.%03d] Recv 0xC7 (CRASH_RESET)\n",
		ld->name, t.hour, t.min, t.sec, t.msec);
#ifdef CONFIG_DEBUG_MODEM_IF
	queue_delayed_work(system_wq, &shmd->dump_dwork, 0);
#endif

	/* Stop network interfaces */
	mif_netif_stop(ld);

	/* Purge the skb_txq in every IPC device (IPC_FMT, IPC_RAW, etc.) */
	for (i = 0; i < MAX_EXYNOS_DEVICES; i++)
		skb_queue_purge(ld->skb_txq[i]);

	mif_err("%s: Recv 0xC7 (CRASH_RESET)\n", ld->name);

	/* Change the modem state to STATE_CRASH_RESET for the FMT IO device */
	iod = link_get_iod_with_format(ld, IPC_FMT);
	if (iod)
		iod->modem_state_changed(iod, STATE_CRASH_RESET);

	/* time margin for taking state changes by rild */
	mdelay(100);

	/* Change the modem state to STATE_CRASH_RESET for the BOOT IO device */
	iod = link_get_iod_with_format(ld, IPC_BOOT);
	if (iod)
		iod->modem_state_changed(iod, STATE_CRASH_RESET);
}

/**
 * cmd_crash_exit_handler
 * @shmd: pointer to an instance of shmem_link_device structure
 *
 * Handles the CRASH_EXIT command from a CP.
 */
static void cmd_crash_exit_handler(struct shmem_link_device *shmd)
{
	struct link_device *ld = &shmd->ld;
	struct utc_time t;

	ld->mode = LINK_MODE_ULOAD;

	del_timer(&shmd->crash_ack_timer);

	if (!wake_lock_active(&shmd->wlock))
		wake_lock(&shmd->wlock);

	get_utc_time(&t);
	mif_err("%s: ERR! [%02d:%02d:%02d.%03d] Recv 0xC9 (CRASH_EXIT)\n",
		ld->name, t.hour, t.min, t.sec, t.msec);
#ifdef CONFIG_DEBUG_MODEM_IF
	queue_delayed_work(system_wq, &shmd->dump_dwork, 0);
#endif

	handle_cp_crash(shmd);
}

/**
 * cmd_phone_start_handler
 * @shmd: pointer to an instance of shmem_link_device structure
 *
 * Handles the PHONE_START command from a CP.
 */
static void cmd_phone_start_handler(struct shmem_link_device *shmd)
{
	int err;
	struct link_device *ld = &shmd->ld;
	struct io_device *iod;

	mif_err("%s: Recv 0xC8 (CP_START)\n", ld->name);

	iod = link_get_iod_with_format(ld, IPC_FMT);
	if (!iod) {
		mif_err("%s: ERR! no FMT iod\n", ld->name);
		return;
	}

	msq_reset(&shmd->rx_msq);

	err = init_shmem_ipc(shmd);
	if (err)
		return;

	if (wake_lock_active(&shmd->wlock))
		wake_unlock(&shmd->wlock);

	mif_err("%s: Send 0xC2 (INIT_END)\n", ld->name);
	send_int2cp(shmd, INT_CMD(INT_CMD_INIT_END));

	iod->modem_state_changed(iod, STATE_ONLINE);
}

/**
 * cmd_handler: processes a SHMEM command from a CP
 * @shmd: pointer to an instance of shmem_link_device structure
 * @cmd: SHMEM command from a CP
 */
static void cmd_handler(struct shmem_link_device *shmd, u16 cmd)
{
	struct link_device *ld = &shmd->ld;

	switch (INT_CMD_MASK(cmd)) {
	case INT_CMD_CRASH_RESET:
		cmd_crash_reset_handler(shmd);
		break;

	case INT_CMD_CRASH_EXIT:
		cmd_crash_exit_handler(shmd);
		break;

	case INT_CMD_PHONE_START:
		cmd_phone_start_handler(shmd);
		complete_all(&ld->init_cmpl);
		break;

	default:
		mif_err("%s: unknown command 0x%04X\n", ld->name, cmd);
	}
}

/**
 * ipc_rx_work
 * @ws: pointer to an instance of work_struct structure
 *
 * Invokes the recv method in the io_device instance to perform receiving IPC
 * messages from each skb.
 */
static void msg_rx_work(struct work_struct *ws)
{
	struct shmem_link_device *shmd;
	struct link_device *ld;
	struct io_device *iod;
	struct sk_buff *skb;
	int i;
	int queue_len;

	shmd = container_of(ws, struct shmem_link_device, msg_rx_dwork.work);
	ld = &shmd->ld;

	for (i = 0; i < MAX_EXYNOS_DEVICES; i++) {
		iod = shmd->iod[i];
		queue_len = skb_queue_len(ld->skb_rxq[i]);

		while (queue_len-- > 0) {
			skb = skb_dequeue(ld->skb_rxq[i]);
			if (!skb)
				break;
			iod->recv_skb(iod, ld, skb);
		}
	}
}

/**
 * rx_ipc_frames
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 * @mst: pointer to an instance of mem_status structure
 *
 * Returns
 *   ret < 0  : error
 *   ret == 0 : ILLEGAL status
 *   ret > 0  : valid data
 *
 * Must be invoked only when there is data in the corresponding RXQ.
 *
 * Requires a recv_skb method in the io_device instance, so this function must
 * be used for only EXYNOS.
 */
static int rx_ipc_frames(struct shmem_link_device *shmd, int dev,
			struct circ_status *circ)
{
	struct link_device *ld = &shmd->ld;
	struct io_device *iod;
	struct sk_buff_head *rxq = ld->skb_rxq[dev];
	struct sk_buff *skb;
	/**
	 * variables for the status of the circular queue
	 */
	u8 *src;
	u8 hdr[EXYNOS_HEADER_SIZE];
	/**
	 * variables for RX processing
	 */
	int qsize;	/* size of the queue			*/
	int rcvd;	/* size of data in the RXQ or error	*/
	int rest;	/* size of the rest data		*/
	int out;	/* index to the start of current frame	*/
	int tot;	/* total length including padding data	*/

	src = circ->buff;
	qsize = circ->qsize;
	out = circ->out;
	rcvd = circ->size;

	rest = circ->size;
	tot = 0;

	while (rest > 0) {
		u8 ch;

		/* Copy the header in the frame to the header buffer */
		circ_read(hdr, src, qsize, out, EXYNOS_HEADER_SIZE);

		/* Check the config field in the header */
		if (unlikely(!exynos_start_valid(hdr))) {
			mif_err("%s: ERR! %s INVALID config 0x%02X (rcvd %d, rest %d)\n",
				ld->name, get_dev_name(dev), hdr[0],
				rcvd, rest);
			goto bad_msg;
		}

		/* Verify the total length of the frame (data + padding) */
		tot = exynos_get_total_len(hdr);
		if (unlikely(tot > rest)) {
			mif_err("%s: ERR! %s tot %d > rest %d (rcvd %d)\n",
				ld->name, get_dev_name(dev), tot, rest, rcvd);
			goto bad_msg;
		}

		/* Allocate an skb */
		skb = dev_alloc_skb(tot);
		if (!skb) {
			mif_err("%s: ERR! %s dev_alloc_skb(%d) fail\n",
				ld->name, get_dev_name(dev), tot);
			goto no_mem;
		}

		/* Set the attribute of the skb as "single frame" */
		skbpriv(skb)->single_frame = true;

		/* Read the frame from the RXQ */
		circ_read(skb_put(skb, tot), src, qsize, out, tot);

		/* Store the skb to the corresponding skb_rxq */
		skb_queue_tail(rxq, skb);

		ch = exynos_get_ch(skb->data);
		iod = link_get_iod_with_channel(ld, ch);
		if (!iod) {
			mif_err("%s: ERR! no IPC_BOOT iod\n", ld->name);
			break;
		}

		skbpriv(skb)->lnk_hdr = iod->link_header;
		skbpriv(skb)->exynos_ch = ch;

		/* Calculate new out value */
		rest -= tot;
		out += tot;
		if (unlikely(out >= qsize))
			out -= qsize;
	}

	/* Update tail (out) pointer to empty out the RXQ */
	set_rxq_tail(shmd, dev, circ->in);
	return rcvd;

no_mem:
	/* Update tail (out) pointer to the frame to be read in the future */
	set_rxq_tail(shmd, dev, out);
	rcvd -= rest;
	return rcvd;

bad_msg:
#ifdef CONFIG_DEBUG_MODEM_IF
	mif_err("%s: ERR! rcvd:%d tot:%d rest:%d\n", ld->name, rcvd, tot, rest);
	pr_ipc(1, "shmem: ERR! CP2MIF", (src + out), (rest > 20) ? 20 : rest);
#endif
	return -EBADMSG;
}

static inline void done_req_ack(struct shmem_link_device *shmd, int dev)
{
	u16 mask;
#ifdef CONFIG_DEBUG_MODEM_IF
	struct mem_status mst;
#endif

	if (unlikely(shmd->dev[dev]->req_ack_rcvd < 0))
		shmd->dev[dev]->req_ack_rcvd = 0;

	if (likely(shmd->dev[dev]->req_ack_rcvd == 0))
		return;

	mask = get_mask_res_ack(shmd, dev);
	send_int2cp(shmd, INT_NON_CMD(mask));
	shmd->dev[dev]->req_ack_rcvd -= 1;

#ifdef CONFIG_DEBUG_MODEM_IF
	get_shmem_status(shmd, TX, &mst);
	print_res_ack(shmd, dev, &mst);
#endif
}

static inline void recv_res_ack(struct shmem_link_device *shmd,
				struct mem_status *mst)
{
	u16 intr = mst->int2ap;

	if (intr & get_mask_res_ack(shmd, IPC_FMT)) {
#ifdef CONFIG_DEBUG_MODEM_IF
		mif_info("%s: recv FMT RES_ACK\n", shmd->ld.name);
#endif
		complete(&shmd->req_ack_cmpl[IPC_FMT]);
	}

	if (intr & get_mask_res_ack(shmd, IPC_RAW)) {
#ifdef CONFIG_DEBUG_MODEM_IF
		mif_info("%s: recv RAW RES_ACK\n", shmd->ld.name);
#endif
		complete(&shmd->req_ack_cmpl[IPC_RAW]);
	}
}

static inline void recv_req_ack(struct shmem_link_device *shmd,
				struct mem_status *mst)
{
	u16 intr = mst->int2ap;

	if (intr & get_mask_req_ack(shmd, IPC_FMT)) {
		shmd->dev[IPC_FMT]->req_ack_rcvd += 1;
#ifdef CONFIG_DEBUG_MODEM_IF
		print_req_ack(shmd, IPC_FMT, mst);
#endif
	}

	if (intr & get_mask_req_ack(shmd, IPC_RAW)) {
		shmd->dev[IPC_RAW]->req_ack_rcvd += 1;
#ifdef CONFIG_DEBUG_MODEM_IF
		print_req_ack(shmd, IPC_RAW, mst);
#endif
	}
}

/**
 * msg_handler: receives IPC messages from every RXQ
 * @shmd: pointer to an instance of shmem_link_device structure
 * @mst: pointer to an instance of mem_status structure
 *
 * 1) Receives all IPC message frames currently in every IPC RXQ.
 * 2) Sends RES_ACK responses if there are REQ_ACK requests from a CP.
 * 3) Completes all threads waiting for the corresponding RES_ACK from a CP if
 *    there is any RES_ACK response.
 */
static void msg_handler(struct shmem_link_device *shmd, struct mem_status *mst)
{
	struct link_device *ld = &shmd->ld;
	struct circ_status circ;
	int i = 0;
	int ret = 0;

	if (!ipc_active(shmd)) {
		mif_err("%s: ERR! IPC is NOT ACTIVE!!!\n", ld->name);
		trigger_forced_cp_crash(shmd);
		return;
	}

	for (i = 0; i < MAX_EXYNOS_DEVICES; i++) {
		/* Skip RX processing if there is no data in the RXQ */
		if (mst->head[i][RX] == mst->tail[i][RX]) {
			done_req_ack(shmd, i);
			continue;
		}

		/* Get the size of data in the RXQ */
		ret = get_rxq_rcvd(shmd, i, mst, &circ);
		if (unlikely(ret < 0)) {
			mif_err("%s: ERR! get_rxq_rcvd fail (err %d)\n",
				ld->name, ret);
			trigger_forced_cp_crash(shmd);
			return;
		}

		/* Read data in the RXQ */
		ret = rx_ipc_frames(shmd, i, &circ);
		if (unlikely(ret < 0)) {
			trigger_forced_cp_crash(shmd);
			return;
		}

		if (ret < circ.size)
			break;

		/* Process REQ_ACK (At this point, the RXQ may be empty.) */
		done_req_ack(shmd, i);
	}
}

/**
 * ipc_rx_task: processes a SHMEM command or receives IPC messages
 * @shmd: pointer to an instance of shmem_link_device structure
 * @mst: pointer to an instance of mem_status structure
 *
 * Invokes cmd_handler for commands or msg_handler for IPC messages.
 */
static void ipc_rx_task(unsigned long data)
{
	struct shmem_link_device *shmd = (struct shmem_link_device *)data;
	int qlen = msq_get_size(&shmd->rx_msq);

	while (qlen-- > 0) {
		struct mem_status *mst;
		int i;
		u16 intr;

		mst = msq_get_data_slot(&shmd->rx_msq);
		if (!mst)
			break;

		intr = mst->int2ap;

		/* Process a SHMEM command */
		if (unlikely(INT_CMD_VALID(intr))) {
			cmd_handler(shmd, intr);
			continue;
		}

		/* Update tail variables with the current tail pointers */
		for (i = 0; i < MAX_EXYNOS_DEVICES; i++)
			update_rxq_tail_status(shmd, i, mst);

		/* Check and receive RES_ACK from CP */
		if (unlikely(intr & INT_MASK_RES_ACK_SET))
			recv_res_ack(shmd, mst);

		/* Check and receive REQ_ACK from CP */
		if (unlikely(intr & INT_MASK_REQ_ACK_SET))
			recv_req_ack(shmd, mst);

		msg_handler(shmd, mst);

		queue_delayed_work(system_long_wq, &shmd->msg_rx_dwork, 0);
	}
}

/**
 * rx_udl_frames
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 * @mst: pointer to an instance of mem_status structure
 *
 * Returns
 *   ret < 0  : error
 *   ret == 0 : ILLEGAL status
 *   ret > 0  : valid data
 *
 * Must be invoked only when there is data in the corresponding RXQ.
 *
 * Requires a recv_skb method in the io_device instance, so this function must
 * be used for only EXYNOS.
 */
static int rx_udl_frames(struct shmem_link_device *shmd, int dev,
			struct circ_status *circ)
{
	struct link_device *ld = &shmd->ld;
	struct io_device *iod;
	struct sk_buff *skb;
	int ret;
	/**
	 * variables for the status of the circular queue
	 */
	u8 *src;
	u8 hdr[EXYNOS_HEADER_SIZE];
	/**
	 * variables for RX processing
	 */
	u32 qsize;	/* size of the queue			*/
	u32 rcvd;	/* size of data in the RXQ or error	*/
	u32 rest;	/* size of the rest data		*/
	u32 out;	/* index to the start of current frame	*/
	unsigned int tot;	/* total length including padding data	*/

	src = circ->buff;
	qsize = circ->qsize;
	out = circ->out;
	rcvd = circ->size;
	rest = circ->size;
	tot = 0;
	while (rest > 0) {
		u8 ch;

		/* Copy the header in the frame to the header buffer */
		circ_read(hdr, src, qsize, out, EXYNOS_HEADER_SIZE);

		/* Check the config field in the header */
		if (unlikely(!exynos_start_valid(hdr))) {
			mif_err("%s: ERR! %s INVALID config 0x%02X (rest %d, rcvd %d)\n",
				ld->name, get_dev_name(dev),
				hdr[0], rest, rcvd);
			pr_ipc(1, "UDL", (src + out), (rest > 20) ? 20 : rest);
			ret = -EBADMSG;
			goto exit;
		}

		/* Verify the total length of the frame (data + padding) */
		tot = exynos_get_total_len(hdr);
		if (unlikely(tot > rest)) {
			mif_err("%s: ERR! %s tot %d > rest %d (rcvd %d)\n",
				ld->name, get_dev_name(dev), tot, rest, rcvd);
			ret = -ENODATA;
			goto exit;
		}

		/* Allocate an skb */
		skb = alloc_skb(tot + NET_SKB_PAD, GFP_KERNEL);
		if (!skb) {
			mif_err("%s: ERR! %s alloc_skb fail\n",
				ld->name, get_dev_name(dev));
			ret = -ENOMEM;
			goto free_skb;
		}
		skb_reserve(skb, NET_SKB_PAD);

		/* Set the attribute of the skb as "single frame" */
		skbpriv(skb)->single_frame = true;

		/* Read the frame from the RXQ */
		circ_read(skb_put(skb, tot), src, qsize, out, tot);

		/* Pass the skb to an iod */
		ch = exynos_get_ch(skb->data);
		iod = link_get_iod_with_channel(ld, ch);
		if (!iod) {
			mif_err("%s: ERR! no IPC_BOOT iod\n", ld->name);
			break;
		}

		skbpriv(skb)->lnk_hdr = iod->link_header;
		skbpriv(skb)->exynos_ch = ch;

#ifdef CONFIG_DEBUG_MODEM_IF
		if (!std_udl_with_payload(std_udl_get_cmd(skb->data))) {
			if (ld->mode == LINK_MODE_DLOAD) {
				pr_ipc(0, "[CP->AP] DL CMD", skb->data,
					(skb->len > 20 ? 20 : skb->len));
			} else {
				pr_ipc(0, "[CP->AP] UL CMD", skb->data,
					(skb->len > 20 ? 20 : skb->len));
			}
		}
#endif

		iod->recv_skb(iod, ld, skb);

		/* Calculate new out value */
		rest -= tot;
		out += tot;
		if (unlikely(out >= qsize))
			out -= qsize;
	}

	/* Update tail (out) pointer to empty out the RXQ */
	set_rxq_tail(shmd, dev, circ->in);

	return rcvd;

free_skb:
	kfree_skb(skb);
exit:
	return ret;
}

/**
 * udl_rx_work
 * @ws: pointer to an instance of the work_struct structure
 *
 * Invokes the recv method in the io_device instance to perform receiving IPC
 * messages from each skb.
 */
static void udl_rx_work(struct work_struct *ws)
{
	struct shmem_link_device *shmd;
	struct link_device *ld;
	struct sk_buff_head *rxq;

	shmd = container_of(ws, struct shmem_link_device, udl_rx_dwork.work);
	ld = &shmd->ld;
	rxq = ld->skb_rxq[IPC_RAW];

	while (1) {
		struct mem_status *mst;
		struct circ_status circ;

		mst = msq_get_data_slot(&shmd->rx_msq);
		if (!mst)
			break;
		update_rxq_tail_status(shmd, IPC_RAW, mst);

		/* Exit the loop if there is no more data in the RXQ */
		if (mst->tail[IPC_RAW][RX] == mst->head[IPC_RAW][RX])
			break;

		/* Invoke an RX function only when there is data in the RXQ */
		if (get_rxq_rcvd(shmd, IPC_RAW, mst, &circ) < 0) {
			mif_err("%s: ERR! get_rxq_rcvd fail\n", ld->name);
#ifdef CONFIG_DEBUG_MODEM_IF
			trigger_forced_cp_crash(shmd);
#endif
			break;
		}

		if (rx_udl_frames(shmd, IPC_RAW, &circ) < 0) {
			skb_queue_purge(rxq);
			break;
		}
	}
}

/**
 * udl_handler: receives BOOT/DUMP IPC messages from every RXQ
 * @shmd: pointer to an instance of shmem_link_device structure
 * @mst: pointer to an instance of mem_status structure
 *
 * 1) Receives all IPC message frames currently in every IPC RXQ.
 * 2) Sends RES_ACK responses if there are REQ_ACK requests from a CP.
 * 3) Completes all threads waiting for the corresponding RES_ACK from a CP if
 *    there is any RES_ACK response.
 */
static void udl_handler(struct shmem_link_device *shmd, struct mem_status *mst)
{
	u16 intr = mst->int2ap;

	/* Process a SHMEM command */
	if (unlikely(INT_CMD_VALID(intr))) {
		cmd_handler(shmd, intr);
		return;
	}

	/* Schedule soft IRQ for RX */
	queue_delayed_work(system_wq, &shmd->udl_rx_dwork, 0);

	/* Check and process RES_ACK */
	if (intr & INT_MASK_RES_ACK_SET) {
		if (intr & get_mask_res_ack(shmd, IPC_RAW)) {
#ifdef CONFIG_DEBUG_MODEM_IF
			struct link_device *ld = &shmd->ld;
			mif_info("%s: recv RAW RES_ACK\n", ld->name);
			print_circ_status(ld, IPC_RAW, mst);
#endif
			complete(&shmd->req_ack_cmpl[IPC_RAW]);
		}
	}
}

/**
 * shmem_irq_handler: interrupt handler for a MCU_IPC interrupt
 * @data: pointer to a data
 *
 * 1) Reads the interrupt value
 * 2) Performs interrupt handling
 *
 * Flow for normal interrupt handling:
 *   shmem_irq_handler -> udl_handler
 *   shmem_irq_handler -> ipc_rx_task -> cmd_handler -> cmd_xxx_handler
 *   shmem_irq_handler -> ipc_rx_task -> msg_handler -> rx_ipc_frames ->  ...
 */
static void shmem_irq_handler(void *data)
{
	struct shmem_link_device *shmd = (struct shmem_link_device *)data;
	struct link_device *ld = (struct link_device *)&shmd->ld;
	struct mem_status *mst = msq_get_free_slot(&shmd->rx_msq);
	u16 intr;

	get_shmem_status(shmd, RX, mst);
	intr = mst->int2ap;

	if (unlikely(ld->mode == LINK_MODE_OFFLINE)) {
		mif_info("%s: ld->mode == LINK_MODE_OFFLINE\n", ld->name);
		return;
	}

	if (unlikely(!INT_VALID(intr))) {
		mif_info("%s: ERR! invalid intr 0x%X\n", ld->name, intr);
		return;
	}

	if (ld->mode == LINK_MODE_DLOAD || ld->mode == LINK_MODE_ULOAD)
		udl_handler(shmd, mst);
	else
		tasklet_schedule(&shmd->rx_tsk);
}

/**
 * write_ipc_to_txq
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 * @circ: pointer to an instance of circ_status structure
 * @skb: pointer to an instance of sk_buff structure
 *
 * Must be invoked only when there is enough space in the TXQ.
 */
static void write_ipc_to_txq(struct shmem_link_device *shmd, int dev,
			struct circ_status *circ, struct sk_buff *skb)
{
	u32 qsize = circ->qsize;
	u32 in = circ->in;
	u8 *buff = circ->buff;
	u8 *src = skb->data;
	u32 len = skb->len;

	/* Print send data to CP */
	log_ipc_pkt(skb, LINK, TX);

	/* Write data to the TXQ */
	circ_write(buff, src, qsize, in, len);

	/* Update new head (in) pointer */
	set_txq_head(shmd, dev, circ_new_pointer(qsize, in, len));
}

/**
 * xmit_ipc_msg
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 *
 * Tries to transmit IPC messages in the skb_txq of @dev as many as possible.
 *
 * Returns total length of IPC messages transmit or an error code.
 */
static int xmit_ipc_msg(struct shmem_link_device *shmd, int dev)
{
	struct link_device *ld = &shmd->ld;
	struct sk_buff_head *txq = ld->skb_txq[dev];
	struct sk_buff *skb;
	unsigned long flags;
	struct circ_status circ;
	int space;
	int copied = 0;
	bool chk_nospc = false;

	/* Acquire the spin lock for a TXQ */
	spin_lock_irqsave(&shmd->tx_lock[dev], flags);

	while (1) {
		/* Get the size of free space in the TXQ */
		space = get_txq_space(shmd, dev, &circ);
		if (unlikely(space < 0)) {
#ifdef CONFIG_DEBUG_MODEM_IF
			/* Trigger a enforced CP crash */
			trigger_forced_cp_crash(shmd);
#endif
			/* Empty out the TXQ */
			reset_txq_circ(shmd, dev);
			copied = -EIO;
			break;
		}

		skb = skb_dequeue(txq);
		if (unlikely(!skb))
			break;

		/* CAUTION : Uplink size is limited to 16KB and
			     this limitation is used ONLY in North America Prj.
		   Check the free space size,
		  - FMT : comparing with skb->len
		  - RAW : check used buffer size  */
#ifdef CONFIG_MACH_GARDA
		if (dev == IPC_FMT) {
			if (unlikely(space < skb->len))
				chk_nospc = true;
		} else { /* dev == IPC_RAW */
			if (unlikely((SHM_4M_RAW_TX_BUFF_SZ - space)
						>= SHM_4M_MAX_UPLINK_SIZE))
				chk_nospc = true;
		}
#else
		chk_nospc = (space < skb->len) ? true : false;
#endif
		if (unlikely(chk_nospc)) {
#ifdef CONFIG_DEBUG_MODEM_IF
			struct mem_status mst;
#endif
			/* Set res_required flag for the "dev" */
			atomic_set(&shmd->res_required[dev], 1);

			/* Take the skb back to the skb_txq */
			skb_queue_head(txq, skb);

			mif_err("%s: <by %pf> NOSPC in %s_TXQ {qsize:%u in:%u out:%u} free:%u < len:%u\n",
				ld->name, CALLER, get_dev_name(dev),
				circ.qsize, circ.in, circ.out, space, skb->len);
#ifdef CONFIG_DEBUG_MODEM_IF
			get_shmem_status(shmd, TX, &mst);
			print_circ_status(ld, dev, &mst);
#endif
			copied = -ENOSPC;
			break;
		}

		/* TX only when there is enough space in the TXQ */
		write_ipc_to_txq(shmd, dev, &circ, skb);
		copied += skb->len;
		dev_kfree_skb_any(skb);
	}

	/* Release the spin lock */
	spin_unlock_irqrestore(&shmd->tx_lock[dev], flags);

	return copied;
}

/**
 * wait_for_res_ack
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 *
 * 1) Sends an REQ_ACK interrupt for @dev to CP.
 * 2) Waits for the corresponding RES_ACK for @dev from CP.
 *
 * Returns the return value from wait_for_completion_interruptible_timeout().
 */
static int wait_for_res_ack(struct shmem_link_device *shmd, int dev)
{
	struct link_device *ld = &shmd->ld;
	struct completion *cmpl = &shmd->req_ack_cmpl[dev];
	unsigned long timeout = msecs_to_jiffies(RES_ACK_WAIT_TIMEOUT);
	int ret;
	u16 mask;

#ifdef CONFIG_DEBUG_MODEM_IF
	mif_info("%s: send %s REQ_ACK\n", ld->name, get_dev_name(dev));
#endif

	mask = get_mask_req_ack(shmd, dev);
	send_int2cp(shmd, INT_NON_CMD(mask));

	/* ret < 0 if interrupted, ret == 0 on timeout */
	ret = wait_for_completion_interruptible_timeout(cmpl, timeout);
	if (ret < 0) {
		mif_err("%s: %s: wait_for_completion interrupted! (ret %d)\n",
			ld->name, get_dev_name(dev), ret);
		goto exit;
	}

	if (ret == 0) {
		struct mem_status mst;

		memset(&mst, 0, sizeof(struct mem_status));
		get_shmem_status(shmd, TX, &mst);

		mif_err("%s: wait_for_completion TIMEOUT! (no %s_RES_ACK)\n",
			ld->name, get_dev_name(dev));

		/*
		** The TXQ must be checked whether or not it is empty, because
		** an interrupt mask can be overwritten by the next interrupt.
		*/
		if (mst.head[dev][TX] == mst.tail[dev][TX]) {
			ret = get_txq_buff_size(shmd, dev);
#ifdef CONFIG_DEBUG_MODEM_IF
			mif_err("%s: %s_TXQ has been emptied\n",
				ld->name, get_dev_name(dev));
			print_circ_status(ld, dev, &mst);
#endif
		}

		goto exit;
	}

#ifdef CONFIG_DEBUG_MODEM_IF
	mif_info("%s: recv %s RES_ACK\n", ld->name, get_dev_name(dev));
#endif

exit:
	return ret;
}

/**
 * process_res_ack
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 *
 * 1) Tries to transmit IPC messages in the skb_txq with xmit_ipc_msg().
 * 2) Sends an interrupt to CP if there is no error from xmit_ipc_msg().
 * 3) Restarts SHMEM flow control if xmit_ipc_msg() returns -ENOSPC.
 *
 * Returns the return value from xmit_ipc_msg().
 */
static int process_res_ack(struct shmem_link_device *shmd, int dev)
{
	int ret;
	u16 mask;

	ret = xmit_ipc_msg(shmd, dev);
	if (ret > 0) {
		mask = get_mask_send(shmd, dev);
		send_int2cp(shmd, INT_NON_CMD(mask));
		get_shmem_status(shmd, TX, msq_get_free_slot(&shmd->tx_msq));
	}

	if (ret >= 0)
		atomic_set(&shmd->res_required[dev], 0);

	return ret;
}

/**
 * fmt_tx_work: performs TX for FMT IPC device under SHMEM flow control
 * @ws: pointer to an instance of the work_struct structure
 *
 * 1) Starts waiting for RES_ACK of FMT IPC device.
 * 2) Returns immediately if the wait is interrupted.
 * 3) Restarts SHMEM flow control if there is a timeout from the wait.
 * 4) Otherwise, it performs processing RES_ACK for FMT IPC device.
 */
static void fmt_tx_work(struct work_struct *ws)
{
	struct link_device *ld;
	struct shmem_link_device *shmd;
	int ret;

	ld = container_of(ws, struct link_device, fmt_tx_dwork.work);
	shmd = to_shmem_link_device(ld);

	ret = wait_for_res_ack(shmd, IPC_FMT);
	/* ret < 0 if interrupted */
	if (ret < 0)
		return;

	/* ret == 0 on timeout */
	if (ret == 0) {
		queue_delayed_work(ld->tx_wq, ld->tx_dwork[IPC_FMT], 0);
		return;
	}

	ret = process_res_ack(shmd, IPC_FMT);
	if (ret >= 0)
		return;

	/* At this point, ret < 0 */
	if (ret == -ENOSPC || ret == -EBUSY) {
		queue_delayed_work(ld->tx_wq, ld->tx_dwork[IPC_FMT],
				   msecs_to_jiffies(1));
	}
}

/**
 * raw_tx_work: performs TX for RAW IPC device under SHMEM flow control.
 * @ws: pointer to an instance of the work_struct structure
 *
 * 1) Starts waiting for RES_ACK of RAW IPC device.
 * 2) Returns immediately if the wait is interrupted.
 * 3) Restarts SHMEM flow control if there is a timeout from the wait.
 * 4) Otherwise, it performs processing RES_ACK for RAW IPC device.
 */
static void raw_tx_work(struct work_struct *ws)
{
	struct link_device *ld;
	struct shmem_link_device *shmd;
	unsigned long delay = usecs_to_jiffies(1000);
	int ret;

	ld = container_of(ws, struct link_device, raw_tx_dwork.work);
	shmd = to_shmem_link_device(ld);

	ret = wait_for_res_ack(shmd, IPC_RAW);
	/* ret < 0 if interrupted */
	if (ret < 0)
		return;

	/* ret == 0 on timeout */
	if (ret == 0) {
		queue_delayed_work(ld->tx_wq, ld->tx_dwork[IPC_RAW], delay);
		return;
	}

	ret = process_res_ack(shmd, IPC_RAW);
	if (ret >= 0) {
		mif_netif_wake(ld);
		return;
	}

	/* At this point, ret < 0 */
	if (ret == -ENOSPC || ret == -EBUSY) {
		queue_delayed_work(ld->tx_wq, ld->tx_dwork[IPC_RAW],
				   msecs_to_jiffies(1));
	}
}

/**
 * shmem_send_ipc
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 * @skb: pointer to an skb that will be transmitted
 *
 * 1) Tries to transmit IPC messages in the skb_txq with xmit_ipc_msg().
 * 2) Sends an interrupt to CP if there is no error from xmit_ipc_msg().
 * 3) Starts SHMEM flow control if xmit_ipc_msg() returns -ENOSPC.
 */
static int shmem_send_ipc(struct shmem_link_device *shmd, int dev)
{
	struct link_device *ld = &shmd->ld;
	int ret;
	u16 mask;

	if (atomic_read(&shmd->res_required[dev]) > 0) {
		mif_err("%s: %s_TXQ is full\n", ld->name, get_dev_name(dev));
		return 0;
	}

	ret = xmit_ipc_msg(shmd, dev);
	if (likely(ret > 0)) {
		mask = get_mask_send(shmd, dev);
		send_int2cp(shmd, INT_NON_CMD(mask));
		get_shmem_status(shmd, TX, msq_get_free_slot(&shmd->tx_msq));
		goto exit;
	}

	/* If there was no TX, just exit */
	if (ret == 0)
		goto exit;

	/* At this point, ret < 0 */
	if (ret == -ENOSPC || ret == -EBUSY) {
		/*----------------------------------------------------*/
		/* shmd->res_required[dev] was set in xmit_ipc_msg(). */
		/*----------------------------------------------------*/

		if (dev == IPC_RAW)
			mif_netif_stop(ld);

		queue_delayed_work(ld->tx_wq, ld->tx_dwork[dev],
				   msecs_to_jiffies(1));
	}

exit:
	return ret;
}

/**
 * shmem_try_send_ipc
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 * @iod: pointer to an instance of the io_device structure
 * @skb: pointer to an skb that will be transmitted
 *
 * 1) Enqueues an skb to the skb_txq for @dev in the link device instance.
 * 2) Tries to transmit IPC messages with shmem_send_ipc().
 */
static void shmem_try_send_ipc(struct shmem_link_device *shmd, int dev,
			struct io_device *iod, struct sk_buff *skb)
{
	struct link_device *ld = &shmd->ld;
	struct sk_buff_head *txq = ld->skb_txq[dev];
	int ret;

	if (unlikely(txq->qlen >= MAX_SKB_TXQ_DEPTH)) {
		mif_err("%s: %s txq->qlen %d >= %d\n", ld->name,
			get_dev_name(dev), txq->qlen, MAX_SKB_TXQ_DEPTH);
		dev_kfree_skb_any(skb);
		return;
	}

	skb_queue_tail(txq, skb);

	ret = shmem_send_ipc(shmd, dev);
	if (ret < 0) {
		mif_err("%s->%s: ERR! shmem_send_ipc fail (err %d)\n",
			iod->name, ld->name, ret);
	}
}

static int shmem_send_udl_cmd(struct shmem_link_device *shmd, int dev,
			struct io_device *iod, struct sk_buff *skb)
{
	struct link_device *ld = &shmd->ld;
	u8 *buff;
	u8 *src;
	u32 qsize;
	u32 in;
	int space;
	int tx_bytes;
	struct circ_status circ;

	if (iod->format == IPC_BOOT) {
		pr_ipc(0, "[AP->CP] DL CMD", skb->data,
			(skb->len > 20 ? 20 : skb->len));
	} else {
		pr_ipc(0, "[AP->CP] UL CMD", skb->data,
			(skb->len > 20 ? 20 : skb->len));
	}

	/* Get the size of free space in the TXQ */
	space = get_txq_space(shmd, dev, &circ);
	if (space < 0) {
		reset_txq_circ(shmd, dev);
		tx_bytes = -EIO;
		goto exit;
	}

	/* Get the size of data to be sent */
	tx_bytes = skb->len;

	/* Check the size of free space */
	if (space < tx_bytes) {
		mif_err("%s: NOSPC in %s_TXQ {qsize:%u in:%u out:%u} free:%u < tx_bytes:%u\n",
			ld->name, get_dev_name(dev), circ.qsize,
			circ.in, circ.out, space, tx_bytes);
		tx_bytes = -ENOSPC;
		goto exit;
	}

	/* Write data to the TXQ */
	buff = circ.buff;
	src = skb->data;
	qsize = circ.qsize;
	in = circ.in;
	circ_write(buff, src, qsize, in, tx_bytes);

	/* Update new head (in) pointer */
	set_txq_head(shmd, dev, circ_new_pointer(qsize, circ.in, tx_bytes));

exit:
	dev_kfree_skb_any(skb);
	return tx_bytes;
}

static int shmem_send_udl_data(struct shmem_link_device *shmd, int dev)
{
	struct link_device *ld = &shmd->ld;
	struct sk_buff_head *txq = ld->skb_txq[dev];
	struct sk_buff *skb;
	u8 *src;
	int tx_bytes;
	int copied;
	u8 *buff;
	u32 qsize;
	u32 in;
	u32 out;
	int space;
	struct circ_status circ;

	/* Get the size of free space in the TXQ */
	space = get_txq_space(shmd, dev, &circ);
	if (space < 0) {
#ifdef CONFIG_DEBUG_MODEM_IF
		/* Trigger a enforced CP crash */
		trigger_forced_cp_crash(shmd);
#endif
		/* Empty out the TXQ */
		reset_txq_circ(shmd, dev);
		return -EFAULT;
	}

	buff = circ.buff;
	qsize = circ.qsize;
	in = circ.in;
	out = circ.out;
	space = circ.size;

	copied = 0;
	while (1) {
		skb = skb_dequeue(txq);
		if (!skb)
			break;

		/* Get the size of data to be sent */
		src = skb->data;
		tx_bytes = skb->len;

		/* Check the free space size comparing with skb->len */
		if (space < tx_bytes) {
			/* Set res_required flag for the "dev" */
			atomic_set(&shmd->res_required[dev], 1);

			/* Take the skb back to the skb_txq */
			skb_queue_head(txq, skb);

			mif_info("NOSPC in RAW_TXQ {qsize:%u in:%u out:%u} space:%u < tx_bytes:%u\n",
				qsize, in, out, space, tx_bytes);
			break;
		}

		/*
		** TX only when there is enough space in the TXQ
		*/
		circ_write(buff, src, qsize, in, tx_bytes);

		copied += tx_bytes;
		in = circ_new_pointer(qsize, in, tx_bytes);
		space -= tx_bytes;

		dev_kfree_skb_any(skb);
	}

	/* Update new head (in) pointer */
	if (copied > 0) {
		in = circ_new_pointer(qsize, circ.in, copied);
		set_txq_head(shmd, dev, in);
	}

	return copied;
}

/**
 * shmem_send_udl
 * @shmd: pointer to an instance of shmem_link_device structure
 * @dev: IPC device (IPC_FMT, IPC_RAW, etc.)
 * @iod: pointer to an instance of the io_device structure
 * @skb: pointer to an skb that will be transmitted
 *
 * 1) Enqueues an skb to the skb_txq for @dev in the link device instance.
 * 2) Tries to transmit IPC messages in the skb_txq by invoking xmit_ipc_msg()
 *    function.
 * 3) Sends an interrupt to CP if there is no error from xmit_ipc_msg().
 * 4) Starts SHMEM flow control if xmit_ipc_msg() returns -ENOSPC.
 */
static void shmem_send_udl(struct shmem_link_device *shmd, int dev,
			struct io_device *iod, struct sk_buff *skb)
{
	struct link_device *ld = &shmd->ld;
	struct sk_buff_head *txq = ld->skb_txq[dev];
	struct completion *cmpl = &shmd->req_ack_cmpl[dev];
	struct std_dload_info *dl_info = &shmd->dl_info;
	struct mem_status mst;
	u32 timeout = msecs_to_jiffies(RES_ACK_WAIT_TIMEOUT);
	u32 udl_cmd;
	int ret;
	u16 mask = get_mask_req_ack(shmd, dev) | get_mask_send(shmd, dev);

	memset(&mst, 0, sizeof(struct mem_status));

	udl_cmd = std_udl_get_cmd(skb->data);
	if (iod->format == IPC_DUMP || !std_udl_with_payload(udl_cmd)) {
		ret = shmem_send_udl_cmd(shmd, dev, iod, skb);
		if (ret > 0)
			send_int2cp(shmd, INT_NON_CMD(mask));
		else
			mif_err("ERR! shmem_send_udl_cmd fail(err %d)\n", ret);
		goto exit;
	}

	skb_queue_tail(txq, skb);
	if (txq->qlen < dl_info->num_frames)
		goto exit;

	while (1) {
		ret = shmem_send_udl_data(shmd, dev);
		if (ret < 0) {
			mif_err("ERR! shmem_send_udl_data fail(err %d)\n", ret);
			skb_queue_purge(txq);
			break;
		}

		if (skb_queue_empty(txq)) {
			send_int2cp(shmd, INT_NON_CMD(mask));
			break;
		}

		send_int2cp(shmd, INT_NON_CMD(mask));

		do {
			ret = wait_for_completion_timeout(cmpl, timeout);
			get_shmem_status(shmd, TX, &mst);
		} while (mst.head[dev][TX] != mst.tail[dev][TX]);
	}

exit:
	return;
}

/**
 * shmem_send
 * @ld: pointer to an instance of the link_device structure
 * @iod: pointer to an instance of the io_device structure
 * @skb: pointer to an skb that will be transmitted
 *
 * Returns the length of data transmitted or an error code.
 *
 * Normal call flow for an IPC message:
 *   shmem_try_send_ipc -> shmem_send_ipc -> xmit_ipc_msg -> write_ipc_to_txq
 *
 * Call flow on congestion in a IPC TXQ:
 *   shmem_try_send_ipc -> shmem_send_ipc -> xmit_ipc_msg ,,, queue_delayed_work
 *   => xxx_tx_work -> wait_for_res_ack
 *   => msg_handler
 *   => process_res_ack -> xmit_ipc_msg (,,, queue_delayed_work ...)
 */
static int shmem_send(struct link_device *ld, struct io_device *iod,
			struct sk_buff *skb)
{
	struct shmem_link_device *shmd = to_shmem_link_device(ld);
	struct modem_ctl *mc = ld->mc;
	int dev = iod->format;
	int len = skb->len;

	switch (dev) {
	case IPC_FMT:
	case IPC_RAW:
		if (likely(ld->mode == LINK_MODE_IPC)) {
			if (unlikely(shmd->forced_cp_crash)) {
				mif_err("%s:%s->%s: ERR! Forced CP Crash ...\n",
					ld->name, iod->name, mc->name);
				dev_kfree_skb_any(skb);
			} else {
				shmem_try_send_ipc(shmd, dev, iod, skb);
			}
		} else {
			mif_err("%s:%s->%s: ERR! ld->mode != LINK_MODE_IPC\n",
				ld->name, iod->name, mc->name);
			dev_kfree_skb_any(skb);
		}
		break;

	case IPC_BOOT:
	case IPC_DUMP:
		shmem_send_udl(shmd, IPC_RAW, iod, skb);
		break;

	default:
		mif_err("%s:%s->%s: ERR! Invalid IOD (format %d)\n",
			ld->name, iod->name, mc->name, dev);
		dev_kfree_skb_any(skb);
		len = -ENODEV;
		break;
	}

	return len;
}

static int shmem_dload_start(struct link_device *ld, struct io_device *iod)
{
	struct shmem_link_device *shmd = to_shmem_link_device(ld);
	u32 magic;

	ld->mode = LINK_MODE_DLOAD;

	clear_shmem_map(shmd);
	msq_reset(&shmd->rx_msq);

	set_magic(shmd, SHM_BOOT_MAGIC);
	magic = get_magic(shmd);
	if (magic != SHM_BOOT_MAGIC) {
		mif_err("%s: ERR! magic 0x%08X != SHM_BOOT_MAGIC 0x%08X\n",
			ld->name, magic, SHM_BOOT_MAGIC);
		return -EFAULT;
	}

	return 0;
}

/**
 * shmem_set_dload_info
 * @ld: pointer to an instance of link_device structure
 * @iod: pointer to an instance of io_device structure
 * @arg: pointer to an instance of std_dload_info structure in "user" memory
 *
 */
static int shmem_set_dload_info(struct link_device *ld, struct io_device *iod,
			unsigned long arg)
{
	struct shmem_link_device *shmd = to_shmem_link_device(ld);
	struct std_dload_info *dl_info = &shmd->dl_info;
	int ret;

	ret = copy_from_user(dl_info, (void __user *)arg,
			sizeof(struct std_dload_info));
	if (ret) {
		mif_err("ERR! copy_from_user fail!\n");
		return -EFAULT;
	}

	return 0;
}

static int shmem_force_dump(struct link_device *ld, struct io_device *iod)
{
	struct shmem_link_device *shmd = to_shmem_link_device(ld);
	mif_err("+++\n");
	trigger_forced_cp_crash(shmd);
	mif_err("---\n");
	return 0;
}

static inline void shmem_send_thermal(struct link_device *ld,
		struct io_device *iod, int arg)
{
	struct shmem_link_device *shmd = to_shmem_link_device(ld);

	if (ld->mode != LINK_MODE_IPC) {
		mif_info("%s: <by %pf> mask 0x%04X\n", ld->name, CALLER, arg);
		return;
	}

	mbox_set_value(shmd->mbx_ap2cp_tmu_msg, arg);
	mbox_set_interrupt(shmd->int_ap2cp_tmu_msg);
}

static int shmem_dump_start(struct link_device *ld, struct io_device *iod)
{
	struct shmem_link_device *shmd = to_shmem_link_device(ld);

	ld->mode = LINK_MODE_ULOAD;

	clear_shmem_map(shmd);
	msq_reset(&shmd->rx_msq);

	mif_err("%s: magic = 0x%08X\n", ld->name, SHM_DUMP_MAGIC);
	set_magic(shmd, SHM_DUMP_MAGIC);

	return 0;
}

static void shmem_remap_4mb_ipc_region(struct shmem_link_device *shmd)
{
	struct shmem_4mb_phys_map *map;
	struct shmem_ipc_device *dev;

	map = (struct shmem_4mb_phys_map *)shmd->base;

	/* Magic code and access enable fields */
	shmd->ipc_map.magic = (u32 __iomem *)&map->magic;
	shmd->ipc_map.access = (u32 __iomem *)&map->access;

	/* FMT */
	dev = &shmd->ipc_map.dev[IPC_FMT];

	memmove(dev->name, "FMT", strlen("FMT"));
	dev->id = IPC_FMT;

	dev->txq.head = (u32 __iomem *)&map->fmt_tx_head;
	dev->txq.tail = (u32 __iomem *)&map->fmt_tx_tail;
	dev->txq.buff = (u8 __iomem *)&map->fmt_tx_buff[0];
	dev->txq.size = SHM_4M_FMT_TX_BUFF_SZ;

	dev->rxq.head = (u32 __iomem *)&map->fmt_rx_head;
	dev->rxq.tail = (u32 __iomem *)&map->fmt_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&map->fmt_rx_buff[0];
	dev->rxq.size = SHM_4M_FMT_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_F;
	dev->mask_res_ack = INT_MASK_RES_ACK_F;
	dev->mask_send    = INT_MASK_SEND_F;

	/* RAW */
	dev = &shmd->ipc_map.dev[IPC_RAW];

	memmove(dev->name, "RAW", strlen("RAW"));
	dev->id = IPC_RAW;

	dev->txq.head = (u32 __iomem *)&map->raw_tx_head;
	dev->txq.tail = (u32 __iomem *)&map->raw_tx_tail;
	dev->txq.buff = (u8 __iomem *)&map->raw_tx_buff[0];
	dev->txq.size = SHM_4M_RAW_TX_BUFF_SZ;

	dev->rxq.head = (u32 __iomem *)&map->raw_rx_head;
	dev->rxq.tail = (u32 __iomem *)&map->raw_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&map->raw_rx_buff[0];
	dev->rxq.size = SHM_4M_RAW_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_R;
	dev->mask_res_ack = INT_MASK_RES_ACK_R;
	dev->mask_send    = INT_MASK_SEND_R;

	/* interrupt ports */
	shmd->ipc_map.mbx2ap = NULL;
	shmd->ipc_map.mbx2cp = NULL;
}

static int shmem_init_ipc_map(struct shmem_link_device *shmd)
{
	if (shmd->size >= SZ_4M)
		shmem_remap_4mb_ipc_region(shmd);
	else
		return -EINVAL;

	memset(shmd->base, 0, shmd->size);

	shmd->magic = shmd->ipc_map.magic;
	shmd->access = shmd->ipc_map.access;

	shmd->dev[IPC_FMT] = &shmd->ipc_map.dev[IPC_FMT];
	shmd->dev[IPC_RAW] = &shmd->ipc_map.dev[IPC_RAW];

	shmd->mbx2ap = shmd->ipc_map.mbx2ap;
	shmd->mbx2cp = shmd->ipc_map.mbx2cp;

	return 0;
}

static void shmem_link_terminate(struct link_device *ld, struct io_device *iod)
{
	if (iod->format == IPC_FMT && ld->mode == LINK_MODE_IPC) {
		if (!atomic_read(&iod->opened)) {
			ld->mode = LINK_MODE_OFFLINE;
			mif_err("%s: %s: link mode changed: IPC -> OFFLINE\n",
				iod->name, ld->name);
		}
	}

	return;
}

static int shmem_xmit_boot(struct shmem_link_device *shmd,
		struct io_device *iod, unsigned long arg)
{
	struct modem_firmware mf;
	struct link_device *ld = &shmd->ld;
	struct modem_data *modem = ld->mdm_data;
	size_t size = 0;
	int err;
	u8 __iomem *bl_buff = NULL;

	mif_info("%s\n", __func__);
	memset(&mf, 0, sizeof(struct modem_firmware));
	/* Get information about the boot image */
	err = copy_from_user(&mf, (const void __user *)arg,
			sizeof(struct modem_firmware));
	if (err) {
		mif_err("%s: ERR! INFO copy_from_user fail\n", ld->name);
		err = -EFAULT;
		goto exit;
	}

	size = mf.size;
	mif_info("Get cp bootloader(size : %d bytes)\n", mf.size);
	if (mf.size > SZ_8K) {
		mif_err("Unexpected cp bootloader size : 0x%x\n", mf.size);
		goto exit;
	}

	bl_buff = shm_request_region(modem->shmem_base, SZ_8K);
	if (!bl_buff) {
		mif_err("%s: ERR! bootloader_reserved_region fail\n", ld->name);
		goto exit;
	}

	err = copy_from_user((void *)bl_buff, (void __user *)mf.binary, size);
	if (err) {
		mif_err("%s: ERR! BOOT copy_from_user fail\n", ld->name);
		err = -EFAULT;
		goto exit;
	}

exit:
	vunmap(bl_buff);

	return 0;
}

static int shmem_xmit_bin(struct link_device *ld, struct io_device *iod,
				unsigned long arg)
{
	struct data_info di;
	struct modem_data *modem = ld->mdm_data;
	struct modem_ctl *mc = ld->mc;
	u32 size = 0;
	u8 __iomem *mem_base;
	int err;
#if defined(CONFIG_LTE_MODEM_S5E7890) || defined(CONFIG_LTE_MODEM_S5E8890)
	unsigned int modem_ver_offset;
	char *info_addr;
	int i;
#endif

	memset(&di, 0, sizeof(struct data_info));

	err = copy_from_user(&di, (const void __user *)arg,
			sizeof(struct data_info));
	if (err) {
		mif_err("%s: ERR! INFO copy_from_user fail\n", ld->name);
		err = -EFAULT;
		goto exit;
	}

	if (di.len > di.total_size) {
		mif_err("Unexpected cp binary size : 0x%x\n", di.len);
		goto exit;
	}

	size = di.len;
	if (di.stage == BOOT) {
		mif_info("Get cp bootloader(size : %d bytes)\n", di.total_size);

		if (di.total_size > SZ_8K) {
			mif_err("Unexpected cp bootloader size : 0x%x\n",
					di.total_size);
			goto exit;
		}

		if (IS_ENABLED(CONFIG_CP_SECURE_BOOT)) {
			if (mc->phone_state == STATE_CRASH_EXIT)
				mem_base = modem->dump_base + di.m_offset;
			else
				mem_base = modem->modem_base + di.m_offset;
		} else {
			mem_base = modem->modem_base + di.m_offset;
		}

		err = copy_from_user(mem_base, (void __user *)di.buff, size);
		if (err) {
			mif_err("%s: ERR! BOOT copy_from_user fail\n",
					ld->name);
			err = -EFAULT;
			goto exit;
		}

		/* change magic code for boot */
		shmem_dload_start(ld, iod);
	} else if (di.stage == MAIN) {
		err = copy_from_user(modem->modem_base + di.m_offset,
				(void __user *)di.buff, size);
		if (err) {
			mif_err("%s: ERR! MAIN copy_from_user fail\n",
					ld->name);
			err = -EFAULT;
			goto exit;
		}
#if defined(CONFIG_LTE_MODEM_S5E7890) || defined(CONFIG_LTE_MODEM_S5E8890)
		/* Save CP binary information */
		if (*(unsigned int *)(modem->modem_base + MODEM_VER_ID_OFFSET)
				== MODEM_VER_ID_VALUE) {
			modem_ver_offset = *(unsigned int *)(modem->modem_base
					+ MODEM_VER_ADDR_OFFSET);
			modem_ver_offset = modem_ver_offset & 0x0fffffff;

			info_addr = (char *)modem->modem_base + modem_ver_offset;
			for (i = 0; i < 30; i++) {
				if (*(char *)(info_addr + i) == 0)
					break;
			}
			memmove(mc->info_cp_build, info_addr, i);
			mc->info_cp_build[i] = 0;

			info_addr = (char *)(info_addr + i + 1);
			for (i = 0; i < 30; i++) {
				if (*(char *)(info_addr + i) == 0)
					break;
			}

			info_addr = (char *)(info_addr + i + 1);
			for (i = 0; i < 30; i++) {
				if (*(char *)(info_addr + i) == 0)
					break;
			}
			memmove(mc->info_cp_carr, info_addr, i);
			mc->info_cp_carr[i] = 0;
		}
#endif
	} else if (di.stage == NV) {
		err = copy_from_user(modem->modem_base + di.m_offset,
				(void __user *)di.buff, size);
		if (err) {
			mif_err("%s: ERR! NV copy_from_user fail\n", ld->name);
			err = -EFAULT;
			goto exit;
		}
	} else if (di.stage == NV_PROT) {
		err = copy_from_user(modem->modem_base + di.m_offset,
				(void __user *)di.buff, size);
		if (err) {
			mif_err("%s: ERR! NV_PROT copy_from_user fail\n",
					ld->name);
			err = -EFAULT;
			goto exit;
		}
	}

exit:
	return 0;
}

static int shmem_sec_init(struct link_device *ld, struct io_device *iod,
				unsigned long arg)
{
	enum cp_boot_mode mode = (enum cp_boot_mode)arg;
	int err = 0;

	mif_info("%s\n", __func__);

	/* How to use exynos_smc
	 * exynos_smc(unsigned long cmd, unsigned long arg1, unsigned long arg2, unsigned long arg3)
	 * x0: smc_cmd_id
	 * x1: mode(0x0: boot, 0x1: dump 0x2: re_init)
	 * x2: size(binary size)
	 * x3: addr(only dump mode)
	 */
	err = exynos_smc(SMC_ID, mode, 0, 0);
	mif_info("%s:smc call return value: %d\n", __func__, err);

	return err;
}

static int shmem_security_check(struct link_device *ld, struct io_device *iod,
				unsigned long arg)
{
	struct modem_ctl *mc = ld->mc;
	struct modem_data *modem = ld->mdm_data;
	struct sec_info info;
	int err = 0;

	exynos_smc(SMC_ID_CLK, SSS_CLK_ENABLE, 0, 0);

	err = copy_from_user(&info, (const void __user *)arg,
			sizeof(struct sec_info));
	if (err) {
		mif_err("%s: ERR! security_check copy_from_user fail\n",
				ld->name);
		err = -EFAULT;
		goto exit;
	}

	mif_err("%s: call requeset_security_check(mode: %d, boot_size: %d, main_size: %d\n",
		ld->name, info.mode, info.boot_size, info.main_size);

	if (mc->phone_state == STATE_CRASH_EXIT)
		err = exynos_smc(SMC_ID, info.mode, info.boot_size,
				modem->dump_addr);
	else
		err = exynos_smc(SMC_ID, info.mode, info.boot_size,
				info.main_size);

	mif_info("%s:smc call return value: %d\n", __func__, err);
exit:
	exynos_smc(SMC_ID_CLK, SSS_CLK_DISABLE, 0, 0);

	return err;
}

struct link_device *shmem_create_link_device(struct platform_device *pdev)
{
	struct shmem_link_device *shmd = NULL;
	struct link_device *ld = NULL;
	struct modem_data *modem = NULL;
	struct device *dev = &pdev->dev;
	int err = 0;
	int i = 0;
	mif_info("+++\n");

	/* Get the modem (platform) data */
	modem = (struct modem_data *)dev->platform_data;
	if (!modem) {
		mif_err("ERR! modem == NULL\n");
		return NULL;
	}
	mif_err("%s: %s\n", modem->link_name, modem->name);

	if (!modem->mbx) {
		mif_err("%s: ERR! %s->mbx == NULL\n",
			modem->link_name, modem->name);
		return NULL;
	}

	/* Alloc an instance of shmem_link_device structure */
	shmd = devm_kzalloc(dev, sizeof(struct shmem_link_device), GFP_KERNEL);
	if (!shmd) {
		mif_err("%s: ERR! shmd kzalloc fail\n", modem->link_name);
		goto error;
	}
	ld = &shmd->ld;

	/* Retrieve modem data and SHMEM control data from the modem data */
	ld->mdm_data = modem;
	ld->name = modem->link_name;
	ld->aligned = 1;
	ld->max_ipc_dev = MAX_EXYNOS_DEVICES;

	/* Set attributes as a link device */
	ld->terminate_comm = shmem_link_terminate;
	ld->send = shmem_send;
	ld->dload_start = shmem_dload_start;
	ld->firm_update = shmem_set_dload_info;
	ld->force_dump = shmem_force_dump;
	ld->dump_start = shmem_dump_start;
	ld->send_tmu = shmem_send_thermal;
	ld->xmit_boot = (int (*)(struct link_device *ld,
		struct io_device *iod, unsigned long arg))shmem_xmit_boot;

	ld->xmit_bin = shmem_xmit_bin;
	ld->check_security = shmem_security_check;
	ld->sec_init = shmem_sec_init;

	INIT_LIST_HEAD(&ld->list);

	skb_queue_head_init(&ld->sk_fmt_tx_q);
	skb_queue_head_init(&ld->sk_raw_tx_q);
	ld->skb_txq[IPC_FMT] = &ld->sk_fmt_tx_q;
	ld->skb_txq[IPC_RAW] = &ld->sk_raw_tx_q;

	skb_queue_head_init(&ld->sk_fmt_rx_q);
	skb_queue_head_init(&ld->sk_raw_rx_q);
	ld->skb_rxq[IPC_FMT] = &ld->sk_fmt_rx_q;
	ld->skb_rxq[IPC_RAW] = &ld->sk_raw_rx_q;

	init_completion(&ld->init_cmpl);

	/* Retrieve SHMEM resource */
	if (modem->link_types & LINKTYPE(LINKDEV_SHMEM)) {
		shmd->type = REAL_SHMEM;
		mif_debug("%s: shmd->type = REAL_SHMEM\n", ld->name);
	} else {
		mif_err("%s: ERR! invalid type\n", ld->name);
		goto error;
	}

	/* Initialize CP Reserved mem */
	modem->modem_base = shm_request_region(modem->shmem_base,
			modem->ipcmem_offset);
	if (!modem->modem_base) {
		mif_err("%s: ERR! cp_reserved_region fail\n", ld->name);
		goto error;
	}
	mif_err("%s: cp phys_addr:0x%08X virt_addr:0x%p size: %d\n", ld->name,
		modem->shmem_base, modem->modem_base, modem->ipcmem_offset);

	shmd->start = modem->shmem_base + modem->ipcmem_offset;
	shmd->size = modem->ipc_size;
	shmd->base = shm_request_region(shmd->start, shmd->size);
	if (!shmd->base) {
		mif_err("%s: ERR! shm_request_region fail\n", ld->name);
		goto error;
	}
	mif_err("%s: phys_addr:0x%08X virt_addr:0x%8p size:%d\n",
		ld->name, shmd->start, shmd->base, shmd->size);

	modem->ipc_base = (u8 __iomem *)shmd->base;
	mif_err(">>>>>>>>> 0x%p virt_addr:0x%p\n",
		modem->ipc_base, modem->ipc_base + 0x1000);
	modem->dump_addr = shmd->start + modem->dump_offset;
	modem->dump_base = (u8 __iomem *)shmd->base + modem->dump_offset;

	/* Initialize SHMEM maps (physical map -> logical map) */
	err = shmem_init_ipc_map(shmd);
	if (err < 0) {
		mif_err("%s: ERR! shmem_init_ipc_map fail (err %d)\n",
			ld->name, err);
		goto error;
	}

	/* Initialize locks, completions, and bottom halves */
	snprintf(shmd->wlock_name, MIF_MAX_NAME_LEN, "%s_wlock", ld->name);
	wake_lock_init(&shmd->wlock, WAKE_LOCK_SUSPEND, shmd->wlock_name);

	init_completion(&shmd->udl_cmpl);
	for (i = 0; i < MAX_EXYNOS_DEVICES; i++)
		init_completion(&shmd->req_ack_cmpl[i]);

	tasklet_init(&shmd->rx_tsk, ipc_rx_task, (unsigned long)shmd);
	INIT_DELAYED_WORK(&shmd->msg_rx_dwork, msg_rx_work);
	INIT_DELAYED_WORK(&shmd->udl_rx_dwork, udl_rx_work);

	for (i = 0; i < MAX_EXYNOS_DEVICES; i++) {
		spin_lock_init(&shmd->tx_lock[i]);
		atomic_set(&shmd->res_required[i], 0);
	}

	ld->tx_wq = create_singlethread_workqueue("shmem_tx_wq");
	if (!ld->tx_wq) {
		mif_err("%s: ERR! fail to create tx_wq\n", ld->name);
		goto error;
	}
	INIT_DELAYED_WORK(&ld->fmt_tx_dwork, fmt_tx_work);
	INIT_DELAYED_WORK(&ld->raw_tx_dwork, raw_tx_work);
	ld->tx_dwork[IPC_FMT] = &ld->fmt_tx_dwork;
	ld->tx_dwork[IPC_RAW] = &ld->raw_tx_dwork;

	spin_lock_init(&shmd->tx_msq.lock);
	spin_lock_init(&shmd->rx_msq.lock);

#ifdef CONFIG_DEBUG_MODEM_IF
	spin_lock_init(&shmd->trace_list.lock);
	INIT_DELAYED_WORK(&shmd->dump_dwork, mem_dump_work);
#endif

	/* Retrieve SHMEM MBOX#, IRQ#, etc. */
	shmd->mbx_cp2ap_msg = modem->mbx->mbx_cp2ap_msg;
	shmd->irq = modem->mbx->irq_cp2ap_msg;
	shmd->mbx_ap2cp_msg = modem->mbx->mbx_ap2cp_msg;
	shmd->int_ap2cp_msg = modem->mbx->int_ap2cp_msg;

	/* Register interrupt handlers */
	err = mbox_request_irq(shmd->irq, shmem_irq_handler, shmd);
	if (err) {
		mif_err("%s: ERR! mbox_request_irq fail (err %d)\n",
			ld->name, err);
		goto error;
	}

	mif_info("---\n");
	return ld;

error:
	mif_err("xxx\n");
	kfree(shmd);
	return NULL;
}
