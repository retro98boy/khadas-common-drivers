// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Amlogic, Inc. All rights reserved.
 */

#include "sensor_drv.h"
#include "i2c_api.h"
#include "mclk_api.h"
#include "aml_imx678.h"

#define AML_SENSOR_NAME  "imx678-%u"

/* supported link frequencies */
#define FREQ_INDEX_2160P    0
#define FREQ_INDEX_2160P_HDR 1

static const s64 imx678_link_freq_4lanes[] = {
	[FREQ_INDEX_2160P] = 1440000000,
	[FREQ_INDEX_2160P_HDR] = 1440000000,
};

static inline const s64 *imx678_link_freqs_ptr(const struct imx678 *imx678)
{
	return imx678_link_freq_4lanes;

}

static inline int imx678_link_freqs_num(const struct imx678 *imx678)
{
	return ARRAY_SIZE(imx678_link_freq_4lanes);
}

static const struct imx678_mode imx678_modes_4lanes[] = {
	{
		.width = 3840,
		.height = 2160,
		.hmax = 0x0226,
		.link_freq_index = FREQ_INDEX_2160P,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.data = setting_4lane_3840_2160_1440m_60fps,
		.data_size = ARRAY_SIZE(setting_4lane_3840_2160_1440m_60fps),
	},
	{
		.width = 3840,
		.height = 2160,
		.hmax = 0x0226,
		.link_freq_index = FREQ_INDEX_2160P_HDR,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.data = dol_4k_30fps_1440Mbps_4lane_12bits,
		.data_size = ARRAY_SIZE(dol_4k_30fps_1440Mbps_4lane_12bits),
	},
};

static inline const struct imx678_mode *imx678_modes_ptr(const struct imx678 *imx678)
{
	return imx678_modes_4lanes;
}

static inline int imx678_modes_num(const struct imx678 *imx678)
{
	return ARRAY_SIZE(imx678_modes_4lanes);
}

static inline struct imx678 *to_imx678(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx678, sd);
}

static inline int imx678_read_reg(struct imx678 *imx678, u16 addr, u8 *value)
{
	unsigned int regval;

	int i, ret;

	for (i = 0; i < 3; ++i) {
		ret = regmap_read(imx678->regmap, addr, &regval);
		if (0 == ret ) {
			break;
		}
	}

	if (ret)
		dev_err(imx678->dev, "I2C read with i2c transfer failed for addr: %x, ret %d\n", addr, ret);

	*value = regval & 0xff;
	return 0;
}

static int imx678_write_reg(struct imx678 *imx678, u16 addr, u8 value)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = regmap_write(imx678->regmap, addr, value);
		if (0 == ret) {
			break;
		}
	}

	if (ret)
		dev_err(imx678->dev, "I2C write failed for addr: %x, ret %d\n", addr, ret);

	return ret;
}

