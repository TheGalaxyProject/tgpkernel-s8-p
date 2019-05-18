/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <asm/neon.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <exynos-fimc-is-sensor.h>
#include "fimc-is-hw.h"
#include "fimc-is-core.h"
#include "fimc-is-device-sensor.h"
#include "fimc-is-resourcemgr.h"
#include "fimc-is-dt.h"
#include "fimc-is-device-module-base.h"
#include "interface/fimc-is-interface-library.h"

int sensor_module_init(struct v4l2_subdev *subdev, u32 val)
{
	int ret = 0;
	struct fimc_is_module_enum *module;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;
	struct exynos_platform_fimc_is_module *pdata = NULL;
	struct v4l2_subdev *subdev_cis = NULL;
	struct v4l2_subdev *subdev_actuator = NULL;
	struct v4l2_subdev *subdev_flash = NULL;
	struct v4l2_subdev *subdev_ois = NULL;
	struct fimc_is_preprocessor *preprocessor = NULL;
	struct v4l2_subdev *subdev_preprocessor = NULL;
#ifndef DISABLE_LIB
	register_sensor_interface register_sensor_itf =
		(register_sensor_interface)SENSOR_REGISTER_FUNC_ADDR;
#ifdef CONFIG_COMPANION_DIRECT_USE
#ifdef USE_RTA_BINARY
	register_sensor_interface register_preprocessor_itf =
		(register_sensor_interface)PREPROC_REGISTER_FUNC_ADDR;
#endif
#endif
#endif

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	BUG_ON(!module);

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	BUG_ON(!sensor_peri);

	subdev_preprocessor = sensor_peri->subdev_preprocessor;
	if (subdev_preprocessor){
		preprocessor = (struct fimc_is_preprocessor *)v4l2_get_subdevdata(subdev_preprocessor);
		BUG_ON(!preprocessor);
	}
	memset(&sensor_peri->sensor_interface, 0, sizeof(struct fimc_is_sensor_interface));
#ifdef CONFIG_COMPANION_DIRECT_USE
	memset(&sensor_peri->preprocessor_inferface, 0, sizeof(struct fimc_is_preprocessor_interface));
#endif

	if (sensor_peri->actuator) {
		memset(&sensor_peri->actuator->pre_position, 0, sizeof(u32 [EXPECT_DM_NUM]));
		memset(&sensor_peri->actuator->pre_frame_cnt, 0, sizeof(u32 [EXPECT_DM_NUM]));
	}

	ret = init_sensor_interface(&sensor_peri->sensor_interface);
	if (ret) {
		err("failed in init_sensor_interface, return: %d", ret);
		goto p_err;
	}

	subdev_cis = sensor_peri->subdev_cis;
	BUG_ON(!subdev_cis);

	pdata = module->pdata;
	BUG_ON(!pdata);

#ifdef CONFIG_COMPANION_DIRECT_USE
	ret = init_preprocessor_interface(&sensor_peri->preprocessor_inferface);
	if (ret) {
		err("failed in init_preprocessor_interface, return: %d", ret);
		goto p_err;
	}
#endif
	/* wait preproc s_input before init sensor mode change */
	if (subdev_preprocessor) {
		ret = CALL_PREPROPOPS(preprocessor, preprocessor_wait_s_input, subdev_preprocessor);
		if (ret) {
			err("[SEN:%d] preprocessor wait s_input timeout\n", module->sensor_id);
			goto p_err;
		}
	}

#if !defined(DISABLE_LIB)
	register_sensor_itf = (register_sensor_interface)SENSOR_REGISTER_FUNC_ADDR;
#ifdef ENABLE_FPSIMD_FOR_USER
	fpsimd_get();
	ret = register_sensor_itf((void *)&sensor_peri->sensor_interface);
	fpsimd_put();
#else
	ret = register_sensor_itf((void *)&sensor_peri->sensor_interface);
#endif
	if (ret < 0) {
		goto p_err;
	}

#ifdef USE_RTA_BINARY
	register_sensor_itf = (register_sensor_interface)SENSOR_REGISTER_FUNC_ADDR_RTA;
#ifdef ENABLE_FPSIMD_FOR_USER
	fpsimd_get();
	ret = register_sensor_itf((void *)&sensor_peri->sensor_interface);
	fpsimd_put();
#else
	ret = register_sensor_itf((void *)&sensor_peri->sensor_interface);
#endif
	if (ret < 0) {
		goto p_err;
	}

#ifdef CONFIG_COMPANION_DIRECT_USE
	if (test_bit(FIMC_IS_SENSOR_PREPROCESSOR_AVAILABLE, &sensor_peri->peri_state) &&
			pdata->preprocessor_product_name != PREPROCESSOR_NAME_NOTHING) {
		dbg("%s(%d): call register_preprocessor_itf \n", __func__, __LINE__);

		register_preprocessor_itf = (register_sensor_interface)PREPROC_REGISTER_FUNC_ADDR;
#ifdef ENABLE_FPSIMD_FOR_USER
		fpsimd_get();
		ret = register_preprocessor_itf((void *)&sensor_peri->preprocessor_inferface);
		fpsimd_put();
#else
		ret = register_preprocessor_itf((void *)&sensor_peri->preprocessor_inferface);
#endif
		if (ret < 0) {
			goto p_err;
		}
	}
#endif
#endif
#endif

	/* init kthread for sensor register setting when s_format */
	ret = fimc_is_sensor_init_mode_change_thread(sensor_peri);
	if (ret) {
		err("fimc_is_sensor_init_mode_change is fail(%d)\n", ret);
		goto p_err;
	}

	ret = CALL_CISOPS(&sensor_peri->cis, cis_init, subdev_cis);
	if (ret) {
		err("v4l2_subdev_call(init) is fail(%d)", ret);
		goto p_err;
	}

#ifdef USE_FACE_UNLOCK_AE_AWB_INIT
	ret = CALL_CISOPS(&sensor_peri->cis, cis_set_initial_exposure, subdev_cis);
	if (ret) {
		err("v4l2_subdev_call(set_initial_exposure) is fail(%d)", ret);
		goto p_err;
	}
#endif

	subdev_flash = sensor_peri->subdev_flash;
	if (subdev_flash != NULL) {
		ret = v4l2_subdev_call(subdev_flash, core, init, 0);
		if (ret) {
			err("v4l2_subdev_call(init) is fail(%d)", ret);
			goto p_err;
		}
	}

	subdev_ois = sensor_peri->subdev_ois;
#if defined(CONFIG_OIS_DIRECT_FW_CONTROL)
	if (subdev_ois != NULL) {
		ret = CALL_OISOPS(sensor_peri->ois, ois_fw_update, subdev_ois);
		if (ret < 0) {
			err("v4l2_subdev_call(ois_fw_update) is fail(%d)", ret);
			return ret;
		}
	}
#endif
	if (subdev_ois != NULL) {
		ret = CALL_OISOPS(sensor_peri->ois, ois_init, sensor_peri->subdev_ois);
		if (ret < 0) {
			err("v4l2_subdev_call(ois_init) is fail(%d)", ret);
			return ret;
		}
	}

	if (test_bit(FIMC_IS_SENSOR_ACTUATOR_AVAILABLE, &sensor_peri->peri_state) &&
			pdata->af_product_name != ACTUATOR_NAME_NOTHING && sensor_peri->actuator != NULL) {

		sensor_peri->actuator->actuator_data.actuator_init = true;
		sensor_peri->actuator->actuator_index = -1;
		sensor_peri->actuator->left_x = 0;
		sensor_peri->actuator->left_y = 0;
		sensor_peri->actuator->right_x = 0;
		sensor_peri->actuator->right_y = 0;

		sensor_peri->actuator->actuator_data.afwindow_timer.function = fimc_is_actuator_m2m_af_set;

		subdev_actuator = sensor_peri->subdev_actuator;
		BUG_ON(!subdev_actuator);

		if (!sensor_peri->reuse_3a_value)
			sensor_peri->actuator->position = 0;

		ret = v4l2_subdev_call(subdev_actuator, core, init, 0);
		if (ret) {
			err("v4l2_actuator_call(init) is fail(%d)", ret);
			goto p_err;
		}
	}

#ifdef CONFIG_COMPANION_DIRECT_USE
	if (test_bit(FIMC_IS_SENSOR_PREPROCESSOR_AVAILABLE, &sensor_peri->peri_state) &&
			pdata->preprocessor_product_name != PREPROCESSOR_NAME_NOTHING) {

		subdev_preprocessor = sensor_peri->subdev_preprocessor;
		BUG_ON(!subdev_preprocessor);

		ret = v4l2_subdev_call(subdev_preprocessor, core, init, 0);
		if (ret) {
			err("v4l2_preprocessor_call(init) is fail(%d)", ret);
			goto p_err;
		}
	}
#endif

	pr_info("[MOD:D:%d] %s(%d)\n", module->sensor_id, __func__, val);

p_err:
	return ret;
}

