/*
 * Copyright (C) 2011 Samsung Electronics.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/wakelock.h>

#include "modem_prj.h"
#include "modem_utils.h"

#define CMD_SUSPEND	((unsigned short)(0x00CA))
#define CMD_RESUME	((unsigned short)(0x00CB))

#define TX_SEPARATOR	"mif: >>>>>>>>>> Outgoing packet "
#define RX_SEPARATOR	"mif: Incoming packet <<<<<<<<<<"
#define LINE_SEPARATOR	\
	"mif: ------------------------------------------------------------"
#define LINE_BUFF_SIZE	80

static const char *hex = "0123456789abcdef";

void ts2utc(struct timespec *ts, struct utc_time *utc)
{
	struct tm tm;

	time_to_tm((ts->tv_sec - (sys_tz.tz_minuteswest * 60)), 0, &tm);
	utc->year = 1900 + tm.tm_year;
	utc->mon = 1 + tm.tm_mon;
	utc->day = tm.tm_mday;
	utc->hour = tm.tm_hour;
	utc->min = tm.tm_min;
	utc->sec = tm.tm_sec;
	utc->msec = ns2ms(ts->tv_nsec);
	utc->us = ns2us(ts->tv_nsec);
}

void get_utc_time(struct utc_time *utc)
{
	struct timespec ts;
	getnstimeofday(&ts);
	ts2utc(&ts, utc);
}
EXPORT_SYMBOL(get_utc_time);

int mif_dump_log(struct modem_shared *msd, struct io_device *iod)
{
	struct sk_buff *skb;
	unsigned long read_len = 0;
	unsigned long int flags;

	spin_lock_irqsave(&msd->lock, flags);
	while (read_len < MAX_MIF_BUFF_SIZE) {
		skb = alloc_skb(MAX_IPC_SKB_SIZE, GFP_ATOMIC);
		if (!skb) {
			mif_err("ERR! alloc_skb fail\n");
			spin_unlock_irqrestore(&msd->lock, flags);
			return -ENOMEM;
		}
		memcpy(skb_put(skb, MAX_IPC_SKB_SIZE),
			msd->storage.addr + read_len, MAX_IPC_SKB_SIZE);
		skb_queue_tail(&iod->sk_rx_q, skb);
		read_len += MAX_IPC_SKB_SIZE;
		wake_up(&iod->wq);
	}
	spin_unlock_irqrestore(&msd->lock, flags);
	return 0;
}

static unsigned long long get_kernel_time(void)
{
	int this_cpu;
	unsigned long flags;
	unsigned long long time;

	preempt_disable();
	raw_local_irq_save(flags);

	this_cpu = smp_processor_id();
	time = cpu_clock(this_cpu);

	preempt_enable();
	raw_local_irq_restore(flags);

	return time;
}

void mif_ipc_log(enum mif_log_id id,
	struct modem_shared *msd, const char *data, size_t len)
{
	struct mif_ipc_block *block;
	unsigned long int flags;

	spin_lock_irqsave(&msd->lock, flags);

	block = (struct mif_ipc_block *)
		(msd->storage.addr + (MAX_LOG_SIZE * msd->storage.cnt));
	msd->storage.cnt = ((msd->storage.cnt + 1) < MAX_LOG_CNT) ?
		msd->storage.cnt + 1 : 0;

	spin_unlock_irqrestore(&msd->lock, flags);

	block->id = id;
	block->time = get_kernel_time();
	block->len = (len > MAX_IPC_LOG_SIZE) ? MAX_IPC_LOG_SIZE : len;
	memcpy(block->buff, data, block->len);
}

void _mif_irq_log(enum mif_log_id id, struct modem_shared *msd,
	struct mif_irq_map map, const char *data, size_t len)
{
	struct mif_irq_block *block;
	unsigned long int flags;

	spin_lock_irqsave(&msd->lock, flags);

	block = (struct mif_irq_block *)
		(msd->storage.addr + (MAX_LOG_SIZE * msd->storage.cnt));
	msd->storage.cnt = ((msd->storage.cnt + 1) < MAX_LOG_CNT) ?
		msd->storage.cnt + 1 : 0;

	spin_unlock_irqrestore(&msd->lock, flags);

	block->id = id;
	block->time = get_kernel_time();
	memcpy(&(block->map), &map, sizeof(struct mif_irq_map));
	if (data)
		memcpy(block->buff, data,
			(len > MAX_IRQ_LOG_SIZE) ? MAX_IRQ_LOG_SIZE : len);
}

void _mif_com_log(enum mif_log_id id,
	struct modem_shared *msd, const char *format, ...)
{
	struct mif_common_block *block;
	unsigned long int flags;
	va_list args;

	spin_lock_irqsave(&msd->lock, flags);

	block = (struct mif_common_block *)
		(msd->storage.addr + (MAX_LOG_SIZE * msd->storage.cnt));
	msd->storage.cnt = ((msd->storage.cnt + 1) < MAX_LOG_CNT) ?
		msd->storage.cnt + 1 : 0;

	spin_unlock_irqrestore(&msd->lock, flags);

	block->id = id;
	block->time = get_kernel_time();

	va_start(args, format);
	vsnprintf(block->buff, MAX_COM_LOG_SIZE, format, args);
	va_end(args);
}

void _mif_time_log(enum mif_log_id id, struct modem_shared *msd,
	struct timespec epoch, const char *data, size_t len)
{
	struct mif_time_block *block;
	unsigned long int flags;

	spin_lock_irqsave(&msd->lock, flags);

	block = (struct mif_time_block *)
		(msd->storage.addr + (MAX_LOG_SIZE * msd->storage.cnt));
	msd->storage.cnt = ((msd->storage.cnt + 1) < MAX_LOG_CNT) ?
		msd->storage.cnt + 1 : 0;

	spin_unlock_irqrestore(&msd->lock, flags);

	block->id = id;
	block->time = get_kernel_time();
	memcpy(&block->epoch, &epoch, sizeof(struct timespec));

	if (data)
		memcpy(block->buff, data,
			(len > MAX_IRQ_LOG_SIZE) ? MAX_IRQ_LOG_SIZE : len);
}

/* dump2hex
 * dump data to hex as fast as possible.
 * the length of @buff must be greater than "@len * 3"
 * it need 3 bytes per one data byte to print.
 */