static int imx678_set_register_array(struct imx678 *imx678,
				const struct imx678_regval *settings,
				unsigned int num_settings)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < num_settings; ++i, ++settings) {
		ret = imx678_write_reg(imx678, settings->reg, settings->val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int imx678_write_buffered_reg(struct imx678 *imx678, u16 address_low,
				u8 nr_regs, u32 value)
{
	u8 val = 0;
	int ret = 0;
	unsigned int i;

	ret = imx678_write_reg(imx678, IMX678_REGHOLD, 0x01);
	if (ret) {
		dev_err(imx678->dev, "Error setting hold register\n");
		return ret;
	}

	for (i = 0; i < nr_regs; i++) {
		val = (u8)(value >> (i * 8)); //low register writed low value
		//pr_err("%s addr: 0x%x, value 0x%x", __func__, (address_low + i), val);
		ret = imx678_write_reg(imx678, address_low + i, val);
		if (ret) {
			dev_err(imx678->dev, "Error writing buffered registers\n");
			return ret;
		}
	}

	ret = imx678_write_reg(imx678, IMX678_REGHOLD, 0x00);
	if (ret) {
		dev_err(imx678->dev, "Error setting hold register\n");
		return ret;
	}

	return ret;
}

static int imx678_set_gain(struct imx678 *imx678, u32 value)
{
	int ret = 0;
	u8 val;
	//pr_err("%s gain value 0x%x", __func__, value);
	ret = imx678_write_buffered_reg(imx678, IMX678_GAIN, 2, value);
	if (ret)
		dev_err(imx678->dev, "Unable to write gain\n");
	imx678_read_reg(imx678, IMX678_GAIN, &val);
	//pr_err("%s read gain value 0x%x", __func__, val);
	imx678_read_reg(imx678, IMX678_GAIN+1, &val);
	//pr_err("%s read gain value 0x%x", __func__, val);
	return ret;
}

static int imx678_set_exposure(struct imx678 *imx678, u32 value)
{
	int ret = 0;
	int shr0_reg = value & 0xFFFF;
	int shr1_reg = (value >> 16) & 0xFFFF;
	u8 val;
	//pr_err("%s exporL value 0x%x, exporS value 0x%x\n", __func__, shr0_reg, shr1_reg);
	ret = imx678_write_buffered_reg(imx678, IMX678_EXPOSURE, 2, shr0_reg);
	if (ret)
		dev_err(imx678->dev, "Unable to write expo\n");

	if (imx678->enWDRMode) {
		ret = imx678_write_buffered_reg(imx678, IMX678_EXPOSURE_SHR1, 2, shr1_reg);
		//dev_info(imx678->dev,"expo 0x%x reg value: SHR0-F1-big 0x%x SHR1-f0-small 0x%x", value, shr0_reg , shr1_reg);
		if (ret)
			dev_err(imx678->dev, "Unable to write exposure SHR1 reg\n");
	}



	imx678_read_reg(imx678, IMX678_EXPOSURE, &val);
	//pr_err("%s read exp value 0x%x", __func__, val);
	val = 0;
	imx678_read_reg(imx678, IMX678_EXPOSURE+1, &val);
	//pr_err("%s read exp value 0x%x", __func__, val);
	return ret;
}

static int imx678_set_fps(struct imx678 *imx678, u32 value)
{
	return 0;
}

static int imx678_stop_streaming(struct imx678 *imx678)
{
	int ret;
	imx678->enWDRMode = WDR_MODE_NONE;

	ret = imx678_write_reg(imx678, IMX678_STANDBY, 0x01);
	ret = imx678_write_reg(imx678, 0x3002, 0x01);
	if (ret != 0)
		pr_err("%s write register fail\n", __func__);
	return ret;
}

static int imx678_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx678 *imx678 = container_of(ctrl->handler,
					struct imx678, ctrls);
	int ret = 0;

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(imx678->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		ret = imx678_set_gain(imx678, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = imx678_set_exposure(imx678, ctrl->val);
		break;
	case V4L2_CID_HBLANK:
		break;
	case V4L2_CID_AML_CSI_LANES:
		break;
	case V4L2_CID_AML_MODE:
		imx678->enWDRMode = ctrl->val;
		break;
	case V4L2_CID_AML_ORIG_FPS:
		imx678->fps = ctrl->val;
		if (imx678->fps != 60) {
			ret = imx678_set_fps(imx678, imx678->fps);
		}
		break;
	default:
		dev_err(imx678->dev, "Error ctrl->id %u, flag 0x%lx\n",
			ctrl->id, ctrl->flags);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(imx678->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx678_ctrl_ops = {
	.s_ctrl = imx678_set_ctrl,
};
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int imx678_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
#else
static int imx678_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
#endif
{
	if (code->index >= ARRAY_SIZE(imx678_formats))
		return -EINVAL;

	code->code = imx678_formats[code->index].code;

	return 0;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int imx678_enum_frame_size(struct v4l2_subdev *sd,
			        struct v4l2_subdev_state *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
#else
static int imx678_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
#endif
{
	int cfg_num = 1; //report one max size
	if (fse->index >= cfg_num)
		return -EINVAL;

	fse->min_width = imx678_formats[0].max_width;
	fse->min_height = imx678_formats[0].max_height;;
	fse->max_width = imx678_formats[0].max_width;
	fse->max_height = imx678_formats[0].max_height;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int imx678_enum_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_frame_interval_enum *fie)
#else
static int imx678_enum_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
#endif
{
	struct imx678 *imx678 = to_imx678(sd);
	int cfg_num = imx678_modes_num(imx678);
	const struct imx678_mode* supported_modes = imx678_modes_ptr(imx678);
	if (fie->index >= cfg_num)
		return -EINVAL;

	fie->code = imx678_formats[0].code;
	fie->width = imx678_formats[0].max_width;
	fie->height = imx678_formats[0].max_height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int imx678_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *cfg,
			  struct v4l2_subdev_format *fmt)
#else
static int imx678_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
#endif
{
	struct imx678 *imx678 = to_imx678(sd);
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&imx678->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		framefmt = v4l2_subdev_get_try_format(&imx678->sd, cfg,
						      fmt->pad);
	else
		framefmt = &imx678->current_format;

	fmt->format = *framefmt;

	mutex_unlock(&imx678->lock);

	return 0;
}

static inline u8 imx678_get_link_freq_index(struct imx678 *imx678)
{
	return imx678->current_mode->link_freq_index;
}

static s64 imx678_get_link_freq(struct imx678 *imx678)
{
	u8 index = imx678_get_link_freq_index(imx678);

	return *(imx678_link_freqs_ptr(imx678) + index);
}

static u64 imx678_calc_pixel_rate(struct imx678 *imx678)
{
	s64 link_freq = imx678_get_link_freq(imx678);
	u8 nlanes = imx678->nlanes;
	u64 pixel_rate;

	/* pixel rate = link_freq * 2 * nr_of_lanes / bits_per_sample */
	pixel_rate = link_freq * 2 * nlanes;
	do_div(pixel_rate, imx678->bpp);
	return pixel_rate;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int imx678_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *cfg,
			struct v4l2_subdev_format *fmt)
#else
static int imx678_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
#endif
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_mode *mode;
	struct v4l2_mbus_framefmt *format;
	unsigned int i,ret;

	mutex_lock(&imx678->lock);
	mode = v4l2_find_nearest_size(imx678_modes_ptr(imx678),
				 imx678_modes_num(imx678),
				width, height,
				fmt->format.width, fmt->format.height);
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;

	for (i = 0; i < ARRAY_SIZE(imx678_formats); i++) {
		if (imx678_formats[i].code == fmt->format.code) {
			dev_info(imx678->dev, "find proper format %d \n",i);
			break;
		}
	}

	if (i >= ARRAY_SIZE(imx678_formats)) {
		i = 0;
		dev_err(imx678->dev, "No format. reset i = 0 \n");
	}

	fmt->format.code = imx678_formats[i].code;
	fmt->format.field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		dev_info(imx678->dev, "try format \n");
		format = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		mutex_unlock(&imx678->lock);
		return 0;
	} else {
		dev_err(imx678->dev, "set format, w %d, h %d, code 0x%x \n",
			fmt->format.width, fmt->format.height,
			fmt->format.code);
		format = &imx678->current_format;
		imx678->current_mode = mode;
		imx678->bpp = imx678_formats[i].bpp;
		imx678->nlanes = 4;

		if (imx678->link_freq)
			__v4l2_ctrl_s_ctrl(imx678->link_freq, imx678_get_link_freq_index(imx678) );
		if (imx678->pixel_rate)
			__v4l2_ctrl_s_ctrl_int64(imx678->pixel_rate, imx678_calc_pixel_rate(imx678) );
		if (imx678->data_lanes)
			__v4l2_ctrl_s_ctrl(imx678->data_lanes, imx678->nlanes);
	}

	*format = fmt->format;
	imx678->status = 0;

	mutex_unlock(&imx678->lock);

	if (imx678->enWDRMode) {
		ret = imx678_set_register_array(imx678, dol_4k_30fps_1440Mbps_4lane_12bits,
			ARRAY_SIZE(dol_4k_30fps_1440Mbps_4lane_12bits));
		if (ret < 0) {
			dev_err(imx678->dev, "Could not set init wdr registers\n");
			return ret;
		} else
			dev_err(imx678->dev, "imx678 wdr mode init...\n");
	} else {/* Set init register settings */
		ret = imx678_set_register_array(imx678, setting_4lane_3840_2160_1440m_60fps,
			ARRAY_SIZE(setting_4lane_3840_2160_1440m_60fps));
		if (ret < 0) {
			dev_err(imx678->dev, "Could not set init registers\n");
			return ret;
		} else
			dev_err(imx678->dev, "imx678 linear mode init...\n");
	}
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
int imx678_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *cfg,
			     struct v4l2_subdev_selection *sel)
#else
int imx678_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
#endif
{
	int rtn = 0;
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_mode *mode = imx678->current_mode;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = mode->width;
		sel->r.height = mode->height;
	break;
	case V4L2_SEL_TGT_CROP:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = mode->width;
		sel->r.height = mode->height;
	break;
	default:
		rtn = -EINVAL;
		dev_err(imx678->dev, "Error support target: 0x%x\n", sel->target);
	break;
	}

	return rtn;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int imx678_entity_init_cfg(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *cfg)
#else
static int imx678_entity_init_cfg(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg)
#endif
{
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = cfg ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.width = 3840;
	fmt.format.height = 2160;
	fmt.format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	imx678_set_fmt(subdev, cfg, &fmt);
	return 0;
}

static int imx678_start_streaming(struct imx678 *imx678)
{
	int ret = 0;
	ret = imx678_write_reg(imx678, IMX678_STANDBY, 0x00);
	if (ret) {
		pr_err("%s: start streaming fail\n", __func__);
		return ret;
	}

	usleep_range(30000, 31000);
	ret = imx678_write_reg(imx678, 0x3002, 0x00);
	if (ret) {
		pr_err("%s: start streaming fail\n", __func__);
		return ret;
	}

	return ret;
}

static int imx678_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx678 *imx678 = to_imx678(sd);
	int ret = 0;

	if (imx678->status == enable)
		return ret;
	else
		imx678->status = enable;

	if (enable) {
		ret = imx678_start_streaming(imx678);
		if (ret) {
			dev_err(imx678->dev, "Start stream failed\n");
			goto unlock_and_return;
		}

		dev_info(imx678->dev, "stream on\n");
	} else {
		imx678_stop_streaming(imx678);

		dev_info(imx678->dev, "stream off\n");
	}

unlock_and_return:

	return ret;
}

int imx678_power_on(struct device *dev, struct sensor_gpio *gpio)
{
	int ret;

	gpiod_set_value_cansleep(gpio->rst_gpio, 1);

	 ret = mclk_enable(dev, 37125000);
	 if (ret < 0 )
	 dev_err(dev, "set mclk fail\n");

	 udelay(30);

	 // 30ms
	 usleep_range(30000, 31000);

	return 0;
}


int imx678_power_off(struct device *dev, struct sensor_gpio *gpio)
{
	mclk_disable(dev);
	gpiod_set_value_cansleep(gpio->rst_gpio, 0);
	return 0;
}

int imx678_power_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	dev_err(dev, "%s\n", __func__);
	imx678_power_off(imx678->dev, imx678->gpio);

	return 0;
}

int imx678_power_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	dev_err(dev, "%s\n", __func__);
	imx678_power_on(imx678->dev, imx678->gpio);

	return 0;
}

static int imx678_log_status(struct v4l2_subdev *sd)
{
	struct imx678 *imx678 = to_imx678(sd);

	dev_info(imx678->dev, "log status done\n");

	return 0;
}

int imx678_sbdev_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh) {
	struct imx678 *imx678 = to_imx678(sd);

	if (atomic_inc_return(&imx678->open_count) == 1)
		imx678_power_on(imx678->dev, imx678->gpio);
	return 0;
}

int imx678_sbdev_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh) {
	struct imx678 *imx678 = to_imx678(sd);
	imx678_set_stream(sd, 0);

	if (atomic_dec_and_test(&imx678->open_count))
		imx678_power_off(imx678->dev, imx678->gpio);
	return 0;
}