int sensor_module_deinit(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;
	struct fimc_is_preprocessor *preprocessor = NULL;
	struct v4l2_subdev *subdev_preprocessor = NULL;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	BUG_ON(!module);

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	subdev_preprocessor = sensor_peri->subdev_preprocessor;

	if (subdev_preprocessor){
		preprocessor = (struct fimc_is_preprocessor *)v4l2_get_subdevdata(subdev_preprocessor);
		BUG_ON(!preprocessor);
	}

	fimc_is_sensor_deinit_sensor_thread(sensor_peri);

	/* kthread stop to sensor setting when s_format */
	fimc_is_sensor_deinit_mode_change_thread(sensor_peri);

#ifdef CONFIG_OIS_USE_RUMBA_S6
	if (sensor_peri->subdev_ois != NULL) {
		ret = CALL_OISOPS(sensor_peri->ois, ois_deinit, sensor_peri->subdev_ois);
		if (ret < 0) {
			err("v4l2_subdev_call(ois_deinit) is fail(%d)", ret);
		}
	}
#endif

	if (sensor_peri->flash != NULL) {
		sensor_peri->flash->flash_data.mode = CAM2_FLASH_MODE_OFF;
		if (sensor_peri->flash->flash_data.flash_fired == true) {
			ret = fimc_is_sensor_flash_fire(sensor_peri, 0);
			if (ret) {
				err("failed to turn off flash at flash expired handler\n");
			}
		}
	}
/* HACK : temporary code
	if (sensor_peri->actuator) {
		ret = fimc_is_sensor_peri_actuator_softlanding(sensor_peri);
		if (ret)
			err("failed to soft landing control of actuator driver\n");
	}
*/
	if (sensor_peri->flash != NULL) {
		cancel_work_sync(&sensor_peri->flash->flash_data.flash_fire_work);
		cancel_work_sync(&sensor_peri->flash->flash_data.flash_expire_work);
	}
	cancel_work_sync(&sensor_peri->cis.cis_status_dump_work);

	if (subdev_preprocessor) {
		CALL_PREPROPOPS(preprocessor, preprocessor_deinit, subdev_preprocessor);
	}

	clear_bit(FIMC_IS_SENSOR_PREPROCESSOR_AVAILABLE, &sensor_peri->peri_state);
	clear_bit(FIMC_IS_SENSOR_ACTUATOR_AVAILABLE, &sensor_peri->peri_state);
	clear_bit(FIMC_IS_SENSOR_FLASH_AVAILABLE, &sensor_peri->peri_state);
	clear_bit(FIMC_IS_SENSOR_OIS_AVAILABLE, &sensor_peri->peri_state);

	pr_info("[MOD:D:%d] %s\n", module->sensor_id, __func__);

	return ret;
}

