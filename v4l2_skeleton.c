// SPDX-License-Identifier: GPL-2.0+

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>

MODULE_LICENSE("GPL v2");

static struct platform_device dummy_device = {
	.name = "v4l2_dummy_device",
	.id = PLATFORM_DEVID_NONE,
};

static int dummy_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);
	return 0;
}

static int dummy_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);
	return 0;
}

static struct platform_driver dummy_driver = {
	.probe = dummy_probe,
	.remove = dummy_remove,
	.driver = {
		.name = "v4l2_dummy_device",
		.owner = THIS_MODULE,
	},
};

static int __init skeleton_init(void)
{
	int ret;

	pr_info("%s\n", __func__);
	platform_device_register(&dummy_device);
	ret = platform_driver_probe(&dummy_driver, dummy_probe);
	return ret;
}

static void __exit skeleton_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&dummy_driver);
}

module_init(skeleton_init);
module_exit(skeleton_exit);