static const struct dev_pm_ops imx678_pm_ops = {
	SET_RUNTIME_PM_OPS(imx678_power_suspend, imx678_power_resume, NULL)
};

const struct v4l2_subdev_core_ops imx678_core_ops = {
	.log_status = imx678_log_status,
};

static const struct v4l2_subdev_video_ops imx678_video_ops = {
	.s_stream = imx678_set_stream,
};

static const struct v4l2_subdev_pad_ops imx678_pad_ops = {
	.init_cfg = imx678_entity_init_cfg,
	.enum_mbus_code = imx678_enum_mbus_code,
	.enum_frame_size = imx678_enum_frame_size,
	.enum_frame_interval = imx678_enum_frame_interval,
	.get_selection = imx678_get_selection,
	.get_fmt = imx678_get_fmt,
	.set_fmt = imx678_set_fmt,
};
static struct v4l2_subdev_internal_ops imx678_internal_ops = {
	.open = imx678_sbdev_open,
	.close = imx678_sbdev_close,
};

static const struct v4l2_subdev_ops imx678_subdev_ops = {
	.core = &imx678_core_ops,
	.video = &imx678_video_ops,
	.pad = &imx678_pad_ops,
};

static const struct media_entity_operations imx678_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static struct v4l2_ctrl_config wdr_cfg = {
	.ops = &imx678_ctrl_ops,
	.id = V4L2_CID_AML_MODE,
	.name = "wdr mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 2,
	.step = 1,
	.def = 0,
};

