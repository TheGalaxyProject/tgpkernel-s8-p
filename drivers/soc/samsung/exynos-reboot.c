/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * Exynos - Support SoC specific Reboot
 * Author: Hosung Kim <hosung0.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/soc/samsung/exynos-soc.h>
#include <soc/samsung/acpm_ipc_ctrl.h>

#ifdef CONFIG_SEC_DEBUG
#include <linux/sec_debug.h>
#endif

extern void (*arm_pm_restart)(enum reboot_mode reboot_mode, const char *cmd);
static void __iomem *exynos_pmu_base = NULL;

static const char * const mngs_cores[] = {
	"arm,mongoose",
	NULL,
};

static bool is_mngs_cpu(struct device_node *cn)
{
	const char * const *lc;
	for (lc = mngs_cores; *lc; lc++)
		if (of_device_is_compatible(cn, *lc))
			return true;
	return false;
}

int soc_has_mongoose(void)
{
	struct device_node *cn = NULL;
	u32 mngs_cpu_cnt = 0;

	/* find arm,mongoose compatable in device tree */
	while ((cn = of_find_node_by_type(cn, "cpu"))) {
		if (is_mngs_cpu(cn))
			mngs_cpu_cnt++;
	}
	return mngs_cpu_cnt;
}

/* defines for MNGS reset */
#define PEND_MNGS				(1 << 1)
#define PEND_APOLLO				(1 << 0)
#define DEFAULT_VAL_CPU_RESET_DISABLE		(0xFFFFFFFC)
#define RESET_DISABLE_GPR_CPUPORESET		(1 << 15)
#define RESET_DISABLE_WDT_CPUPORESET		(1 << 12)
#define RESET_DISABLE_CORERESET			(1 << 9)
#define RESET_DISABLE_CPUPORESET		(1 << 8)
#define RESET_DISABLE_WDT_PRESET_DBG		(1 << 25)
#define RESET_DISABLE_PRESET_DBG		(1 << 18)
#define DFD_EDPCSR_DUMP_EN			(1 << 0)
#define RESET_DISABLE_L2RESET			(1 << 16)
#define RESET_DISABLE_WDT_L2RESET		(1 << 31)

#define EXYNOS_PMU_CPU_RESET_DISABLE_FROM_SOFTRESET	(0x041C)
#define EXYNOS_PMU_CPU_RESET_DISABLE_FROM_WDTRESET	(0x0414)
#define EXYNOS_PMU_ATLAS_CPU0_RESET			(0x200C)
#define EXYNOS_PMU_ATLAS_DBG_RESET			(0x244C)
#define EXYNOS_PMU_ATLAS_NONCPU_RESET			(0x240C)
#define EXYNOS_PMU_SWRESET				(0x0400)
#define EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION	(0x0500)
#define EXYNOS_PMU_PS_HOLD_CONTROL			(0x330C)

#define DFD_EDPCSR_DUMP_EN			(1 << 0)
#define DFD_L2RSTDISABLE_MNGS_EN		(1 << 11)
#define DFD_DBGL1RSTDISABLE_MNGS_EN		(1 << 10)
#define DFD_L2RSTDISABLE_APOLLO_EN		(1 << 9)
#define DFD_DBGL1RSTDISABLE_APOLLO_EN		(1 << 8)
#define DFD_CLEAR_L2RSTDISABLE_MNGS		(1 << 7)
#define DFD_CLEAR_DBGL1RSTDISABLE_MNGS		(1 << 6)
#define DFD_CLEAR_L2RSTDISABLE_APOLLO		(1 << 5)
#define DFD_CLEAR_DBGL1RSTDISABLE_APOLLO	(1 << 4)

