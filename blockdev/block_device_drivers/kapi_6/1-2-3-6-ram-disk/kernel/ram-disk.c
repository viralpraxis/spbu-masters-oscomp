/*
 * SO2 - Block device drivers lab (#7)
 * Linux - Exercise #1, #2, #3, #6 (RAM Disk)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

// #include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>

MODULE_DESCRIPTION("Simple RAM Disk");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");


#define KERN_LOG_LEVEL		KERN_ALERT

#define MY_BLOCK_MAJOR		240
#define MY_BLKDEV_NAME		"mybdev"
#define MY_BLOCK_MINORS		1
#define NR_SECTORS		128

#define EXIT_SUCCESS 0

#define KERNEL_SECTOR_SIZE	512

/* TODO 6: use bios for read/write requests */
#define USE_BIO_TRANSFER	1


static struct my_block_dev {
	struct blk_mq_tag_set tag_set;
	struct request_queue *queue;
	struct gendisk *gd;
	u8 *data;
	size_t size;
} g_dev;

static int my_block_open(struct block_device *bdev, fmode_t mode)
{
	return EXIT_SUCCESS;
}

static void my_block_release(struct gendisk *gd, fmode_t mode)
{
}

static const struct block_device_operations my_block_ops = {
	.owner = THIS_MODULE,
	.open = my_block_open,
	.release = my_block_release
};

static void my_block_transfer(struct my_block_dev *dev, sector_t sector,
		unsigned long len, char *buffer, int dir)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;

	/* check for read/write beyond end of block device */
	if ((offset + len) > dev->size)
		return;

	/* TODO 3: read/write to dev buffer depending on dir */
	if(dir){
		memcpy(dev->data + offset, buffer, len);
	} else {
		memcpy(buffer, dev->data + offset, len);
	}
}

/* to transfer data using bio structures enable USE_BIO_TRANFER */
#if USE_BIO_TRANSFER == 1
static void my_xfer_request(struct my_block_dev *dev, struct request *req)
{
	struct bio_vec bvec;
	struct req_iterator iter;
	/* TODO 6: iterate segments */
	rq_for_each_segment(bvec, req, iter){
		/* TODO 6: copy bio data to device buffer */
		sector_t sector = iter.iter.bi_sector;
		size_t len = bvec.bv_len;
		off_t offset = bvec.bv_offset;
		int dir = (int) bio_data_dir(iter.bio);
		char *buffer = kmap_atomic(bvec.bv_page);
		printk(KERN_LOG_LEVEL "%s: buf %8p offset %lu len %u", __func__, buffer, offset, len, dir);
		my_block_transfer(dev, sector, len, buffer+offset, dir);
		kunmap_atomic(buffer);
	}
}
#endif

static blk_status_t my_block_request(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	struct request *rq;
	struct my_block_dev *dev = hctx->queue->queuedata;
	char *direction;

	/* TODO 2: get pointer to request */
	rq = bd->rq;
	/* TODO 2: start request processing. */
	blk_mq_start_request(rq);
	/* TODO 2: check fs request. Return if passthrough. */
	if(blk_rq_is_passthrough(rq)){
		printk(KERN_NOTICE "skip non-fs request");
		blk_mq_end_request(rq, BLK_STS_IOERR);
		goto out;
	}
	/* TODO 2: print request information */
	if (rq_data_dir(rq)) {
		direction = "w";
	} else {
		direction = "r";
	}

	printk(KERN_LOG_LEVEL "blockdev: pos: %llu; bytes=%u; dir=%c\n", blk_rq_pos(rq), blk_rq_cur_bytes(rq), direction);
#if USE_BIO_TRANSFER == 1
	/* TODO 6: process the request by calling my_xfer_request */
	my_xfer_request(dev, rq);
#else
	/* TODO 3: process the request by calling my_block_transfer */
	my_block_transfer(dev, blk_rq_pos(rq),
			blk_rq_bytes(rq),
			bio_data(rq->bio), rq_data_dir(rq));
#endif

	/* TODO 2: end request successfully */
	blk_mq_end_request(rq, BLK_STS_OK);
out:
	return BLK_STS_OK;
}

