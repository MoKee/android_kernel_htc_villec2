/*
 * Copyright (C) 2010 HTC, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*
 * GPIO dump is output a buffer generated by some gpio-v2 function.
 * To output more than 4k bytes, we need transfering them one page by one page.
 *
 * DDR Reg dump is output sequential objects, all data will exceed 4k bytes.
 * we use seq_XXX api to output them.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <mach/board.h>

/* for GPIO dump */
#include <mach/gpio.h>		/* msm gpio */
#include <linux/mfd/pmic8058.h>	/* pmic8058 gpio & mpp */
#include <mach/mpp.h>		/* pm8901 mpp */


/* for Register dump */
#include <mach/msm_iomap.h>

#include "sysinfo-8x60.h"

/* For GPIO dump */
/* Need about 20000 to contain all data */
#define GPIO_INFO_BUFFER_SIZE	20000
/* For DDR Reg dump */
#define REG(x)	{#x, MSM_EBI1_CH0_##x##_ADDR}
#define REGI(x, n)	{#x "_" #n, MSM_EBI1_CH0_##x##_ADDR(n)}

#define CHECK_PROC_ENTRY(name, entry) do { \
				if (entry) { \
					pr_info("Create /proc/%s OK.\n", name); \
				} else { \
					pr_err("Create /proc/%s FAILED.\n", name); \
				} \
			} while (0);

/* For DDR Reg dump */
static int sys_ddr_regs_seq_open(struct inode *inode, struct file *file);

/* For DDR Reg dump */
struct _reg_data {
	const char *name;
	unsigned int *addr;
};

/* For GPIO dump */
static char gpio_info_buffer[GPIO_INFO_BUFFER_SIZE];

/* For DDR Reg dump */
struct _reg_data msm_ddr_registers[] = {
	#include "msm_ddr_regs-8x60.inc"
};

/* For DDR Reg dump */
struct file_operations sys_ddr_regs_proc_fops = {
	.owner = THIS_MODULE,
	.open = sys_ddr_regs_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int sys_gpio_read_proc(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	char *p = page;
	uint32_t len = 0;

	pr_info("%s: page=0x%08x, start=0x%08x, off=%d, count=%d, eof=%d, data=0x%08x\n", __func__,
			(uint32_t)page, (uint32_t)*start, (int)off, count, *eof, (uint32_t)data);

	/* for multi-page data, set *start to page */
	*start = page;

	if (!off) {
		/* for first time, dump to buffer */
		memset(gpio_info_buffer, 0, sizeof(gpio_info_buffer));
		len = 0;
		len = msm_dump_gpios(NULL, len, gpio_info_buffer);
		len = pm8xxx_dump_gpios(NULL, len, gpio_info_buffer);
		len = pm8xxx_dump_mpp(NULL, len, gpio_info_buffer);
		len = pm8901_dump_mpp(NULL, len, gpio_info_buffer);
		pr_info("%s: total bytes = %d.\n", __func__, len);
	}
	len = strlen(gpio_info_buffer + off);

	/* If there are data, transfer it. */
	if (len > 0) {
		/* data are moure than requested count, set it to count. */
		if (len >= count)
			len = count;
		pr_info("%s: transfer %d bytes.", __func__, len);
		memcpy(p, gpio_info_buffer + off, len);
	}

	/* If we have less data than count or no data, it's the end. */
	if (len < count) {
		pr_info("%s: end.", __func__);
		*eof = 1;
	}

	return len;
}

static void *sys_ddr_regs_seq_start(struct seq_file *seq, loff_t *pos)
{
	unsigned int number = sizeof(msm_ddr_registers) / sizeof(msm_ddr_registers[0]);

	void *ret = msm_ddr_registers + *pos;

	if (*pos == 0) {
		seq_printf(seq, " DDR Register Dump (Base: %08X, Count: %d):\n", (unsigned int)MSM_EBI1_CH0_BASE, number);
		seq_printf(seq, " %-30s | %-8s   | %-8s   | %-8s   \n", "Name", "Addr", "Offset", "Value");
		seq_printf(seq, "-----------------------------------------------------------\n");
	}

	if (*pos >= number)
		ret = NULL;

	return ret;
}

static void *sys_ddr_regs_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	unsigned int number = sizeof(msm_ddr_registers) / sizeof(msm_ddr_registers[0]);
	if (*pos + 1 < number) {
		*pos += 1;
		v = msm_ddr_registers + *pos;
		return v;
	}

	return NULL; /* NULL means no more data. */
}

static int sys_ddr_regs_seq_show(struct seq_file *seq, void *v)
{
	struct _reg_data *data = (struct _reg_data *)v;
	seq_printf(seq, " %-30s | 0x%08X | 0x%08X | 0x%08X \n",
			data->name,
			(unsigned int)data->addr,
			((unsigned int)data->addr - (unsigned int)MSM_EBI1_CH0_BASE),
			*data->addr);
	return 0;
}

static void sys_ddr_regs_seq_stop(struct seq_file *seq, void *v)
{
	if (v == NULL)
		seq_printf(seq, "-----------------------------------------------------------\n");
}

static struct seq_operations sys_ddr_regs_seq_ops = {
	.start = sys_ddr_regs_seq_start,
	.next = sys_ddr_regs_seq_next,
	.stop = sys_ddr_regs_seq_stop,
	.show = sys_ddr_regs_seq_show,
};

static int sys_ddr_regs_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &sys_ddr_regs_seq_ops);
}

void sysinfo_proc_init(void)
{
	struct proc_dir_entry *entry = NULL;

	/* FIXME: these proc objects should be put in /proc/sysinfo. */
	entry = create_proc_read_entry("emmc", 0, NULL, emmc_partition_read_proc, NULL);
	CHECK_PROC_ENTRY("emmc", entry);

	entry = create_proc_read_entry("gpio_info", 0, NULL, sys_gpio_read_proc, NULL);
	CHECK_PROC_ENTRY("gpio_info", entry);

	entry = create_proc_read_entry("processor_name", 0, NULL, processor_name_read_proc, NULL);
	CHECK_PROC_ENTRY("processor_name", entry);

	entry = create_proc_read_entry("dying_processes", 0, NULL, dying_processors_read_proc, NULL);
	CHECK_PROC_ENTRY("dying_processes", entry);

	entry = create_proc_entry("ddr_regs", 0, NULL);
	if (entry)
		entry->proc_fops = &sys_ddr_regs_proc_fops;
	CHECK_PROC_ENTRY("ddr_regs", entry);
}

