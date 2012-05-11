/*  battery.c - A module used to obtain battery voltage from the ADC pin of the 
 *              UCB1x00 touchscreen/audio device
 *
 *  Copyright (C) 2007 by Angelo Failla - http://www.spmc.mobi
 *
 */

/* Kernel Programming */

#ifndef MODULE
#define MODULE
#endif

#ifndef LINUX
#define LINUX
#endif

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_ALERT */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/moduleparam.h>  /* Needed for the moduleparam macros */
#include <linux/proc_fs.h>	    /* Necessary because we use the proc fs */
#include <asm/uaccess.h>	    /* Needed for copy_from_user */
#include <asm/atomic.h>         /* Needed for atomic_set, atomic_read, etc... */
#include <ucb1x00.h>
#include "macros.h"

#ifdef ARM_PXA
	/* PXA279 header for convertion functions between irqs numbers and gpio numbers */
	#include <asm-arm/arch-pxa/irqs.h> 
	/* PXA270 header for GPIO macros and functions */
	#include <asm-arm/arch-pxa/pxa-regs.h>
	/* PXA270 header for irqs types */
	#include <asm-arm/arch-pxa/hardware.h>
	/* Colibri specific header */
	#include <asm-arm/arch-pxa/colibri.h>
#endif

#define DRIVER_AUTHOR     "Angelo Failla <angelo.failla@spmc.mobi>"
#define DRIVER_DESC       "A module used to obtain battery voltage from the ADC pin of the UCB1x00 touchscreen/audio device"
#define MODULE_NAME       "colibri-battery"

#define PROCFS_NAME       "battery"
#define PROCFS_MAX_SIZE	  1024

struct proc_dir_entry *proc = NULL;

// The buffer used to store characters written to the procfs entry
static char procfs_buffer[PROCFS_MAX_SIZE];
// The size of the buffer above
static unsigned long procfs_buffer_size = 0;

/* function prototypes */
int n_atoi(char *str);
int proc_read(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);
int init_module(void);
void cleanup_module(void);

/* this function is called each time that a userland process read the
 * /proc/battery proc entry */
int proc_read(char *buffer, char **buffer_location, off_t offset,
		int buffer_length, int *eof, void *data)
{
	int ret;
	unsigned int val;


	if (offset > 0) {
		ret  = 0;
	} else {
		// Init ucb1x00
		struct ucb1x00 *ucb = ucb1x00_get();
		// this function lock che ucb1x00, it prevents race conditions
		ucb1x00_adc_enable(ucb);
		// Read from ADC 0, val is an unsigned int
		// from 0 to 1023
		val = ucb1x00_adc_read(ucb, UCB_ADC_INP_AD0, 0);
		// this function unlock che ucb1x00, it prevents race conditions
		ucb1x00_adc_disable(ucb);
		// percent value must be obtained calculating this proportion => 1023 : 100 = x : val 
		ret = sprintf(buffer, "%d%%\n", val*1023/100);
	}

	return ret;
}

// my implementation of atoi
int n_atoi(char *str) {

	int res = 0;
	int mul = 1;
	char *ptr;

	for (ptr = str + strlen(str) - 1; ptr >= str; ptr--) { 
		if (*ptr < '0' || *ptr > '9')
			return (-1);
		res += (*ptr - '0') * mul;
		mul *= 10;
	}

	return (res);
} 

// the module first called function
int init_module(void) {

	int rc;

	printk(KERN_INFO MODULE_NAME ": Starting module...\n");

	proc = create_proc_entry(PROCFS_NAME, 0644, NULL);
	if (proc == NULL) {
		remove_proc_entry(PROCFS_NAME, &proc_root);
		printk(KERN_ALERT MODULE_NAME "Error: Could not initialize /proc/%s\n",
				PROCFS_NAME);
		return -ENOMEM;
	}

	proc->read_proc  = proc_read;
	proc->owner	     = THIS_MODULE;
	proc->mode 	     = S_IFREG | S_IRUGO;
	proc->uid 	     = 0;
	proc->gid 	     = 0;
	proc->size 	     = 37;

    PDEBUG("/proc/%s created\n", PROCFS_NAME);	

	// A non 0 return means init_module failed; module can't be loaded.
	return 0;

}

// the module last called function
void cleanup_module(void) {

	remove_proc_entry(PROCFS_NAME, &proc_root);

	printk(KERN_INFO MODULE_NAME ": Stopping module...\n");

}  

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);		/* Who wrote this module? */
MODULE_DESCRIPTION(DRIVER_DESC);	/* What does this module do */