static inline int dump2hex(char *buff, const char *data, size_t len)
{
	char *dest = buff;
	int i;

	for (i = 0; i < len; i++) {
		*dest++ = hex[(data[i] >> 4) & 0xf];
		*dest++ = hex[data[i] & 0xf];
		*dest++ = ' ';
	}
	if (likely(len > 0))
		dest--; /* last space will be overwrited with null */

	*dest = '\0';

	return dest - buff;
}

static inline void pr_ipc_msg(int level, u8 ch, struct timespec *ts,
				const char *prefix, const u8 *msg,
				unsigned int len)
{
	size_t offset;
	struct utc_time utc;
	char str[MAX_STR_LEN] = {0, };

	/* If @ch is for BOOT or DUMP, only UDL command without any payload
	 * should be printed. */
	if (exynos_udl_ch(ch)) {
		u32 udl_cmd = *((u32 *)msg);
		if (std_udl_with_payload(udl_cmd))
			return;
	}

	ts2utc(ts, &utc);

	if (prefix)
		snprintf(str, MAX_STR_LEN, "%s", prefix);

	offset = strlen(str);
	dump2hex((str + offset), msg, (len > MAX_HEX_LEN ? MAX_HEX_LEN : len));

	if (level > 0) {
		pr_err("%s: " HMSU_FMT " %s\n", MIF_TAG,
			utc.hour, utc.min, utc.sec, utc.msec, str);
	}
}

void log_ipc_pkt(struct sk_buff *skb, enum ipc_layer layer, enum direction dir)
{
	struct io_device *iod;
	struct link_device *ld;
	char prefix[MAX_PREFIX_LEN] = {0, };
	struct timespec *ts;
	unsigned int hdr_len;
	unsigned int msg_len;
	u8 *msg;
	u8 *hdr;
	u8 ch;

	if (!log_info.debug_log)
		return;

	iod = skbpriv(skb)->iod;
	ld = skbpriv(skb)->ld;
	ch = skbpriv(skb)->exynos_ch;

	getnstimeofday(&skbpriv(skb)->ts);
	/**
	 * Make a string of the route
	 */
	snprintf(prefix, MAX_PREFIX_LEN, "%s %s: %s: %s%s%s: ",
		layer_str(layer), dir_str(dir), ld->name,
		iod->name, arrow(dir), iod->mc->name);

	hdr = skbpriv(skb)->lnk_hdr ? skb->data : NULL;
	hdr_len = hdr ? EXYNOS_HEADER_SIZE : 0;
	if (hdr_len > 0) {
		char *separation = " | ";
		size_t offset = strlen(prefix);

		dump2hex((prefix + offset), hdr, hdr_len);
		strncat(prefix, separation, strlen(separation));
	}

	/**
	* Print an IPC message with the prefix
    */
	ts = &skbpriv(skb)->ts;
	msg = skb->data + hdr_len;
	msg_len = (skb->len - hdr_len);

	if (exynos_fmt_ch(ch))
		pr_ipc_msg(log_info.fmt_msg, ch, ts, prefix, msg, msg_len);
	else if (exynos_boot_ch(ch))
		pr_ipc_msg(log_info.boot_msg, ch, ts, prefix, msg, msg_len);
	else if (exynos_dump_ch(ch))
		pr_ipc_msg(log_info.dump_msg, ch, ts, prefix, msg, msg_len);
	else if (exynos_rfs_ch(ch))
		pr_ipc_msg(log_info.rfs_msg, ch, ts, prefix, msg, msg_len);
	else if (exynos_log_ch(ch))
		pr_ipc_msg(log_info.log_msg, ch, ts, prefix, msg, msg_len);
	else if (exynos_ps_ch(ch))
		pr_ipc_msg(log_info.ps_msg, ch, ts, prefix, msg, msg_len);
	else if (exynos_router_ch(ch))
		pr_ipc_msg(log_info.router_msg, ch, ts, prefix, msg, msg_len);
}