static void dfd_set_dump_gpr(int en)
{
	u32 reg_val;

	if (en) {
		reg_val = DFD_EDPCSR_DUMP_EN
			| DFD_L2RSTDISABLE_MNGS_EN | DFD_DBGL1RSTDISABLE_MNGS_EN
			| DFD_L2RSTDISABLE_APOLLO_EN | DFD_DBGL1RSTDISABLE_APOLLO_EN;
		writel(reg_val, exynos_pmu_base + EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION);
	} else {
		reg_val = readl(exynos_pmu_base + EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION);
		if (reg_val) {
			reg_val =
				DFD_CLEAR_L2RSTDISABLE_MNGS | DFD_CLEAR_DBGL1RSTDISABLE_MNGS |
				DFD_CLEAR_L2RSTDISABLE_APOLLO | DFD_CLEAR_DBGL1RSTDISABLE_APOLLO;
#ifdef CONFIG_SEC_DEBUG
			if ((sec_debug_get_debug_level() & 0x1) == 0x1) {
				pr_info("Enable DumpGPR for reboot lockup\n");
				reg_val |= DFD_EDPCSR_DUMP_EN;
			}
#endif
		}
		writel(reg_val, exynos_pmu_base + EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION);
	}
}

void mngs_reset_control(int en)
{
	u32 reg_val, val;
	u32 mngs_cpu_cnt = soc_has_mongoose();
	u32 check_dumpGPR;

	if (mngs_cpu_cnt == 0 || !exynos_pmu_base)
		return;

	check_dumpGPR = DFD_EDPCSR_DUMP_EN &
		readl(exynos_pmu_base + EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION);

	if (!check_dumpGPR)
		return;

	if (en) {
		/* reset disable for MNGS */
		reg_val = readl(exynos_pmu_base + EXYNOS_PMU_CPU_RESET_DISABLE_FROM_SOFTRESET);
		if (reg_val & (PEND_MNGS | PEND_APOLLO)) {
			reg_val &= ~(PEND_MNGS | PEND_APOLLO);
			writel(reg_val, exynos_pmu_base + EXYNOS_PMU_CPU_RESET_DISABLE_FROM_SOFTRESET);
		}

		reg_val = readl(exynos_pmu_base + EXYNOS_PMU_CPU_RESET_DISABLE_FROM_WDTRESET);
		if (reg_val != DEFAULT_VAL_CPU_RESET_DISABLE) {
			reg_val &= ~(PEND_MNGS | PEND_APOLLO);
			writel(reg_val, exynos_pmu_base + EXYNOS_PMU_CPU_RESET_DISABLE_FROM_WDTRESET);
		}

		for (val = 0; val < mngs_cpu_cnt; val++) {
			reg_val = readl(exynos_pmu_base + EXYNOS_PMU_ATLAS_CPU0_RESET + (val * 0x80));
			reg_val |= (RESET_DISABLE_WDT_CPUPORESET
					| RESET_DISABLE_CORERESET | RESET_DISABLE_CPUPORESET);
			writel(reg_val, exynos_pmu_base + EXYNOS_PMU_ATLAS_CPU0_RESET + (val * 0x80));
		}

		reg_val = readl(exynos_pmu_base + EXYNOS_PMU_ATLAS_DBG_RESET);
		reg_val |= (RESET_DISABLE_WDT_PRESET_DBG | RESET_DISABLE_PRESET_DBG);
		writel(reg_val, exynos_pmu_base + EXYNOS_PMU_ATLAS_DBG_RESET);

                reg_val = readl(exynos_pmu_base + EXYNOS_PMU_ATLAS_NONCPU_RESET);
                reg_val |= (RESET_DISABLE_L2RESET | RESET_DISABLE_WDT_L2RESET);
                writel(reg_val, exynos_pmu_base + EXYNOS_PMU_ATLAS_NONCPU_RESET);
	} else {
		/* reset enable for MNGS */
		for (val = 0; val < mngs_cpu_cnt; val++) {
			reg_val = readl(exynos_pmu_base + EXYNOS_PMU_ATLAS_CPU0_RESET + (val * 0x80));
			reg_val &= ~(RESET_DISABLE_WDT_CPUPORESET
					| RESET_DISABLE_CORERESET | RESET_DISABLE_CPUPORESET);
			writel(reg_val, exynos_pmu_base + EXYNOS_PMU_ATLAS_CPU0_RESET + (val * 0x80));
		}

		reg_val = readl(exynos_pmu_base + EXYNOS_PMU_ATLAS_DBG_RESET);
		reg_val &= ~(RESET_DISABLE_WDT_PRESET_DBG | RESET_DISABLE_PRESET_DBG);
		writel(reg_val, exynos_pmu_base + EXYNOS_PMU_ATLAS_DBG_RESET);

                reg_val = readl(exynos_pmu_base + EXYNOS_PMU_ATLAS_NONCPU_RESET);
                reg_val &= ~(RESET_DISABLE_L2RESET);
                writel(reg_val, exynos_pmu_base + EXYNOS_PMU_ATLAS_NONCPU_RESET);
	}
}

