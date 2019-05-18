/*
 * Interrupt support for Cirrus Logic CS47L92
 *
 * Copyright 2016 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/registers.h>

#include "irq-madera.h"

static const struct regmap_irq cs47l92_irqs[MADERA_NUM_IRQ] = {
	[MADERA_IRQ_BOOT_DONE] = { .reg_offset = 0,
				  .mask = MADERA_BOOT_DONE_EINT1 },
	[MADERA_IRQ_CTRLIF_ERR] = { .reg_offset = 0,
				  .mask = MADERA_CTRLIF_ERR_EINT1 },

	[MADERA_IRQ_FLL1_LOCK] = { .reg_offset = 1,
				  .mask = MADERA_FLL1_LOCK_EINT1 },
	[MADERA_IRQ_FLL2_LOCK] = { .reg_offset = 1,
				  .mask = MADERA_FLL2_LOCK_EINT1},

	[MADERA_IRQ_MICDET1] = { .reg_offset = 5,
				  .mask = MADERA_MICDET1_EINT1 },
	[MADERA_IRQ_MICDET2] = { .reg_offset = 5,
				.mask = MADERA_MICDET2_EINT1 },
	[MADERA_IRQ_HPDET] = { .reg_offset = 5,
				  .mask = MADERA_HPDET_EINT1},

	[MADERA_IRQ_MICD_CLAMP_RISE] = { .reg_offset = 6,
				  .mask = MADERA_MICD_CLAMP_RISE_EINT1 },
	[MADERA_IRQ_MICD_CLAMP_FALL] = { .reg_offset = 6,
				  .mask = MADERA_MICD_CLAMP_FALL_EINT1 },
	[MADERA_IRQ_JD1_FALL] = { .reg_offset = 6,
				  .mask = MADERA_JD1_FALL_EINT1 },
	[MADERA_IRQ_JD1_RISE] = { .reg_offset = 6,
				  .mask = MADERA_JD1_RISE_EINT1 },

	[MADERA_IRQ_ASRC1_IN1_LOCK] = { .reg_offset = 8,
				  .mask = MADERA_ASRC1_IN1_LOCK_EINT1 },
	[MADERA_IRQ_ASRC1_IN2_LOCK] = { .reg_offset = 8,
				  .mask = MADERA_ASRC1_IN2_LOCK_EINT1 },
	[MADERA_IRQ_DRC2_SIG_DET] = { .reg_offset = 8,
				  .mask = MADERA_DRC2_SIG_DET_EINT1 },
	[MADERA_IRQ_DRC1_SIG_DET] = { .reg_offset = 8,
				  .mask = MADERA_DRC1_SIG_DET_EINT1 },

	[MADERA_IRQ_DSP_IRQ1] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ1_EINT1},
	[MADERA_IRQ_DSP_IRQ2] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ2_EINT1},
	[MADERA_IRQ_DSP_IRQ3] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ3_EINT1},
	[MADERA_IRQ_DSP_IRQ4] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ4_EINT1},
	[MADERA_IRQ_DSP_IRQ5] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ5_EINT1},
	[MADERA_IRQ_DSP_IRQ6] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ6_EINT1},
	[MADERA_IRQ_DSP_IRQ7] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ7_EINT1},
	[MADERA_IRQ_DSP_IRQ8] = { .reg_offset = 10,
				  .mask = MADERA_DSP_IRQ8_EINT1},

	[MADERA_IRQ_DSP1_BUS_ERROR] = { .reg_offset = 32,
				  .mask = MADERA_ADSP_ERROR_STATUS_DSP1},
};

const struct regmap_irq_chip cs47l92_irq = {
	.name = "cs47l92 IRQ",
	.status_base = MADERA_IRQ1_STATUS_1,
	.mask_base = MADERA_IRQ1_MASK_1,
	.ack_base = MADERA_IRQ1_STATUS_1,
	.num_regs = 33,
	.irqs = cs47l92_irqs,
	.num_irqs = ARRAY_SIZE(cs47l92_irqs),
};
EXPORT_SYMBOL_GPL(cs47l92_irq);