void pr_ipc(int level, const char *tag, const char *data, size_t len)
{
	struct utc_time utc;
	unsigned char str[128];

	if (level < 0)
		return;

	get_utc_time(&utc);
	dump2hex(str, data, (len > 32 ? 32 : len));
	if (level > 0) {
		pr_err("%s: [%02d:%02d:%02d.%03d] %s: %s\n", MIF_TAG,
			utc.hour, utc.min, utc.sec, utc.msec, tag, str);
	} else {
		pr_info("%s: [%02d:%02d:%02d.%03d] %s: %s\n", MIF_TAG,
			utc.hour, utc.min, utc.sec, utc.msec, tag, str);
	}
}

/* print buffer as hex string */
int pr_buffer(const char *tag, const char *data, size_t data_len,
							size_t max_len)
{
	size_t len = min(data_len, max_len);
	unsigned char str[len ? len * 3 : 1]; /* 1 <= sizeof <= max_len*3 */
	dump2hex(str, data, len);

	/* don't change this printk to mif_debug for print this as level7 */
	return printk(KERN_INFO "%s: %s(%lu): %s%s\n", MIF_TAG, tag, data_len,
			str, (len == data_len) ? "" : " ...");
}

/* flow control CM from CP, it use in serial devices */
int link_rx_flowctl_cmd(struct link_device *ld, const char *data, size_t len)
{
	struct modem_shared *msd = ld->msd;
	unsigned short *cmd, *end = (unsigned short *)(data + len);

	mif_debug("flow control cmd: size=%ld\n", len);

	for (cmd = (unsigned short *)data; cmd < end; cmd++) {
		switch (*cmd) {
		case CMD_SUSPEND:
			iodevs_for_each(msd, iodev_netif_stop, 0);
			ld->raw_tx_suspended = true;
			mif_info("flowctl CMD_SUSPEND(%04X)\n", *cmd);
			break;

		case CMD_RESUME:
			iodevs_for_each(msd, iodev_netif_wake, 0);
			ld->raw_tx_suspended = false;
			complete_all(&ld->raw_tx_resumed_by_cp);
			mif_info("flowctl CMD_RESUME(%04X)\n", *cmd);
			break;

		default:
			mif_err("flowctl BAD CMD: %04X\n", *cmd);
			break;
		}
	}

	return 0;
}

struct io_device *get_iod_with_channel(struct modem_shared *msd,
					unsigned channel)
{
	struct rb_node *n = msd->iodevs_tree_chan.rb_node;
	struct io_device *iodev;
	while (n) {
		iodev = rb_entry(n, struct io_device, node_chan);
		if (channel < iodev->id)
			n = n->rb_left;
		else if (channel > iodev->id)
			n = n->rb_right;
		else
			return iodev;
	}
	return NULL;
}

struct io_device *get_iod_with_format(struct modem_shared *msd,
			enum dev_format format)
{
	struct rb_node *n = msd->iodevs_tree_fmt.rb_node;
	struct io_device *iodev;
	while (n) {
		iodev = rb_entry(n, struct io_device, node_fmt);
		if (format < iodev->format)
			n = n->rb_left;
		else if (format > iodev->format)
			n = n->rb_right;
		else
			return iodev;
	}
	return NULL;
}

struct io_device *insert_iod_with_channel(struct modem_shared *msd,
		unsigned channel, struct io_device *iod)
{
	struct rb_node **p = &msd->iodevs_tree_chan.rb_node;
	struct rb_node *parent = NULL;
	struct io_device *iodev;
	while (*p) {
		parent = *p;
		iodev = rb_entry(parent, struct io_device, node_chan);
		if (channel < iodev->id)
			p = &(*p)->rb_left;
		else if (channel > iodev->id)
			p = &(*p)->rb_right;
		else
			return iodev;
	}
	rb_link_node(&iod->node_chan, parent, p);
	rb_insert_color(&iod->node_chan, &msd->iodevs_tree_chan);
	return NULL;
}