static struct blk_mq_ops my_queue_ops = {
	.queue_rq = my_block_request,
};

static int create_block_device(struct my_block_dev *dev)
{
	int err;

	dev->size = NR_SECTORS * KERNEL_SECTOR_SIZE;
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		printk(KERN_ERR "vmalloc: out of memory\n");
		err = -ENOMEM;
		goto out_vmalloc;
	}

	/* Initialize tag set. */
	dev->tag_set.ops = &my_queue_ops;
	dev->tag_set.nr_hw_queues = 1;
	dev->tag_set.queue_depth = 128;
	dev->tag_set.numa_node = NUMA_NO_NODE;
	dev->tag_set.cmd_size = 0;
	dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	err = blk_mq_alloc_tag_set(&dev->tag_set);
	if (err) {
	    printk(KERN_LOG_LEVEL "blk_mq_alloc_tag_set: can't allocate tag set\n");
	    goto out_alloc_tag_set;
	}

	/* Allocate queue. */
	dev->queue = blk_mq_init_queue(&dev->tag_set);
	if (IS_ERR(dev->queue)) {
		printk(KERN_LOG_LEVEL "blk_mq_init_queue: out of memory\n");
		err = -ENOMEM;
		goto out_blk_init;
	}
	blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);
	dev->queue->queuedata = dev;

	/* initialize the gendisk structure */
	dev->gd = blk_alloc_disk(NUMA_NO_NODE); // kapi 6
	if (!dev->gd) {
		printk(KERN_LOG_LEVEL "alloc_disk: failure\n");
		err = -ENOMEM;
		goto out_alloc_disk;
	}

	dev->gd->major = MY_BLOCK_MAJOR;
	dev->gd->first_minor = 0;
	dev->gd->minors = MY_BLOCK_MINORS; // kapi 6
	dev->gd->fops = &my_block_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf(dev->gd->disk_name, DISK_NAME_LEN, "myblock");
	set_capacity(dev->gd, NR_SECTORS);

	err = add_disk(dev->gd);
	if (err) {
		printk(KERN_ERR "add_disk: failure\n");
		return err;
	}

	return EXIT_SUCCESS;

out_alloc_disk:
	blk_mq_destroy_queue(dev->queue);
out_blk_init:
	blk_mq_free_tag_set(&dev->tag_set);
out_alloc_tag_set:
	vfree(dev->data);
out_vmalloc:
	return err;
}

static int __init my_block_init(void)
{
	int err = 0;

	/* TODO 1: register block device */
	err = register_blkdev(MY_BLOCK_MAJOR, MY_BLKDEV_NAME);
	if(err<0){
		printk(KERN_ERR "unable to register mybdev block device\n");
		return -EBUSY;
	}
	/* TODO 2: create block device using create_block_device */
	err = create_block_device(&g_dev);
	if(err){
		printk("[block_init] Error creating block device");
		goto out;
	}

  printk(KERN_INFO "mybdev: initialized\n");
	return EXIT_SUCCESS;

out:
	/* TODO 2: unregister block device in case of an error */
	unregister_blkdev(MY_BLOCK_MAJOR, MY_BLKDEV_NAME);
	return err;
}

static void delete_block_device(struct my_block_dev *dev)
{
	if (dev->gd) {
		del_gendisk(dev->gd);
		put_disk(dev->gd);
	}

	if (dev->queue)
		blk_mq_destroy_queue(dev->queue);
	if (dev->tag_set.tags)
		blk_mq_free_tag_set(&dev->tag_set);
	if (dev->data)
		vfree(dev->data);
}

static void __exit my_block_exit(void)
{
	/* TODO 2: cleanup block device using delete_block_device */
	delete_block_device(&g_dev);
	/* TODO 1: unregister block device */
	unregister_blkdev(MY_BLOCK_MAJOR, MY_BLKDEV_NAME);
}

module_init(my_block_init);
module_exit(my_block_exit);