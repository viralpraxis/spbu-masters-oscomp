#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x8fc142cd, "blk_mq_start_request" },
	{ 0x92997ed8, "_printk" },
	{ 0x9a994cf7, "current_task" },
	{ 0x4629334c, "__preempt_count" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x48d88a2c, "__SCT__preempt_schedule" },
	{ 0x4ae5634f, "blk_mq_end_request" },
	{ 0x720a27a7, "__register_blkdev" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xf85923dd, "blk_mq_alloc_tag_set" },
	{ 0xeee0f39d, "blk_mq_init_queue" },
	{ 0x8f1c83a9, "blk_queue_logical_block_size" },
	{ 0x12657b71, "__blk_alloc_disk" },
	{ 0xa829ef51, "blk_mq_destroy_queue" },
	{ 0xe914e41e, "strcpy" },
	{ 0x28447b99, "set_capacity" },
	{ 0x4b3a8c01, "device_add_disk" },
	{ 0x893f80c5, "blk_mq_free_tag_set" },
	{ 0x999e8297, "vfree" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0xfd020264, "del_gendisk" },
	{ 0x8a6513dd, "put_disk" },
	{ 0x541a6db8, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "B04FA57487D95CFFFEDD288");
