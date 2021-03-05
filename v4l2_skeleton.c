// SPDX-License-Identifier: GPL-2.0+

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include<linux/types.h>
#include <linux/slab.h>
#include<linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include<media/v4l2-device.h>
#include<media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>

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

struct skeleton {
	struct platform_device *pdev;
	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct mutex lock;
	v4l2_std_id std;
	struct v4l2_dv_timings timings;
	struct v4l2_pix_format format;
	unsigned int input;
	struct vb2_queue queue;
	spinlock_t qlock;
	struct list_head buf_list;
	unsigned int field;
	unsigned int sequence;
};

struct skel_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static const struct v4l2_dv_timings_cap skel_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
};

#define SKEL_TVNORMS V4L2_STD_ALL

static int queue_setup(struct vb2_queue *vq, unsigned int *nbuffers, unsigned int *nplanes, unsigned int sizes[], struct device *alloc_devs[])
{
	struct skeleton *skel = vb2_get_drv_priv(vq);

	skel->field = skel->format.field;
	if (skel->field == V4L2_FIELD_ALTERNATE) {
		if (vb2_fileio_is_active(vq))
			return -EINVAL;
		skel->field = V4L2_FIELD_TOP;
	}
	if (vq->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - vq->num_buffers;
	if (*nplanes)
		return sizes[0] < skel->format.sizeimage ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = skel->format.sizeimage;
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct skeleton *skel = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = skel->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(&skel->pdev->dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct skeleton *skel = vb2_get_drv_priv(vb->vb2_queue);
	struct skel_buffer *buf = container_of(vbuf, struct skel_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&skel->qlock, flags);
	list_add_tail(&buf->list, &skel->buf_list);

	spin_unlock_irqrestore(&skel->qlock, flags);
}

static void return_all_buffers(struct skeleton *skel, enum vb2_buffer_state state)
{
	struct skel_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&skel->qlock, flags);
	list_for_each_entry_safe(buf, node, &skel->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&skel->qlock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct skeleton *skel = vb2_get_drv_priv(vq);
	int ret = 0;

	skel->sequence = 0;

	if (ret)
		return_all_buffers(skel, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct skeleton *skel = vb2_get_drv_priv(vq);

	return_all_buffers(skel, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops skel_qops = {
	.queue_setup = queue_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int skeleton_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	return 0;
}

static void skeleton_fill_pix_format(struct skeleton *skel, struct v4l2_pix_format *pix)
{
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	if (skel->input == 0) {
		pix->width = 720;
		pix->height = (skel->std & V4L2_STD_525_60) ? 480 : 576;
		pix->field = V4L2_FIELD_INTERLACED;
		pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	} else {
		pix->width = skel->timings.bt.width;
		pix->height = skel->timings.bt.height;
		if (skel->timings.bt.interlaced) {
			pix->field = V4L2_FIELD_ALTERNATE;
			pix->height /= 2;
		} else {
			pix->field = V4L2_FIELD_NONE;
		}
		pix->colorspace = V4L2_COLORSPACE_REC709;
	}
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->priv = 0;
}

static int skeleton_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct skeleton *skel = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->pixelformat != V4L2_PIX_FMT_YUYV)
		return -EINVAL;
	skeleton_fill_pix_format(skel, pix);
	return 0;
}

static int skeleton_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct skeleton *skel = video_drvdata(file);
	int ret;

	ret = skeleton_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;
	if (vb2_is_busy(&skel->queue))
		return -EBUSY;

	skel->format = f->fmt.pix;
	return 0;
}

static int skeleton_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct skeleton *skel = video_drvdata(file);

	f->fmt.pix = skel->format;
	return 0;
}

static int skeleton_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_YUYV;
	return 0;
}

static int skeleton_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input)
		return -ENODATA;
	if (std == skel->std)
		return 0;
	if (vb2_is_busy(&skel->queue))
		return -EBUSY;

	skel->std = std;
	skeleton_fill_pix_format(skel, &skel->format);
	return 0;
}

static int skeleton_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input)
		return -ENODATA;
	*std = skel->std;
	return 0;
}

static int skeleton_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input)
		return -ENODATA;
	return 0;
}

static int skeleton_s_dv_timings(struct file *file, void *_fh,
				 struct v4l2_dv_timings *timings)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input == 0)
		return -ENODATA;
	if (!v4l2_valid_dv_timings(timings, &skel_timings_cap, NULL, NULL))
		return -EINVAL;
	if (!v4l2_find_dv_timings_cap(timings, &skel_timings_cap, 0, NULL, NULL))
		return -EINVAL;
	if (v4l2_match_dv_timings(timings, &skel->timings, 0, false))
		return 0;
	if (vb2_is_busy(&skel->queue))
		return -EBUSY;

	skel->timings = *timings;
	skeleton_fill_pix_format(skel, &skel->format);
	return 0;
}

static int skeleton_g_dv_timings(struct file *file, void *_fh,  struct v4l2_dv_timings *timings)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input == 0)
		return -ENODATA;
	*timings = skel->timings;
	return 0;
}

static int skeleton_enum_dv_timings(struct file *file, void *_fh, struct v4l2_enum_dv_timings *timings)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input == 0)
		return -ENODATA;
	return v4l2_enum_dv_timings_cap(timings, &skel_timings_cap, NULL, NULL);
}

static int skeleton_query_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input == 0)
		return -ENODATA;
	return 0;
}

static int skeleton_dv_timings_cap(struct file *file, void *fh, struct v4l2_dv_timings_cap *cap)
{
	struct skeleton *skel = video_drvdata(file);

	if (skel->input == 0)
		return -ENODATA;
	*cap = skel_timings_cap;
	return 0;
}

