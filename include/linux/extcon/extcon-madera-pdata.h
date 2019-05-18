/*
 * Platform data for Madera codecs
 *
 * Copyright 2016 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXTCON_MADERA_PDATA_H
#define EXTCON_MADERA_PDATA_H

#include <linux/kernel.h>

#define MADERA_MAX_ACCESSORY		1

/* Treat INT_MAX impedance as open circuit */
#define MADERA_HP_Z_OPEN		INT_MAX

struct madera_jd_state;

/** Bias and sense configuration for probing MICDET */
struct madera_micd_config {
	/** cs47l35, cs47l85, WM1840: value of the ACCDET_SRC field
	 *  other codecs: value of MICDn_SENSE_SEL field
	 * See datasheet for field values
	 */
	unsigned int src;

	/** cs47l35, cs47l85, wm1840: unused
	 *  other codecs: value of MICDn_GND_SEL
	 *
	 * See datasheet for field values
	 */
	unsigned int gnd;

	/** MICBIAS number to enable */
	unsigned int bias;

	/** State of polarity gpio during probe (true == gpio asserted) */
	bool gpio;

	/** cs47l35, cs47l85, wm1840: unused
	 *  other codecs: value of HPn_GND_SEL
	 *
	 * See datasheet for field values
	 */
	unsigned int hp_gnd;

};

struct madera_micd_range {
	int max;  /** Ohms */
	int key;  /** Key to report to input layer */
};

/** pdata sub-structure for accessory detect configuration */
struct madera_accdet_pdata {
	/** set to true to enable this accessory detect */
	bool enabled;

	/** Output this accdet pdata refers */
	unsigned int output;

	/** Time in milliseconds to keep wake lock during jack detection */
	int jd_wake_time;

	/** Set whether JD2 is used for jack detection */
	bool jd_use_jd2;

	/** set to true if jackdet contact opens on insert */
	bool jd_invert;

	/** If non-zero don't run headphone detection, just report this value
	 * Specified as hundredths-of-an-ohm, that is (ohms * 100)
	 */
	int fixed_hpdet_imp_x100;

	/** Impedance of external series resistor on hpdet.
	 * Specified as hundredths-of-an-ohm, that is (ohms * 100)
	 */
	int hpdet_ext_res_x100;

	/** If non-zero, specifies the maximum impedance in ohms
	 * that will be considered as a short circuit.
	 */
	int hpdet_short_circuit_imp;

	/**
	 * Channel to use for headphone detection, valid values are 0 for
	 * left and 1 for right
	 */
	int hpdet_channel;

	/** Extra debounce timeout during initial mic detect (milliseconds) */
	int micd_detect_debounce_ms;

	/** Extra software debounces during button detection */
	int micd_manual_debounce;

	/** GPIO for mic detection polarity */
	int micd_pol_gpio;

	/** Mic detect ramp rate */
	int micd_bias_start_time;

	/** Setting for the codec MICD_RATE field
	 * See the  datasheet for documentation of the field values
	 */
	int micd_rate;

	/** Mic detect debounce level */
	int micd_dbtime;

	/** Mic detect timeout (milliseconds) */
	int micd_timeout_ms;

	/** Mic detect clamp function */
	unsigned int micd_clamp_mode;

	/** Force MICBIAS on for mic detect */
	bool micd_force_micbias;

	/** Declare an open circuit as a 4 pole jack */
	bool micd_open_circuit_declare;

	/** Use software comparison to determine mic presence */
	bool micd_software_compare;

	/** Mic detect level parameters */
	const struct madera_micd_range *micd_ranges;
	int num_micd_ranges;


	/** Headset polarity configurations */
	const struct madera_micd_config *micd_configs;
	int num_micd_configs;

	/**
	 * 4 entries:
	 * [HPL_clamp_pin, HPL_impedance_measurement_pin,
	 * HPR_clamp_pin, HPR_impedance_measurement_pin]
	 *
	 * 0 = use default setting
	 * values 0x1..0xffff = set to this value
	 * >0xffff = set to 0
	 */
	unsigned int hpd_pins[4];

	/** Moisture detection pin */
	int moisture_pin;
	/** Moisture detection impedance threshold */
	int moisture_imp;
	/** Number of debounces to perform on moisture detection */
	int moisture_debounce;

	/** Delay between jack detection and any futher action */
	int init_delay;

	/**
	 * Specify an input to mute during headset button presses:
	 * 1 - IN1L, 2 - IN1R, ..., n - IN[n]R
	 */
	u32 hs_mic;

	/** Override the normal jack detection */
	const struct madera_jd_state *custom_jd;
};

#endif