static struct v4l2_ctrl_config fps_cfg = {
	.ops = &imx678_ctrl_ops,
	.id = V4L2_CID_AML_ORIG_FPS,
	.name = "sensor fps",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
	.min = 1,
	.max = 60,
	.step = 1,
	.def = 30,
};

static struct v4l2_ctrl_config nlane_cfg = {
	.ops = &imx678_ctrl_ops,
	.id = V4L2_CID_AML_CSI_LANES,
	.name = "sensor lanes",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.flags = V4L2_CTRL_FLAG_VOLATILE,
	.min = 1,
	.max = 4,
	.step = 1,
	.def = 4,
};

static int imx678_ctrls_init(struct imx678 *imx678)
{
	int rtn = 0;

	v4l2_ctrl_handler_init(&imx678->ctrls, 7);

	v4l2_ctrl_new_std(&imx678->ctrls, &imx678_ctrl_ops,
				V4L2_CID_GAIN, 0, 0xffff, 1, 0);

	v4l2_ctrl_new_std(&imx678->ctrls, &imx678_ctrl_ops,
				V4L2_CID_EXPOSURE, 0, 0x7fffffff, 1, 0);

	imx678->link_freq = v4l2_ctrl_new_int_menu(&imx678->ctrls,
					       &imx678_ctrl_ops,
					       V4L2_CID_LINK_FREQ,
					       imx678_link_freqs_num(imx678) - 1,
					       0, imx678_link_freqs_ptr(imx678) );

	if (imx678->link_freq)
		imx678->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx678->pixel_rate = v4l2_ctrl_new_std(&imx678->ctrls,
					       &imx678_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       1, INT_MAX, 1,
					       imx678_calc_pixel_rate(imx678));

	imx678->data_lanes = v4l2_ctrl_new_custom(&imx678->ctrls, &nlane_cfg, NULL);
	if (imx678->data_lanes)
		imx678->data_lanes->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx678->wdr = v4l2_ctrl_new_custom(&imx678->ctrls, &wdr_cfg, NULL);

	v4l2_ctrl_new_custom(&imx678->ctrls, &fps_cfg, NULL);

	imx678->sd.ctrl_handler = &imx678->ctrls;

	if (imx678->ctrls.error) {
		dev_err(imx678->dev, "Control initialization a error  %d\n",
			imx678->ctrls.error);
		rtn = imx678->ctrls.error;
	}

	return rtn;
}