struct io_device *insert_iod_with_format(struct modem_shared *msd,
		enum dev_format format, struct io_device *iod)
{
	struct rb_node **p = &msd->iodevs_tree_fmt.rb_node;
	struct rb_node *parent = NULL;
	struct io_device *iodev;
	while (*p) {
		parent = *p;
		iodev = rb_entry(parent, struct io_device, node_fmt);
		if (format < iodev->format)
			p = &(*p)->rb_left;
		else if (format > iodev->format)
			p = &(*p)->rb_right;
		else
			return iodev;
	}
	rb_link_node(&iod->node_fmt, parent, p);
	rb_insert_color(&iod->node_fmt, &msd->iodevs_tree_fmt);
	return NULL;
}

void iodevs_for_each(struct modem_shared *msd, action_fn action, void *args)
{
	struct io_device *iod;
	struct rb_node *node = rb_first(&msd->iodevs_tree_chan);
	for (; node; node = rb_next(node)) {
		iod = rb_entry(node, struct io_device, node_chan);
		action(iod, args);
	}
}

void iodev_netif_wake(struct io_device *iod, void *args)
{
	if (iod->io_typ == IODEV_NET && iod->ndev) {
		netif_wake_queue(iod->ndev);
		mif_info("%s\n", iod->name);
	}
}

void iodev_netif_stop(struct io_device *iod, void *args)
{
	if (iod->io_typ == IODEV_NET && iod->ndev) {
		netif_stop_queue(iod->ndev);
		mif_info("%s\n", iod->name);
	}
}

static void iodev_set_tx_link(struct io_device *iod, void *args)
{
	struct link_device *ld = (struct link_device *)args;
	if (iod->format == IPC_RAW && IS_CONNECTED(iod, ld)) {
		set_current_link(iod, ld);
		mif_err("%s -> %s\n", iod->name, ld->name);
	}
}

void rawdevs_set_tx_link(struct modem_shared *msd, enum modem_link link_type)
{
	struct link_device *ld = find_linkdev(msd, link_type);
	if (ld)
		iodevs_for_each(msd, iodev_set_tx_link, ld);
}

void mif_netif_stop(struct link_device *ld)
{
	struct io_device *iod;

	iod = link_get_iod_with_channel(ld, RMNET0_CH_ID);
	if (iod)
		iodevs_for_each(iod->msd, iodev_netif_stop, 0);
}

void mif_netif_wake(struct link_device *ld)
{
	struct io_device *iod;

	/**
	 * If ld->suspend_netif_tx is true, this means that there was a SUSPEND
	 * flow control command from CP so MIF must wait for a RESUME command
	 * from CP.
	 */
	if (ld->suspend_netif_tx) {
		mif_info("%s: waiting for FLOW_CTRL_RESUME\n", ld->name);
		return;
	}

	iod = link_get_iod_with_channel(ld, RMNET0_CH_ID);
	if (iod)
		iodevs_for_each(iod->msd, iodev_netif_wake, 0);
}

/**
 * ipv4str_to_be32 - ipv4 string to be32 (big endian 32bits integer)
 * @return: return zero when errors occurred
 */
__be32 ipv4str_to_be32(const char *ipv4str, size_t count)
{
	unsigned char ip[4];
	char ipstr[16]; /* == strlen("xxx.xxx.xxx.xxx") + 1 */
	char *next = ipstr;
	char *p;
	int i;

	memmove(ipstr, ipv4str, ARRAY_SIZE(ipstr));

	for (i = 0; i < 4; i++) {
		p = strsep(&next, ".");
		if (kstrtou8(p, 10, &ip[i]) < 0)
			return 0; /* == 0.0.0.0 */
	}

	return *((__be32 *)ip);
}

void mif_add_timer(struct timer_list *timer, unsigned long expire,
		void (*function)(unsigned long), unsigned long data)
{
	if (timer_pending(timer))
		return;

	init_timer(timer);
	timer->expires = get_jiffies_64() + expire;
	timer->function = function;
	timer->data = data;
	add_timer(timer);
}

/**
 * std_udl_get_cmd
 * @frm: pointer to an SIPC5 link frame
 *
 * Returns the standard BOOT/DUMP (STD_UDL) command in an SIPC5 BOOT/DUMP frame.
 */
u32 std_udl_get_cmd(u8 *frm)
{
	u8 *cmd = frm + EXYNOS_HEADER_SIZE;
	return *((u32 *)cmd);
}

/**
 * std_udl_with_payload
 * @cmd: standard BOOT/DUMP command
 *
 * Returns true if the STD_UDL command has a payload.
 */