static int skeleton_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	if (i->index > 1)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	if (i->index == 0) {
		i->std = SKEL_TVNORMS;
		strlcpy(i->name, "S-Video", sizeof(i->name));
		i->capabilities = V4L2_IN_CAP_STD;
	} else {
		i->std = 0;
		strlcpy(i->name, "HDMI", sizeof(i->name));
		i->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	}
	return 0;
}

static int skeleton_s_input(struct file *file, void *priv, unsigned int i)
{
	struct skeleton *skel = video_drvdata(file);

	if (i > 1)
		return -EINVAL;
	if (vb2_is_busy(&skel->queue))
		return -EBUSY;
	skel->input = i;
	skel->vdev.tvnorms = i ? 0 : SKEL_TVNORMS;
	skeleton_fill_pix_format(skel, &skel->format);
	return 0;
}

static int skeleton_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct skeleton *skel = video_drvdata(file);

	*i = skel->input;
	return 0;
}

static int skeleton_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops skel_ctrl_ops = {
	.s_ctrl = skeleton_s_ctrl,
};

static const struct v4l2_ioctl_ops skel_ioctl_ops = {
	.vidioc_querycap = skeleton_querycap,
	.vidioc_try_fmt_vid_cap = skeleton_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = skeleton_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = skeleton_g_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = skeleton_enum_fmt_vid_cap,
	.vidioc_g_std = skeleton_g_std,
	.vidioc_s_std = skeleton_s_std,
	.vidioc_querystd = skeleton_querystd,
	.vidioc_s_dv_timings = skeleton_s_dv_timings,
	.vidioc_g_dv_timings = skeleton_g_dv_timings,
	.vidioc_enum_dv_timings = skeleton_enum_dv_timings,
	.vidioc_query_dv_timings = skeleton_query_dv_timings,
	.vidioc_dv_timings_cap = skeleton_dv_timings_cap,
	.vidioc_enum_input = skeleton_enum_input,
	.vidioc_g_input = skeleton_g_input,
	.vidioc_s_input = skeleton_s_input,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_log_status = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations skeleton_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

static int dummy_probe(struct platform_device *pdev)
{
	static const struct v4l2_dv_timings timings_def = V4L2_DV_BT_CEA_1280X720P60;
	struct skeleton *drvdata;
	struct video_device *vdev;
	struct v4l2_ctrl_handler *hdl;
	struct vb2_queue *q;
	int ret;

	dev_info(&pdev->dev, "%s", __func__);
	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct skeleton), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "%s: Memory allocation failed..", __func__);
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "%s: Memory allocated for driver data..", __func__);
	drvdata->pdev = pdev;
	drvdata->timings = timings_def;
	drvdata->std = V4L2_STD_625_50;
	skeleton_fill_pix_format(drvdata, &drvdata->format);
	platform_set_drvdata(pdev, drvdata);
	ret = v4l2_device_register(&pdev->dev, &drvdata->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "%s: V4L2 device registration failed..", __func__);
		return ret;
	}
	dev_info(&pdev->dev, "%s: V4L2 device registration successful..", __func__);
	mutex_init(&drvdata->lock);
	hdl = &drvdata->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_CONTRAST, 0, 255, 1, 16);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_SATURATION, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &skel_ctrl_ops, V4L2_CID_HUE, -128, 127, 1, 0);
	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(&drvdata->ctrl_handler);
		v4l2_device_unregister(&drvdata->v4l2_dev);
		return ret;
	}
	drvdata->v4l2_dev.ctrl_handler = hdl;
	q = &drvdata->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->dev = &pdev->dev;
	q->drv_priv = drvdata;
	q->buf_struct_size = sizeof(struct skel_buffer);
	q->ops = &skel_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->lock = &drvdata->lock;
	q->gfp_flags = GFP_DMA32;
	ret = vb2_queue_init(q);
	if (ret) {
		v4l2_ctrl_handler_free(&drvdata->ctrl_handler);
		v4l2_device_unregister(&drvdata->v4l2_dev);
		return ret;
	}
	INIT_LIST_HEAD(&drvdata->buf_list);
	spin_lock_init(&drvdata->qlock);
	vdev = &drvdata->vdev;
	strlcpy(vdev->name, KBUILD_MODNAME, sizeof(vdev->name));
	vdev->release = video_device_release_empty;
	vdev->fops = &skeleton_fops;
	vdev->ioctl_ops = &skel_ioctl_ops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	vdev->lock = &drvdata->lock;
	vdev->queue = q;
	vdev->v4l2_dev = &drvdata->v4l2_dev;
	vdev->tvnorms = SKEL_TVNORMS;
	video_set_drvdata(vdev, drvdata);
	dev_info(&pdev->dev, "%s: video device registration..", __func__);
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		dev_err(&pdev->dev, "%s: video device registration failed..", __func__);
		v4l2_ctrl_handler_free(&drvdata->ctrl_handler);
		v4l2_device_unregister(&drvdata->v4l2_dev);
		video_device_release(&drvdata->vdev);
		return ret;
	}
	dev_info(&pdev->dev, "%s: video device registered successfully with minor number = %d", __func__, vdev->minor);
	return 0;
}

static int dummy_remove(struct platform_device *pdev)
{
	struct skeleton *drvdata;

	dev_info(&pdev->dev, "%s", __func__);
	drvdata = platform_get_drvdata(pdev);
	video_unregister_device(&drvdata->vdev);
	dev_info(&pdev->dev, "%s video device unregistered..", __func__);
	v4l2_ctrl_handler_free(&drvdata->ctrl_handler);
	dev_info(&pdev->dev, "%s v4l2 control handler free..", __func__);
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

