// SPDX-License-Identifier: GPL-2.0+

#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL v2");

static int __init skeleton_init(void)
{
	pr_info("%s\n", __func__);
	return 0;
}

static void __exit skeleton_exit(void)
{
	pr_info("%s\n", __func__);
}

module_init(skeleton_init);
module_exit(skeleton_exit);