bool std_udl_with_payload(u32 cmd)
{
	u32 mask = cmd & STD_UDL_STEP_MASK;
	return (mask && mask < STD_UDL_CRC) ? true : false;
}
void mif_print_data(const char *buff, int len)
{
	int words = len >> 4;
	int residue = len - (words << 4);
	int i;
	char *b;
	char last[80];
	char tb[8];

	/* Make the last line, if ((len % 16) > 0) */
	if (residue > 0) {
		snprintf(last, ARRAY_SIZE(last),  "%04X: ", (words << 4));
		b = (char *)buff + (words << 4);
		for (i = 0; i < residue; i++) {
			snprintf(tb, ARRAY_SIZE(tb), "%02x ", b[i]);
			strncat(last, tb, strlen(tb));
			if ((i & 0x3) == 0x3) {
				snprintf(tb, ARRAY_SIZE(tb), " ");
				strncat(last, tb, ARRAY_SIZE(tb));
			}
		}
	}

	for (i = 0; i < words; i++) {
		b = (char *)buff + (i << 4);
		mif_err("%04X: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			(i << 4),
			b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
			b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
	}

	/* Print the last line */
	if (residue > 0)
		mif_err("%s\n", last);
}

void mif_dump2format16(const char *data, int len, char *buff, char *tag)
{
	char *d;
	int i;
	int words = len >> 4;
	int residue = len - (words << 4);
	char line[LINE_BUFF_SIZE];
	char tb[8];

	for (i = 0; i < words; i++) {
		memset(line, 0, LINE_BUFF_SIZE);
		d = (char *)data + (i << 4);

		if (tag)
			snprintf(line, LINE_BUFF_SIZE,
				"%s%04X| %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				tag, (i << 4),
				d[0], d[1], d[2], d[3],
				d[4], d[5], d[6], d[7],
				d[8], d[9], d[10], d[11],
				d[12], d[13], d[14], d[15]);
		else
			snprintf(line, LINE_BUFF_SIZE,
				"%04X| %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				(i << 4),
				d[0], d[1], d[2], d[3],
				d[4], d[5], d[6], d[7],
				d[8], d[9], d[10], d[11],
				d[12], d[13], d[14], d[15]);

		strncat(buff, line, strlen(line));
	}

	/* Make the last line, if (len % 16) > 0 */
	if (residue > 0) {
		memset(line, 0, LINE_BUFF_SIZE);
		memset(tb, 0, sizeof(tb));
		d = (char *)data + (words << 4);

		if (tag)
			snprintf(line, LINE_BUFF_SIZE, "%s%04X|", tag,
				(words << 4));
		else
			snprintf(line, LINE_BUFF_SIZE, "%04X|", (words << 4));

		for (i = 0; i < residue; i++) {
			snprintf(tb, ARRAY_SIZE(tb), " %02x", d[i]);
			strncat(line, tb, strlen(tb));
			if ((i & 0x3) == 0x3) {
				snprintf(tb, ARRAY_SIZE(tb), " ");
				strncat(line, tb, strlen(tb));
			}
		}
		strncat(line, "\n", strlen("\n"));

		strncat(buff, line, strlen(line));
	}
}

void mif_dump2format4(const char *data, int len, char *buff, char *tag)
{
	char *d;
	int i;
	int words = len >> 2;
	int residue = len - (words << 2);
	char line[LINE_BUFF_SIZE];
	char tb[8];

	for (i = 0; i < words; i++) {
		memset(line, 0, LINE_BUFF_SIZE);
		d = (char *)data + (i << 2);

		if (tag)
			snprintf(line, LINE_BUFF_SIZE,
				"%s%04X| %02x %02x %02x %02x\n",
				tag, (i << 2), d[0], d[1], d[2], d[3]);
		else
			snprintf(line, LINE_BUFF_SIZE,
				"%04X| %02x %02x %02x %02x\n",
				(i << 2), d[0], d[1], d[2], d[3]);

		strncat(buff, line, strlen(line));
	}

	/* Make the last line, if (len % 4) > 0 */
	if (residue > 0) {
		memset(line, 0, LINE_BUFF_SIZE);
		memset(tb, 0, sizeof(tb));
		d = (char *)data + (words << 2);

		if (tag)
			snprintf(line, LINE_BUFF_SIZE, "%s%04X|", tag,
				(words << 2));
		else
			snprintf(line, LINE_BUFF_SIZE, "%04X|", (words << 2));

		for (i = 0; i < residue; i++) {
			snprintf(tb, ARRAY_SIZE(tb), " %02x", d[i]);
			strncat(line, tb, strlen(tb));
		}
		strncat(line, "\n", strlen("\n"));

		strncat(buff, line, strlen(line));
	}
}

void mif_print_dump(const char *data, int len, int width)
{
	char *buff;

	buff = kzalloc(len << 3, GFP_ATOMIC);
	if (!buff) {
		mif_err("ERR! kzalloc fail\n");
		return;
	}

	if (width == 16)
		mif_dump2format16(data, len, buff, LOG_TAG);
	else
		mif_dump2format4(data, len, buff, LOG_TAG);

	pr_info("%s", buff);

	kfree(buff);
}

static void strcat_tcp_header(char *buff, u8 *pkt)
{
	struct tcphdr *tcph = (struct tcphdr *)pkt;
	int eol;
	char line[LINE_BUFF_SIZE] = {0, };
	char flag_str[32] = {0, };

/*-------------------------------------------------------------------------

				TCP Header Format

	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          Source Port          |       Destination Port        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                        Sequence Number                        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                    Acknowledgment Number                      |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|  Data |       |C|E|U|A|P|R|S|F|                               |
	| Offset| Rsvd  |W|C|R|C|S|S|Y|I|            Window             |
	|       |       |R|E|G|K|H|T|N|N|                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|           Checksum            |         Urgent Pointer        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                    Options                    |    Padding    |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                             data                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

-------------------------------------------------------------------------*/

	snprintf(line, LINE_BUFF_SIZE,
		"%s: TCP:: Src.Port %u, Dst.Port %u\n",
		MIF_TAG, ntohs(tcph->source), ntohs(tcph->dest));
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE,
		"%s: TCP:: SEQ 0x%08X(%u), ACK 0x%08X(%u)\n",
		MIF_TAG, ntohs(tcph->seq), ntohs(tcph->seq),
		ntohs(tcph->ack_seq), ntohs(tcph->ack_seq));
	strncat(buff, line, strlen(line));

	if (tcph->cwr)
		strncat(flag_str, "CWR ", strlen("CWR "));
	if (tcph->ece)
		strncat(flag_str, "ECE", strlen("ECE"));
	if (tcph->urg)
		strncat(flag_str, "URG ", strlen("URG "));
	if (tcph->ack)
		strncat(flag_str, "ACK ", strlen("ACK "));
	if (tcph->psh)
		strncat(flag_str, "PSH ", strlen("PSH "));
	if (tcph->rst)
		strncat(flag_str, "RST ", strlen("RST "));
	if (tcph->syn)
		strncat(flag_str, "SYN ", strlen("SYN "));
	if (tcph->fin)
		strncat(flag_str, "FIN ", strlen("FIN "));
	eol = strlen(flag_str) - 1;
	if (eol > 0)
		flag_str[eol] = 0;
	snprintf(line, LINE_BUFF_SIZE, "%s: TCP:: Flags {%s}\n",
		MIF_TAG, flag_str);
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE,
		"%s: TCP:: Window %u, Checksum 0x%04X, Urgent %u\n", MIF_TAG,
		ntohs(tcph->window), ntohs(tcph->check), ntohs(tcph->urg_ptr));
	strncat(buff, line, strlen(line));
}

