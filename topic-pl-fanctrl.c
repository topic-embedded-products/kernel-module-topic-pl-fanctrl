#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct topic_fanctrl_data {
	u32 __iomem *regs;
	u32 nr_fans;
};

static void topic_fanctrl_write_reg(struct topic_fanctrl_data *data, u32 index, u32 value)
{
	iowrite32(value, data->regs + index);
}

static u32 topic_fanctrl_read_reg(struct topic_fanctrl_data *data, u32 index)
{
	return ioread32(data->regs + index);
}

static void topic_fanctrl_init(struct topic_fanctrl_data *data, u32 speed)
{
	u32 i;

	topic_fanctrl_write_reg(data, 0, speed ? (1 << data->nr_fans) - 1 : 0);
	for (i = 0; i < data->nr_fans; ++i)
		topic_fanctrl_write_reg(data, i + 1, speed);
}

static int topic_fanctrl_read_fan(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct topic_fanctrl_data *data = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_fan_input:
		/* Register is time between tacho pulses in f=100MHz clocks */
		reg = topic_fanctrl_read_reg(data, 2 + data->nr_fans + channel);
		if (reg)
			/* Assume 2 pulses per round, then (60 * f) / (2 * r) rpm */
			*val = (long)(3000000000u / reg);
		else
			*val = 0;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t topic_fanctrl_fan_is_visible(
	const struct topic_fanctrl_data *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_fan_input:
		if (channel < data->nr_fans)
			return S_IRUGO;
		return 0;
	default:
		return 0;
	}
}

static int topic_fanctrl_read_pwm(struct device *dev, u32 attr, int channel,
				long *val)
{
	struct topic_fanctrl_data *data = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_pwm_enable:
		reg = topic_fanctrl_read_reg(data, 0);
		*val = (reg >> channel) & 0x01;
		return 0;
	case hwmon_pwm_input:
		reg = topic_fanctrl_read_reg(data, 1 + channel);
		*val = reg;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int topic_fanctrl_write_pwm(struct device *dev, u32 attr, int channel,
				 long val)
{
	struct topic_fanctrl_data *data = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_pwm_enable:
		reg = topic_fanctrl_read_reg(data, 0);
		if (val)
			reg |= BIT(channel);
		else
			reg &= ~BIT(channel);
		topic_fanctrl_write_reg(data, 0, reg);
		return 0;
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;
		topic_fanctrl_write_reg(data, 1 + channel, (u32)val);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t topic_fanctrl_pwm_is_visible(
	const struct topic_fanctrl_data *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_pwm_input:
	case hwmon_pwm_enable:
		if (channel < data->nr_fans)
			return S_IRUGO | S_IWUSR;
		return 0;
	default:
		return 0;
	}
}

static int topic_fanctrl_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return topic_fanctrl_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return topic_fanctrl_read_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int topic_fanctrl_write(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return topic_fanctrl_write_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t topic_fanctrl_is_visible(const void *context,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct topic_fanctrl_data *data = context;

	switch (type) {
	case hwmon_fan:
		return topic_fanctrl_fan_is_visible(data, attr, channel);
	case hwmon_pwm:
		return topic_fanctrl_pwm_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

/* Static structures support up to 3 fans */

static const u32 topic_fanctrl_fan_config[] = {
	HWMON_F_INPUT,
	HWMON_F_INPUT,
	HWMON_F_INPUT,
	0
};

static const struct hwmon_channel_info topic_fanctrl_fan = {
	.type = hwmon_fan,
	.config = topic_fanctrl_fan_config,
};


static const u32 topic_fanctrl_pwm_config[] = {
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	0
};

static const struct hwmon_channel_info topic_fanctrl_pwm = {
	.type = hwmon_pwm,
	.config = topic_fanctrl_pwm_config,
};

static const struct hwmon_channel_info *topic_fanctrl_info[] = {
	&topic_fanctrl_fan,
	&topic_fanctrl_pwm,
	NULL
};

static const struct hwmon_ops topic_fanctrl_hwmon_ops = {
	.is_visible = topic_fanctrl_is_visible,
	.read = topic_fanctrl_read,
	.write = topic_fanctrl_write,
};

static const struct hwmon_chip_info topic_fanctrl_chip_info = {
	.ops = &topic_fanctrl_hwmon_ops,
	.info = topic_fanctrl_info,
};

static int topic_fanctrl_probe(struct platform_device *pdev)
{
	struct topic_fanctrl_data *data;
	struct resource *res;
	struct device *hwmon_dev;
	u32 speed = 50;
	int err;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);

	/* Request I/O resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "I/O resource request failed\n");
		return -ENXIO;
	}
	res->flags &= ~IORESOURCE_CACHEABLE;
	data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->regs))
		return PTR_ERR(data->regs);

	err = of_property_read_u32(pdev->dev.of_node, "nr-fans",
				   &data->nr_fans);
	if (err) {
		dev_err(&pdev->dev, "nr-fans missing in devicetree\n");
		return -ENODEV;
	}

	err = of_property_read_u32(pdev->dev.of_node, "topic,initial-pwm",
				   &speed);
	if (err)
		dev_err(&pdev->dev, "topic,initial-pwm missing in "
			"devicetree, using default\n");

	topic_fanctrl_init(data, speed);

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "topicfan",
			data, &topic_fanctrl_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}


static struct of_device_id topic_fanctrlmatch[] = {
	{ .compatible = "topic,axi-pwm-fan-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, topic_fanctrlmatch);

static struct platform_driver topic_fanctrldriver = {
	.probe  = topic_fanctrl_probe,
	.driver = {
		.name = "topic_pl_fanctrl",
		.of_match_table = topic_fanctrlmatch,
	}
};
module_platform_driver(topic_fanctrldriver);

MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("Driver for FPGA AXI multiple fan controller");
MODULE_LICENSE("GPL v2");
