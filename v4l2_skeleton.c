// SPDX-License-Identifier: GPL-2.0+

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL v2");

static struct platform_device dummy_device = {
	.name = "v4l2_dummy_device",
	.id = PLATFORM_DEVID_NONE,
};

struct driver_data {
	int data;
};

static int dummy_probe(struct platform_device *pdev)
{
	struct driver_data *drvdata;

	dev_info(&pdev->dev, "%s", __func__);
	drvdata = kzalloc(sizeof(struct driver_data), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "%s: Memory allocation failed..", __func__);
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "%s: Memory allocated for driver data..", __func__);
	platform_set_drvdata(pdev, drvdata);
	return 0;
}

static int dummy_remove(struct platform_device *pdev)
{
	struct driver_data *drvdata;

	dev_info(&pdev->dev, "%s", __func__);
	drvdata = platform_get_drvdata(pdev);
	if (drvdata) {
		platform_set_drvdata(pdev, NULL);
		dev_info(&pdev->dev, "%s: Free allocated memory..", __func__);
		kfree(drvdata);
	}
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

	pr_info("%s", __func__);
	platform_device_register(&dummy_device);
	ret = platform_driver_probe(&dummy_driver, dummy_probe);
	if (ret)
		pr_err("%s: dummy device is not registered..", __func__);
	else
		pr_info("%s: dummy device registered successfully..", __func__);
	return ret;
}

static void __exit skeleton_exit(void)
{
	pr_info("%s", __func__);
	platform_driver_unregister(&dummy_driver);
}

module_init(skeleton_init);
module_exit(skeleton_exit);

