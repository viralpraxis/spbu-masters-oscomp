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
	{ 0x4629334c, "__preempt_count" },
	{ 0x9a994cf7, "current_task" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x48d88a2c, "__SCT__preempt_schedule" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x7e6a3a64, "fs_bio_set" },
	{ 0xabf2e9ba, "bio_alloc_bioset" },
	{ 0xe64b43f, "alloc_pages" },
	{ 0x888d54d5, "bio_add_page" },
	{ 0xb00190c6, "submit_bio_wait" },
	{ 0x92997ed8, "_printk" },
	{ 0x80738cc9, "bio_put" },
	{ 0xeed3c484, "__free_pages" },
	{ 0x84374d5f, "blkdev_get_by_path" },
	{ 0x77396456, "blkdev_put" },
	{ 0x541a6db8, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "217D770325F115CD9262C4B");