#define INFORM_NONE		0x0
#define INFORM_RAMDUMP		0xd
#define INFORM_RECOVERY		0xf

#if !defined(CONFIG_SEC_REBOOT)
#ifdef CONFIG_OF
static void exynos_power_off(void)
{
	exynos_acpm_reboot();

	pr_emerg("%s: Set PS_HOLD Low.\n", __func__);
	writel(readl(exynos_pmu_base + EXYNOS_PMU_PS_HOLD_CONTROL) & 0xFFFFFEFF,
				exynos_pmu_base + EXYNOS_PMU_PS_HOLD_CONTROL);
}
#else
static void exynos_power_off(void)
{
	pr_info("Exynos power off does not support.\n");
}
#endif
#endif

static void exynos_reboot(enum reboot_mode mode, const char *cmd)
{
	u32 restart_inform, soc_id;

	if (!exynos_pmu_base)
		return;

	exynos_acpm_reboot();

	restart_inform = INFORM_NONE;

	if (cmd) {
		if (!strcmp((char *)cmd, "recovery"))
			restart_inform = INFORM_RECOVERY;
		else if(!strcmp((char *)cmd, "ramdump"))
			restart_inform = INFORM_RAMDUMP;
	}

	/* Check by each SoC */
	soc_id = exynos_soc_info.product_id & EXYNOS_SOC_MASK;
	switch(soc_id) {
	case EXYNOS8890_SOC_ID:
	case EXYNOS8895_SOC_ID:
		/* Check reset_sequencer_configuration register */
		if (readl(exynos_pmu_base + EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION) & DFD_EDPCSR_DUMP_EN) {
			mngs_reset_control(0);
			dfd_set_dump_gpr(0);
		}
		break;
	default:
		break;
	}

	/* Do S/W Reset */
	pr_emerg("%s: Exynos SoC reset right now\n", __func__);
	__raw_writel(0x1, exynos_pmu_base + EXYNOS_PMU_SWRESET);
}

static int __init exynos_reboot_setup(struct device_node *np)
{
	int err = 0;
	u32 id;

	if (!of_property_read_u32(np, "pmu_base", &id)) {
		exynos_pmu_base = ioremap(id, SZ_16K);
		if (!exynos_pmu_base) {
			pr_err("%s: failed to map to exynos-pmu-base address 0x%x\n",
				__func__, id);
			err = -ENOMEM;
		}
	}

	of_node_put(np);
	return err;
}

static const struct of_device_id reboot_of_match[] __initconst = {
	{ .compatible = "exynos,reboot", .data = exynos_reboot_setup},
	{},
};

typedef int (*reboot_initcall_t)(const struct device_node *);
static int __init exynos_reboot_init(void)
{
	struct device_node *np;
	const struct of_device_id *matched_np;
	reboot_initcall_t init_fn;

	np = of_find_matching_node_and_match(NULL, reboot_of_match, &matched_np);
	if (!np)
		return -ENODEV;

	arm_pm_restart = exynos_reboot;
#if !defined(CONFIG_SEC_REBOOT)
	pm_power_off = exynos_power_off;
#endif
	init_fn = (reboot_initcall_t)matched_np->data;

	return init_fn(np);
}
subsys_initcall(exynos_reboot_init);