static void strcat_udp_header(char *buff, u8 *pkt)
{
	struct udphdr *udph = (struct udphdr *)pkt;
	char line[LINE_BUFF_SIZE] = {0, };

/*-------------------------------------------------------------------------

				UDP Header Format

	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          Source Port          |       Destination Port        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|            Length             |           Checksum            |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                             data                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

-------------------------------------------------------------------------*/

	snprintf(line, LINE_BUFF_SIZE,
		"%s: UDP:: Src.Port %u, Dst.Port %u\n",
		MIF_TAG, ntohs(udph->source), ntohs(udph->dest));
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE,
		"%s: UDP:: Length %u, Checksum 0x%04X\n",
		MIF_TAG, ntohs(udph->len), ntohs(udph->check));
	strncat(buff, line, strlen(line));

	if (ntohs(udph->dest) == 53) {
		snprintf(line, LINE_BUFF_SIZE, "%s: UDP:: DNS query!!!\n",
			MIF_TAG);
		strncat(buff, line, strlen(line));
	}

	if (ntohs(udph->source) == 53) {
		snprintf(line, LINE_BUFF_SIZE, "%s: UDP:: DNS response!!!\n",
			MIF_TAG);
		strncat(buff, line, strlen(line));
	}
}

void print_ip4_packet(const u8 *ip_pkt, bool tx)
{
	char *buff;
	struct iphdr *iph = (struct iphdr *)ip_pkt;
	u8 *pkt = (u8 *)ip_pkt + (iph->ihl << 2);
	u16 flags = (ntohs(iph->frag_off) & 0xE000);
	u16 frag_off = (ntohs(iph->frag_off) & 0x1FFF);
	int eol;
	char line[LINE_BUFF_SIZE] = {0, };
	char flag_str[16] = {0, };

/*---------------------------------------------------------------------------
				IPv4 Header Format

	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|Version|  IHL  |Type of Service|          Total Length         |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|         Identification        |C|D|M|     Fragment Offset     |
	|                               |E|F|F|                         |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|  Time to Live |    Protocol   |         Header Checksum       |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                       Source Address                          |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                    Destination Address                        |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                    Options                    |    Padding    |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	IHL - Header Length
	Flags - Consist of 3 bits
		The 1st bit is "Congestion" bit.
		The 2nd bit is "Dont Fragment" bit.
		The 3rd bit is "More Fragments" bit.

---------------------------------------------------------------------------*/

	if (iph->version != 4)
		return;

	buff = kzalloc(4096, GFP_ATOMIC);
	if (!buff)
		return;

	if (tx)
		snprintf(line, LINE_BUFF_SIZE, "\n%s\n", TX_SEPARATOR);
	else
		snprintf(line, LINE_BUFF_SIZE, "\n%s\n", RX_SEPARATOR);
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE, "%s\n", LINE_SEPARATOR);
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE,
		"%s: IP4:: Version %u, Header Length %u, TOS %u, Length %u\n",
		MIF_TAG, iph->version, (iph->ihl << 2), iph->tos,
		ntohs(iph->tot_len));
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE, "%s: IP4:: ID %u, Fragment Offset %u\n",
		MIF_TAG, ntohs(iph->id), frag_off);
	strncat(buff, line, strlen(line));

	if (flags & IP_CE)
		strncat(flag_str, "CE ", strlen("CE "));
	if (flags & IP_DF)
		strncat(flag_str, "DF ", strlen("DF "));
	if (flags & IP_MF)
		strncat(flag_str, "MF ", strlen("MF "));
	eol = strlen(flag_str) - 1;
	if (eol > 0)
		flag_str[eol] = 0;
	snprintf(line, LINE_BUFF_SIZE, "%s: IP4:: Flags {%s}\n",
		MIF_TAG, flag_str);
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE,
		"%s: IP4:: TTL %u, Protocol %u, Header Checksum 0x%04X\n",
		MIF_TAG, iph->ttl, iph->protocol, ntohs(iph->check));
	strncat(buff, line, strlen(line));

	snprintf(line, LINE_BUFF_SIZE,
		"%s: IP4:: Src.IP %u.%u.%u.%u, Dst.IP %u.%u.%u.%u\n",
		MIF_TAG, ip_pkt[12], ip_pkt[13], ip_pkt[14], ip_pkt[15],
		ip_pkt[16], ip_pkt[17], ip_pkt[18], ip_pkt[19]);
	strncat(buff, line, strlen(line));

	switch (iph->protocol) {
	case 6: /* TCP */
		strcat_tcp_header(buff, pkt);
		break;

	case 17: /* UDP */
		strcat_udp_header(buff, pkt);
		break;

	default:
		break;
	}

	snprintf(line, LINE_BUFF_SIZE, "%s\n", LINE_SEPARATOR);
	strncat(buff, line, strlen(line));

	pr_info("%s", buff);

	kfree(buff);
}