static int imx678_register_subdev(struct imx678 *imx678)
{
	int rtn = 0;

	v4l2_i2c_subdev_init(&imx678->sd, imx678->client, &imx678_subdev_ops);

	imx678->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx678->sd.dev = &imx678->client->dev;
	imx678->sd.internal_ops = &imx678_internal_ops;
	imx678->sd.entity.ops = &imx678_subdev_entity_ops;
	imx678->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	snprintf(imx678->sd.name, sizeof(imx678->sd.name), AML_SENSOR_NAME, imx678->index);

	imx678->pad.flags = MEDIA_PAD_FL_SOURCE;
	rtn = media_entity_pads_init(&imx678->sd.entity, 1, &imx678->pad);
	if (rtn < 0) {
		dev_err(imx678->dev, "Could not register media entity\n");
		goto err_return;
	}

	rtn = v4l2_async_register_subdev(&imx678->sd);
	if (rtn < 0) {
		dev_err(imx678->dev, "Could not register v4l2 device\n");
		goto err_return;
	}

err_return:
	return rtn;
}

int imx678_init(struct i2c_client *client, void *sdrv)
{
	struct device *dev = &client->dev;
	struct imx678 *imx678;
	struct amlsens *sensor = (struct amlsens *)sdrv;
	int ret = -EINVAL;

	imx678 = devm_kzalloc(dev, sizeof(*imx678), GFP_KERNEL);
	if (!imx678)
		return -ENOMEM;

	imx678->dev = dev;
	imx678->client = client;
	imx678->client->addr = IMX678_SLAVE_ID;
	imx678->gpio = &sensor->gpio;
	imx678->fps = 30;
	imx678->nlanes = 4;

	imx678->regmap = devm_regmap_init_i2c(client, &imx678_regmap_config);
	if (IS_ERR(imx678->regmap)) {
		dev_err(dev, "Unable to initialize I2C\n");
		return -ENODEV;
	}

	if (of_property_read_u32(dev->of_node, "index", &imx678->index)) {
		dev_err(dev, "Failed to read sensor index. default to 0\n");
		imx678->index = 0;
	}

	mutex_init(&imx678->lock);

	/* Power on the device to match runtime PM state below */
	ret = imx678_power_on(imx678->dev, imx678->gpio);
	if (ret < 0) {
		dev_err(dev, "Could not power on the device\n");
		return -ENODEV;
	}

	/*
	 * Initialize the frame format. In particular, imx678->current_mode
	 * and imx678->bpp are set to defaults: imx678_calc_pixel_rate() call
	 * below in imx678_ctrls_init relies on these fields.
	 */
	imx678_entity_init_cfg(&imx678->sd, NULL);

	ret = imx678_ctrls_init(imx678);
	if (ret) {
		dev_err(imx678->dev, "Error ctrls init\n");
		goto free_ctrl;
	}

	ret = imx678_register_subdev(imx678);
	if (ret) {
		dev_err(imx678->dev, "Error register subdev\n");
		goto free_entity;
	}

	dev_info(imx678->dev, "probe done \n");

	return 0;

free_entity:
	media_entity_cleanup(&imx678->sd.entity);
free_ctrl:
	v4l2_ctrl_handler_free(&imx678->ctrls);
	mutex_destroy(&imx678->lock);

	return ret;
}

int imx678_deinit(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	mutex_destroy(&imx678->lock);

	imx678_power_off(imx678->dev, imx678->gpio);

	return 0;
}

int imx678_sensor_id(struct i2c_client *client)
{
	u32 id = 0;
	u8 val = 0;
	int rtn = -EINVAL;

	i2c_write_a16d8(client, IMX678_SLAVE_ID, IMX678_STANDBY, 0x00);
	usleep_range(30000, 31000);

	i2c_read_a16d8(client, IMX678_SLAVE_ID, 0x4d1d, &val);
	id |= (val << 8);
	i2c_read_a16d8(client, IMX678_SLAVE_ID, 0x4d1c, &val);
	id |= val;

	i2c_write_a16d8(client, IMX678_SLAVE_ID, IMX678_STANDBY, 0x01);

	if (id != IMX678_ID) {
		dev_err(&client->dev, "Failed to get imx678 id: 0x%x\n", id);
		return rtn;
	} else {
		dev_err(&client->dev, "success get imx678 id 0x%x", id);
	}

	return 0;
}