long sensor_module_ioctl(struct v4l2_subdev *subdev, unsigned int cmd, void *arg)
{
	int ret = 0;

	BUG_ON(!subdev);

	switch(cmd) {
	case V4L2_CID_SENSOR_DEINIT:
		ret = sensor_module_deinit(subdev);
		if (ret) {
			err("err!!! ret(%d), sensor module deinit fail", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_NOTIFY_VSYNC:
		ret = fimc_is_sensor_peri_notify_vsync(subdev, arg);
		if (ret) {
			err("err!!! ret(%d), sensor notify vsync fail", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_NOTIFY_VBLANK:
		ret = fimc_is_sensor_peri_notify_vblank(subdev, arg);
		if (ret) {
			err("err!!! ret(%d), sensor notify vblank fail", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_NOTIFY_FLASH_FIRE:
		/* Do not use */
		break;
	case V4L2_CID_SENSOR_NOTIFY_ACTUATOR:
		ret = fimc_is_sensor_peri_notify_actuator(subdev, arg);
		if (ret) {
			err("err!!! ret(%d), sensor notify actuator fail", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_NOTIFY_M2M_ACTUATOR:
		ret = fimc_is_actuator_notify_m2m_actuator(subdev);
		if (ret) {
			err("err!!! ret(%d), sensor notify M2M actuator fail", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_NOTIFY_ACTUATOR_INIT:
		ret = fimc_is_sensor_peri_notify_actuator_init(subdev);
		if (ret) {
			err("err!!! ret(%d), actuator init fail\n", ret);
			goto p_err;
		}
		break;

	default:
		err("err!!! Unknown CID(%#x)", cmd);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

int sensor_module_g_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *ctrl)
{
	int ret = 0;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;
	cis_setting_info info;
	info.param = NULL;
	info.return_value = 0;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	BUG_ON(!module);

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	BUG_ON(!sensor_peri);

	switch(ctrl->id) {
	case V4L2_CID_SENSOR_ADJUST_FRAME_DURATION:
		/* TODO: v4l2 g_ctrl cannot support adjust function */
#if 0
		info.param = (void *)&ctrl->value;
		ret = CALL_CISOPS(&sensor_peri->cis, cis_adjust_frame_duration, sensor_peri->subdev_cis, &info);
		if (ret < 0 || info.return_value == 0) {
			err("err!!! ret(%d), frame duration(%d)", ret, info.return_value);
			ctrl->value = 0;
			ret = -EINVAL;
			goto p_err;
		}
		ctrl->value = info.return_value;
#endif
		break;
	case V4L2_CID_SENSOR_GET_MIN_EXPOSURE_TIME:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_min_exposure_time, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d), min exposure time(%d)", ret, info.return_value);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_GET_MAX_EXPOSURE_TIME:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_max_exposure_time, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ctrl->value = 0;
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_ADJUST_ANALOG_GAIN:
		/* TODO: v4l2 g_ctrl cannot support adjust function */
#if 0
		info.param = (void *)&ctrl->value;
		ret = CALL_CISOPS(&sensor_peri->cis, cis_adjust_analog_gain, sensor_peri->subdev_cis, &info);
		if (ret < 0 || info.return_value == 0) {
			err("err!!! ret(%d), adjust analog gain(%d)", ret, info.return_value);
			ctrl->value = 0;
			ret = -EINVAL;
			goto p_err;
		}
		ctrl->value = info.return_value;
#endif
		break;
	case V4L2_CID_SENSOR_GET_ANALOG_GAIN:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_analog_gain, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_GET_MIN_ANALOG_GAIN:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_min_analog_gain, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_GET_MAX_ANALOG_GAIN:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_max_analog_gain, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_GET_DIGITAL_GAIN:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_digital_gain, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_GET_MIN_DIGITAL_GAIN:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_min_digital_gain, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_GET_MAX_DIGITAL_GAIN:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_get_max_digital_gain, sensor_peri->subdev_cis, &ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	case V4L2_CID_ACTUATOR_GET_STATUS:
		ret = v4l2_subdev_call(sensor_peri->subdev_actuator, core, g_ctrl, ctrl);
		if (ret) {
			err("[SEN:%d] v4l2_subdev_call(g_ctrl, id:%d) is fail(%d)",
					module->sensor_id, ctrl->id, ret);
			goto p_err;
		}
		break;
	default:
		err("err!!! Unknown CID(%#x)", ctrl->id);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

int sensor_module_s_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *ctrl)
{
	int ret = 0;
	struct fimc_is_device_sensor *device = NULL;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	BUG_ON(!module);

	device = (struct fimc_is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	BUG_ON(!device);

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	BUG_ON(!sensor_peri);

	switch(ctrl->id) {
	case V4L2_CID_SENSOR_SET_AE_TARGET:
		/* long_exposure_time and short_exposure_time is same value */
		ret = fimc_is_sensor_peri_s_exposure_time(device, ctrl->value, ctrl->value);
		if (ret < 0) {
			err("failed to set exposure time : %d\n - %d",
					ctrl->value, ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_SET_FRAME_RATE:
		ret = CALL_CISOPS(&sensor_peri->cis, cis_set_frame_rate, sensor_peri->subdev_cis, ctrl->value);
		if (ret < 0) {
			err("err!!! ret(%d)", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_SET_FRAME_DURATION:
		ret = fimc_is_sensor_peri_s_frame_duration(device, ctrl->value);
		if (ret < 0) {
			err("failed to set frame duration : %d\n - %d",
					ctrl->value, ret);
			goto p_err;
		}
		break;
	case V4L2_CID_SENSOR_SET_GAIN:
	case V4L2_CID_SENSOR_SET_ANALOG_GAIN:
	if (sensor_peri->cis.cis_data->analog_gain[1] != ctrl->value) {
		/* long_analog_gain and short_analog_gain is same value */
		ret = fimc_is_sensor_peri_s_analog_gain(device, ctrl->value, ctrl->value);
		if (ret < 0) {
			err("failed to set analog gain : %d\n - %d",
					ctrl->value, ret);
			goto p_err;
		}
		break;
	}
	case V4L2_CID_SENSOR_SET_DIGITAL_GAIN:
		/* long_digital_gain and short_digital_gain is same value */
		ret = fimc_is_sensor_peri_s_digital_gain(device, ctrl->value, ctrl->value);
		if (ret < 0) {
			err("failed to set digital gain : %d\n - %d",
					ctrl->value, ret);
			goto p_err;
		}
		break;
	case V4L2_CID_ACTUATOR_SET_POSITION:
		if (sensor_peri->subdev_ois != NULL) {
			ret = CALL_OISOPS(sensor_peri->ois, ois_shift_compensation, sensor_peri->subdev_ois, ctrl->value,
								sensor_peri->actuator->pos_size_bit);
			if (ret < 0) {
				err("ois shift compensation fail");
				goto p_err;
			}
		}
		ret = v4l2_subdev_call(sensor_peri->subdev_actuator, core, s_ctrl, ctrl);
		if (ret < 0) {
			err("[SEN:%d] v4l2_subdev_call(s_ctrl, id:%d) is fail(%d)",
					module->sensor_id, ctrl->id, ret);
			goto p_err;
		}
		break;
	case V4L2_CID_FLASH_SET_INTENSITY:
	case V4L2_CID_FLASH_SET_FIRING_TIME:
		ret = v4l2_subdev_call(sensor_peri->subdev_flash, core, s_ctrl, ctrl);
		if (ret) {
			err("[SEN:%d] v4l2_subdev_call(s_ctrl, id:%d) is fail(%d)",
					module->sensor_id, ctrl->id, ret);
			goto p_err;
		}
		break;
	case V4L2_CID_FLASH_SET_MODE:
		if (ctrl->value < CAM2_FLASH_MODE_OFF || ctrl->value > CAM2_FLASH_MODE_BEST) {
			err("failed to flash set mode: %d, \n", ctrl->value);
			ret = -EINVAL;
			goto p_err;
		}
		if (sensor_peri->flash->flash_data.mode != ctrl->value) {
			ret = fimc_is_sensor_flash_fire(sensor_peri, 0);
			if (ret) {
				err("failed to flash fire: %d\n", ctrl->value);
				ret = -EINVAL;
				goto p_err;
			}
			sensor_peri->flash->flash_data.mode = ctrl->value;
		}
		break;
	case V4L2_CID_FLASH_SET_FIRE:
		ret = fimc_is_sensor_flash_fire(sensor_peri, ctrl->value);
		if (ret) {
			err("failed to flash fire: %d\n", ctrl->value);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	default:
		err("err!!! Unknown CID(%#x)", ctrl->id);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

int sensor_module_g_ext_ctrls(struct v4l2_subdev *subdev, struct v4l2_ext_controls *ctrls)
{
	int ret = 0;
	struct fimc_is_module_enum *module;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);

	/* TODO */
	pr_info("[MOD:D:%d] %s Not implemented\n", module->sensor_id, __func__);

	return ret;
}

int sensor_module_s_ext_ctrls(struct v4l2_subdev *subdev, struct v4l2_ext_controls *ctrls)
{
	int ret = 0;
	struct fimc_is_module_enum *module;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);

	/* TODO */
	pr_info("[MOD:D:%d] %s Not implemented\n", module->sensor_id, __func__);

	return ret;
}

int sensor_module_s_stream(struct v4l2_subdev *subdev, int enable)
{
	int ret = 0;
	struct fimc_is_device_sensor *device = NULL;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	BUG_ON(!module);

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	BUG_ON(!sensor_peri);

	device = (struct fimc_is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	BUG_ON(!device);

	/*
	 * Camera first mode set high speed recording and maintain 120fps
	 * not setting exposure so need to this check
	 */
	if (sensor_peri->cis.cis_data->video_mode == true && device->cfg->framerate >= 60) {
		sensor_peri->sensor_interface.diff_bet_sen_isp = sensor_peri->sensor_interface.otf_flag_3aa? DIFF_OTF_DELAY + 1 : DIFF_M2M_DELAY;
		if (fimc_is_sensor_init_sensor_thread(sensor_peri))
			err("fimc_is_sensor_init_sensor_thread is fail");
	} else {
		sensor_peri->sensor_interface.diff_bet_sen_isp = sensor_peri->sensor_interface.otf_flag_3aa? DIFF_OTF_DELAY : DIFF_M2M_DELAY;
	}

	ret = fimc_is_sensor_peri_s_stream(device, enable);
	if (ret)
		err("[SEN] fimc_is_sensor_peri_s_stream is fail(%d)", ret);

	return ret;
}

int sensor_module_s_format(struct v4l2_subdev *subdev,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	struct fimc_is_device_sensor *device = NULL;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;
	struct v4l2_subdev *sd = NULL;
	struct v4l2_subdev *subdev_preprocessor = NULL;
	struct fimc_is_cis *cis = NULL;
	struct fimc_is_preprocessor *preprocessor = NULL;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	BUG_ON(!module);

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	BUG_ON(!sensor_peri);

	sd = sensor_peri->subdev_cis;
	if (!sd) {
		err("[SEN:%d] no subdev_cis(set_fmt)", module->sensor_id);
		ret = -ENXIO;
		goto p_err;
	}
	cis = (struct fimc_is_cis *)v4l2_get_subdevdata(sd);
	BUG_ON(!cis);
	BUG_ON(!cis->cis_data);

	device = (struct fimc_is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	BUG_ON(!device);

	subdev_preprocessor = sensor_peri->subdev_preprocessor;

	if (subdev_preprocessor){
		preprocessor = (struct fimc_is_preprocessor *)v4l2_get_subdevdata(subdev_preprocessor);
		BUG_ON(!preprocessor);

		CALL_PREPROPOPS(preprocessor, preprocessor_s_format, subdev_preprocessor, device);
	}

	if (cis->cis_data->sens_config_index_cur != device->cfg->mode
		|| sensor_peri->mode_change_first == true) {
		dbg_sensor("[%s] mode changed(%d->%d)\n", __func__,
				cis->cis_data->sens_config_index_cur, device->cfg->mode);

		cis->cis_data->sens_config_index_cur = device->cfg->mode;
		cis->cis_data->cur_width = fmt->format.width;
		cis->cis_data->cur_height = fmt->format.height;

		ret = fimc_is_sensor_mode_change(cis, device->cfg->mode);
		if (ret) {
			err("[SEN:%d] sensor_mode_change(cis_mode_change) is fail(%d)",
					module->sensor_id, ret);
			goto p_err;
		}

		if (subdev_preprocessor)
			ret = CALL_PREPROPOPS(preprocessor, preprocessor_mode_change, subdev_preprocessor, device);
		if (ret) {
			err("[SEN:%d] sensor_mode_change(preprocessor_mode_change) is fail(%d)",
					module->sensor_id, ret);
			goto p_err;
		}
	}

	dbg_sensor("[%s] set format done, size(%dx%d), code(%#x)\n", __func__,
			fmt->format.width, fmt->format.height, fmt->format.code);

p_err:
	return ret;
}

int sensor_module_log_status(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;

	if (unlikely(!subdev)) {
		goto p_err;
	}

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	if (unlikely(!module)) {
		goto p_err;
	}

	sensor_peri = (struct fimc_is_device_sensor_peri *)module->private_data;
	if (unlikely(!sensor_peri)) {
		goto p_err;
	}

	schedule_work(&sensor_peri->cis.cis_status_dump_work);

p_err:
	return ret;
}