bool is_dns_packet(const u8 *ip_pkt)
{
	struct iphdr *iph = (struct iphdr *)ip_pkt;
	struct udphdr *udph = (struct udphdr *)(ip_pkt + (iph->ihl << 2));

	/* If this packet is not a UDP packet, return here. */
	if (iph->protocol != 17)
		return false;

	if (ntohs(udph->dest) == 53 || ntohs(udph->source) == 53)
		return true;
	else
		return false;
}

bool is_syn_packet(const u8 *ip_pkt)
{
	struct iphdr *iph = (struct iphdr *)ip_pkt;
	struct tcphdr *tcph = (struct tcphdr *)(ip_pkt + (iph->ihl << 2));

	/* If this packet is not a TCP packet, return here. */
	if (iph->protocol != 6)
		return false;

	if (tcph->syn || tcph->fin)
		return true;
	else
		return false;
}

void mif_init_irq(struct modem_irq *irq, unsigned int num, const char *name,
		  unsigned long flags)
{
	spin_lock_init(&irq->lock);
	irq->num = num;
	memmove(irq->name, name, MIF_MAX_NAME_LEN);
	irq->flags = flags;
	mif_err("name:%s num:%d flags:0x%08lX\n", name, num, flags);
}

int mif_request_irq(struct modem_irq *irq, irq_handler_t isr, void *data)
{
	int ret;

	ret = request_irq(irq->num, isr, irq->flags, irq->name, data);
	if (ret) {
		mif_err("%s: ERR! request_irq fail (%d)\n", irq->name, ret);
		return ret;
	}

	enable_irq_wake(irq->num);

	irq->active = true;

	mif_err("%s(#%d) handler registered\n", irq->name, irq->num);

	return 0;
}

void mif_enable_irq(struct modem_irq *irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq->lock, flags);

	if (irq->active)
		goto exit;

	enable_irq(irq->num);
	irq->active = true;

exit:
	spin_unlock_irqrestore(&irq->lock, flags);
}

void mif_disable_irq(struct modem_irq *irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq->lock, flags);

	if (!irq->active)
		goto exit;

	disable_irq_nosync(irq->num);
	irq->active = false;

