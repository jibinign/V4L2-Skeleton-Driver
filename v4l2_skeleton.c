// SPDX-License-Identifier: GPL-2.0+

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include<linux/types.h>
#include <linux/slab.h>
#include<linux/videodev2.h>
#include<media/v4l2-device.h>
#include<media/v4l2-dev.h>

MODULE_LICENSE("GPL v2");

static void dummy_device_release(struct device *dev)
{
	dev_info(dev, "%s", __func__);
}

static struct platform_device dummy_device = {
	.name = KBUILD_MODNAME,
	.id = PLATFORM_DEVID_NONE,
	.dev = {
		.release = dummy_device_release,
	},
};

struct skeleton_data {
	struct platform_device *pdev;
	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	int data;
};

static int skeleton_open(struct file *filp)
{
	pr_info("%s", __func__);
	return 0;
}

static int skeleton_release(struct file *filp)
{
	pr_info("%s", __func__);
	return 0;
}

static ssize_t skeleton_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	pr_info("%s", __func__);
	return 0;
}

static ssize_t skeleton_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	pr_info("%s", __func__);
	return 0;
}

static long skeleton_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	pr_info("%s", __func__);
	return 0;
}

static int skeleton_mmap(struct file *filp, struct vm_area_struct *vma)
{
	pr_info("%s", __func__);
	return 0;
}

static struct v4l2_file_operations skeleton_fops = {
	.owner = THIS_MODULE,
	.open = skeleton_open,
	.release = skeleton_release,
	.read = skeleton_read,
	.write = skeleton_write,
	.mmap = skeleton_mmap,
	.unlocked_ioctl = skeleton_ioctl,
};

static int dummy_probe(struct platform_device *pdev)
{
	struct skeleton_data *drvdata;
	struct video_device *vdev;
	int ret;

	dev_info(&pdev->dev, "%s", __func__);
	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct skeleton_data), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "%s: Memory allocation failed..", __func__);
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "%s: Memory allocated for driver data..", __func__);
	drvdata->pdev = pdev;
	dev_set_drvdata(&pdev->dev, drvdata);
	ret = v4l2_device_register(&pdev->dev, &drvdata->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "%s: V4L2 device registration failed..", __func__);
		return ret;
	}
	dev_info(&pdev->dev, "%s: V4L2 device registration successful..", __func__);
	vdev = &drvdata->vdev;
	strlcpy(vdev->name, KBUILD_MODNAME, sizeof(vdev->name));
	vdev->release = video_device_release_empty;
	vdev->fops = &skeleton_fops;
	vdev->v4l2_dev = &drvdata->v4l2_dev;
	video_set_drvdata(vdev, drvdata);
	dev_info(&pdev->dev, "%s: video device registration..", __func__);
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		dev_err(&pdev->dev, "%s: video device registration failed..", __func__);
		video_device_release(&drvdata->vdev);
		return ret;
	}
	dev_info(&pdev->dev, "%s: video device registered successfully with minor number = %d", __func__, vdev->minor);
	return 0;
}

static int dummy_remove(struct platform_device *pdev)
{
	struct skeleton_data *drvdata;

	dev_info(&pdev->dev, "%s", __func__);
	drvdata = platform_get_drvdata(pdev);
	video_unregister_device(&drvdata->vdev);
	dev_info(&pdev->dev, "%s video device unregistered..", __func__);
	v4l2_device_unregister(&drvdata->v4l2_dev);
	dev_info(&pdev->dev, "%s v4l2 device unregistered..", __func__);
	return 0;
}

static struct platform_driver dummy_driver = {
	.probe = dummy_probe,
	.remove = dummy_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
};

static int skeleton_init(void)
{
	int ret;

	pr_info("%s", __func__);
	platform_device_register(&dummy_device);
	pr_info("%s: platform device, %s is registered..", __func__, dummy_device.name);
	ret = platform_driver_probe(&dummy_driver, dummy_probe);
	if (ret)
		pr_err("%s: platform driver, %s is not registered..", __func__, dummy_driver.driver.name);
	else
		pr_info("%s: platform driver,%s registered successfully..", __func__, dummy_driver.driver.name);
	return ret;
}

static void skeleton_exit(void)
{
	pr_info("%s", __func__);
	pr_info("%s: platform driver, %s is going to be unregistered..", __func__, dummy_driver.driver.name);
	platform_driver_unregister(&dummy_driver);
	pr_info("%s: platform device, %s is going to be unregistered..", __func__, dummy_device.name);
	platform_device_unregister(&dummy_device);
}

module_init(skeleton_init);
module_exit(skeleton_exit);