exit:
	spin_unlock_irqrestore(&irq->lock, flags);
}

int mif_test_dpram(char *dp_name, void __iomem *start, u16 bytes)
{
	u16 i;
	u16 words = bytes >> 1;
	u16 __iomem *dst = (u16 __iomem *)start;
	u16 val;
	int err_cnt = 0;

	mif_err("%s: start 0x%p, bytes %d\n", dp_name, start, bytes);

	mif_err("%s: 0/6 stage ...\n", dp_name);
	for (i = 1; i <= 100; i++) {
		iowrite16(0x1234, dst);
		val = ioread16(dst);
		if (val != 0x1234) {
			mif_err("%s: [0x0000] read 0x%04X != written 0x1234 (try# %d)\n",
				dp_name, val, i);
			err_cnt++;
		}
	}

	if (err_cnt > 0) {
		mif_err("%s: FAIL!!!\n", dp_name);
		return -EINVAL;
	}

	mif_err("%s: 1/6 stage ...\n", dp_name);
	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16(0, dst);
		dst++;
	}

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		val = ioread16(dst);
		if (val != 0x0000) {
			mif_err("%s: ERR! [0x%04X] read 0x%04X != written 0x0000\n",
				dp_name, i, val);
			err_cnt++;
		}
		dst++;
	}

	if (err_cnt > 0) {
		mif_err("%s: FAIL!!!\n", dp_name);
		return -EINVAL;
	}

	mif_err("%s: 2/6 stage ...\n", dp_name);
	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16(0xFFFF, dst);
		dst++;
	}

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		val = ioread16(dst);
		if (val != 0xFFFF) {
			mif_err("%s: ERR! [0x%04X] read 0x%04X != written 0xFFFF\n",
				dp_name, i, val);
			err_cnt++;
		}
		dst++;
	}

	if (err_cnt > 0) {
		mif_err("%s: FAIL!!!\n", dp_name);
		return -EINVAL;
	}

	mif_err("%s: 3/6 stage ...\n", dp_name);
	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16(0x00FF, dst);
		dst++;
	}

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		val = ioread16(dst);
		if (val != 0x00FF) {
			mif_err("%s: ERR! [0x%04X] read 0x%04X != written 0x00FF\n",
				dp_name, i, val);
			err_cnt++;
		}
		dst++;
	}

	if (err_cnt > 0) {
		mif_err("%s: FAIL!!!\n", dp_name);
		return -EINVAL;
	}

	mif_err("%s: 4/6 stage ...\n", dp_name);
	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16(0x0FF0, dst);
		dst++;
	}

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		val = ioread16(dst);
		if (val != 0x0FF0) {
			mif_err("%s: ERR! [0x%04X] read 0x%04X != written 0x0FF0\n",
				dp_name, i, val);
			err_cnt++;
		}
		dst++;
	}

	if (err_cnt > 0) {
		mif_err("%s: FAIL!!!\n", dp_name);
		return -EINVAL;
	}

	mif_err("%s: 5/6 stage ...\n", dp_name);
	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16(0xFF00, dst);
		dst++;
	}

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		val = ioread16(dst);
		if (val != 0xFF00) {
			mif_err("%s: ERR! [0x%04X] read 0x%04X != written 0xFF00\n",
				dp_name, i, val);
			err_cnt++;
		}
		dst++;
	}

	mif_err("%s: 6/6 stage ...\n", dp_name);
	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16((i & 0xFFFF), dst);
		dst++;
	}

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		val = ioread16(dst);
		if (val != (i & 0xFFFF)) {
			mif_err("%s: ERR! [0x%04X] read 0x%04X != written 0x%04X\n",
				dp_name, i, val, (i & 0xFFFF));
			err_cnt++;
		}
		dst++;
	}

	if (err_cnt > 0) {
		mif_err("%s: FAIL!!!\n", dp_name);
		return -EINVAL;
	}

	mif_err("%s: PASS!!!\n", dp_name);

	dst = (u16 __iomem *)start;
	for (i = 0; i < words; i++) {
		iowrite16(0, dst);
		dst++;
	}

	return 0;
}

struct file *mif_open_file(const char *path)
{
	struct file *fp;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(get_ds());

	fp = filp_open(path, O_RDWR|O_CREAT|O_APPEND, 0666);

	set_fs(old_fs);

	if (IS_ERR(fp))
		return NULL;

	return fp;
}

void mif_save_file(struct file *fp, const char *buff, size_t size)
{
	int ret;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(get_ds());

	ret = fp->f_op->write(fp, buff, size, &fp->f_pos);
	if (ret < 0)
		mif_err("ERR! write fail\n");

	set_fs(old_fs);
}

void mif_close_file(struct file *fp)
{
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(get_ds());

	filp_close(fp, NULL);

	set_fs(old_fs);
}

