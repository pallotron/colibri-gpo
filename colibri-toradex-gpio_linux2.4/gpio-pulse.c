/*  gpio.c - A sample module that use the Toradex Colibri PXA270 GPIOs
 *
 *  Copyright (C) 2007 by Angelo Failla - http://www.spmc.mobi
 *
 * Good informations used to write this module:
 *
 * - http://raph.people.8d.com/arm-linux-notes.html
 * - http://fghhgh.150m.com/proc_gpio.c.txt
 * - http://docwiki.gumstix.org/GPIO_event
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
#include <linux/interrupt.h>    /* Needed for request_irq */
#include <asm/atomic.h>         /* Needed for atomic_set, atomic_read, etc... */
#include <linux/timer.h>        /* Needed for timer functions */

#include "macros.h"             /* Common macros */

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
#define DRIVER_DESC       "A module that use the Toradex Colibri PXA270 GPIOs"
#define MODULE_NAME       "gpio"

#define PROCFS_PID_NAME   "gpio/pid"
#define PROCFS_CMD_NAME   "gpio/cmd"
#define PROCFS_MAX_SIZE	  1024

#define GPIO_IN_STREAM    19
#define GPIO_OUT_MODEM    12
#define GPIO_OUT_GREEN    14

/* these are the default gpios to be monitored
 *
 * gpio19 in -> start/stop stream (send sigusr1/2 signal to userland pid)
 * gpio12 out -> when the module starts it stays up for at least 500ms to turn on the modem
 * gpio14 out -> streaming/link status, on/off
 */
static int gpio_in_streamctl = GPIO_IN_STREAM;
static int gpio_out_modem    = GPIO_OUT_MODEM;
static int gpio_out_green    = GPIO_OUT_GREEN;

/*
 * module_param_array(name, type, num, perm);
 * The first param is the parameter's (in this case the array's) name
 * The second param is the data type of the elements of the array
 * The third argument is a pointer to the variable that will store the number 
 * of elements of the array initialized by the user at module loading time
 * The fourth argument is the permission bits
 */ 
MODULE_PARM(gpio_in_streamctl, "i");
MODULE_PARM_DESC(gpio_in_streamctl, " The gpio pin used to start/stop the stream.");
MODULE_PARM(gpio_out_modem, "i");
MODULE_PARM_DESC(gpio_out_modem, " The gpio pin used to power on/off the modem.");
MODULE_PARM(gpio_out_green, "i");
MODULE_PARM_DESC(gpio_out_green, " The gpio pin used to power on/off or blink the green led.");
MODULE_PARM(gpio_in_adc, "i");
MODULE_PARM_DESC(gpio_in_adc, " The gpio pin used to read the battery voltage.");

struct proc_dir_entry *proc_pid    = NULL;
struct proc_dir_entry *proc_cmd    = NULL ;
struct proc_dir_entry *proc_parent = NULL;

// The buffer used to store characters written to the procfs entry
static char procfs_buffer[PROCFS_MAX_SIZE];
// The size of the buffer above
static unsigned long procfs_buffer_size = 0;

// the pid of the process that must be advised 
static unsigned int process_pid = 0;
module_param(process_pid, int, 0);
MODULE_PARM_DESC(process_pid, " An integer representing the pid of the process to be advised");

/* this is used as devid in the handler */
static struct irq_data {
	unsigned long seconds;
	unsigned long count;
} data;

static struct pdata {
	int gpio;
	unsigned int num_pulses;;
	unsigned long period;
	unsigned long duration;
} pulse_data;

atomic_t irq_atomic_var_stream = ATOMIC_INIT(0); 
atomic_t atomic_var_pulse      = ATOMIC_INIT(0); 

static struct timer_list jiq_timer;
static struct timer_list jiq_pulse_timer;
static unsigned long tmp_jiffie = 0;
wait_queue_head_t wait;

/* functions prototypes */
int n_atoi(char *str);
void start_stream(void);
void stop_stream(void);
void turn_on_gpio(int gpio);
void turn_off_gpio(int gpio);
void pulse_led(void);
int proc_pid_read(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);
int proc_pid_write(struct file *file, const char *buffer, unsigned long count, void *data);
int proc_cmd_write(struct file *file, const char *buffer, unsigned long count, void *data);
void irq_handler(int irq, void *dev_id, struct pt_regs *regs);
int init_module(void);
void cleanup_module(void);


/* this function is called each time that a userland process read the
 * /proc/gpio/pid proc entry */
int proc_pid_read(char *buffer, char **buffer_location, off_t offset,
		int buffer_length, int *eof, void *data)
{
	int ret;

	if (offset > 0) {
		ret  = 0;
	} else {
		/* fill the buffer, return the buffer size */
		ret = sprintf(buffer, "%d\n", process_pid);
	}

	return ret;
}

/* this function is called each time that a userland process write to the
 * /proc/gpio/pid proc entry */
int proc_pid_write(struct file *file, const char *buffer, unsigned long count,
			   void *data)
{
	int i;
	
	/* get buffer size */
	procfs_buffer_size = count;
	if (procfs_buffer_size > PROCFS_MAX_SIZE ) {
		procfs_buffer_size = PROCFS_MAX_SIZE;
	}

	// clean the kernel buffer
	for(i=0; i<PROCFS_MAX_SIZE; i++)
		 procfs_buffer[i]='\0';

	// write data from the userland buffer to the kernel buffer
	if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
		return -EFAULT;
	}

	// in case the last char is \n convert it to \0
	if(procfs_buffer[procfs_buffer_size-1] == '\n')
		procfs_buffer[procfs_buffer_size-1] = '\0';

	// convert the string to an integer using my atoi function
	// TODO: better data validation
	process_pid = n_atoi(procfs_buffer);
				
	return procfs_buffer_size;
}

/* this function is called each time that a userland process write to the
 * /proc/gpio/cmd proc entry */
int proc_cmd_write(struct file *file, const char *buffer, unsigned long count,
			   void *data)
{
	int i;
	char *token = NULL;
	
	/* get buffer size */
	procfs_buffer_size = count;
	if (procfs_buffer_size > PROCFS_MAX_SIZE ) {
		procfs_buffer_size = PROCFS_MAX_SIZE;
	}

	// clean the kernel buffer
	for(i=0; i<PROCFS_MAX_SIZE; i++)
		 procfs_buffer[i]='\0';

	// write data from the userland buffer to the kernel buffer
	if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
		return -EFAULT;
	}

	// in case the last char is \n convert it to \0
	if(procfs_buffer[procfs_buffer_size-1] == '\n')
		procfs_buffer[procfs_buffer_size-1] = '\0';

	if(strcmp(procfs_buffer,"start")==0) {
		start_stream();
	} else if(strcmp(procfs_buffer,"stop")==0) {
		stop_stream();
	} else if(strcmp(procfs_buffer,"modemoff")==0) {
		PDEBUG("Shutting down modem.\n");
		/* turn on gpio for 4 seconds, this shutdown the modem */
		turn_on_gpio(gpio_out_modem);
		init_timer(&jiq_timer);
		jiq_timer.function = (void*)turn_off_gpio;
		jiq_timer.data = (int)gpio_out_modem;
		jiq_timer.expires = jiffies + HZ * 5; /* 5000ms == 5 seconds */
		add_timer(&jiq_timer);
	} else if(strcmp(procfs_buffer,"modemon")==0) {
		PDEBUG("Power on modem.\n");
		/* turn on gpio for 4 seconds, this shutdown the modem */
		turn_on_gpio(gpio_out_modem);
		init_timer(&jiq_timer);
		jiq_timer.function = (void*)turn_off_gpio;
		jiq_timer.data = (int)gpio_out_modem;
		jiq_timer.expires = jiffies + HZ / 2 ; /* 500ms */
		add_timer(&jiq_timer);
	}

	token = strtok(procfs_buffer, "=");
	if(token != NULL) {
		if(strcmp(token, "ledG") == 0) {
			token = strtok(NULL, "=");
			switch(n_atoi(token)) {
				case 0: // led off
					if(atomic_read(&atomic_var_pulse) == 1) {
						del_timer(&jiq_pulse_timer);
					}
					PDEBUG("Turning green led off.\n");
					turn_on_gpio(gpio_out_green);
					break;
				case 1: // led on
					if(atomic_read(&atomic_var_pulse) == 1) {
						del_timer(&jiq_pulse_timer);
					}
					PDEBUG("Turning green led on.\n");
					turn_off_gpio(gpio_out_green);
					break;
				case 2: // led blink slow -> period: 1s; pulse duration: 0.1s; timing: 1000000000
					pulse_data.gpio = gpio_out_green;
					pulse_data.duration = HZ/10;
					pulse_data.period = HZ;
					pulse_data.num_pulses = 1;
					PDEBUG("Blinking green led slow, period: %lu, duration: %lu, num_pulses: %d.\n",pulse_data.period,pulse_data.duration,pulse_data.num_pulses);
					if(atomic_read(&atomic_var_pulse) == 1) {
						del_timer(&jiq_pulse_timer);
					}
					atomic_set(&atomic_var_pulse,1);
					pulse_led();
					break;
				case 3: // led blink fast -> period: 0.5s; pulse duration: 0.1s; timing: 10000
					pulse_data.gpio = gpio_out_green;
					pulse_data.duration = HZ/10;
					pulse_data.period = HZ/2;
					pulse_data.num_pulses = 1;
					PDEBUG("Blinking green led slow, period: %lu, duration: %lu, num_pulses: %d.\n",pulse_data.period,pulse_data.duration,pulse_data.num_pulses);
					if(atomic_read(&atomic_var_pulse) == 1) {
						del_timer(&jiq_pulse_timer);
					}
					atomic_set(&atomic_var_pulse,1);
					pulse_led();
					break;
				case 4: // led blink double pulse -> period: 1s; pulse duration: 0.1s; timing: 1010000000
					pulse_data.gpio = gpio_out_green;
					pulse_data.duration = HZ/10;
					pulse_data.period = HZ;
					pulse_data.num_pulses =  2;
					PDEBUG("Blinking green led slow, period: %lu, duration: %lu, num_pulses: %d.\n",pulse_data.period,pulse_data.duration,pulse_data.num_pulses);
					if(atomic_read(&atomic_var_pulse) == 1) {
						del_timer(&jiq_pulse_timer);
					}
					atomic_set(&atomic_var_pulse,1);
					pulse_led();
					break;
			}
		}
	}

	return procfs_buffer_size;
}

void pulse_led(void) {

	int i = 0;
	unsigned long j = 0;

	if(atomic_read(&atomic_var_pulse) == 0) 
		return;

	// how many pulses we must do before waiting for a period
	for(i=0; i<pulse_data.num_pulses; i++) {
		// turn on led: reverse logic
		turn_off_gpio(pulse_data.gpio);
		j = jiffies + pulse_data.duration;
		while (time_before(jiffies, j));
		// turn off led: reverse logic 
		turn_on_gpio(pulse_data.gpio);
		j = jiffies + pulse_data.duration;
		while (time_before(jiffies, j));
	}

	// schedule a recursive call to pulse_led function withing p.period of time
	init_timer(&jiq_pulse_timer);
	jiq_pulse_timer.function = (void*)pulse_led;
	jiq_pulse_timer.expires = jiffies + pulse_data.period;
	add_timer(&jiq_pulse_timer);
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

void start_stream(void) {
	PDEBUG("Start stream. Sending SIGUSR1 to process pid: %d\n", process_pid);
	kill_proc(process_pid, SIGUSR1, 0);
	atomic_set(&irq_atomic_var_stream, 1);
}

void stop_stream(void) {
	PDEBUG("Stop stream. Sending SIGUSR2 to process pid: %d\n", process_pid);
	kill_proc(process_pid, SIGUSR2, 0);
	atomic_set(&irq_atomic_var_stream, 0);
}

void irq_handler(int irq, void *dev_id, struct pt_regs *regs) {
	
	PDEBUG("IRQ %i: got event\n", irq);

	if(jiffies - tmp_jiffie > HZ) {
		tmp_jiffie = jiffies;
		if(atomic_read(&irq_atomic_var_stream) == 0) {
			start_stream();
		} else {
			stop_stream();
		}
	}

}

void turn_on_gpio(int gpio) {
	set_GPIO_mode(gpio | GPIO_OUT);
	GPSR(gpio) = GPIO_bit(gpio);
	if (GPLR(gpio) & GPIO_bit(gpio)) {
		PDEBUG("LED gpio %d on\n", gpio);
	} else {
		PDEBUG("LED gpio %d off\n", gpio);
	}
}

void turn_off_gpio(int gpio) {
	set_GPIO_mode(gpio | GPIO_OUT);
	GPCR(gpio) = GPIO_bit(gpio);
	if (GPLR(gpio) & GPIO_bit(gpio)) {
		PDEBUG("LED gpio %d on\n", gpio);
	} else {
		PDEBUG("LED gpio %d off\n", gpio);
	}
}

// the module first called function
int init_module(void) {

	int rc;

	printk(KERN_INFO MODULE_NAME ": Starting gpio module...\n");

	/* turn on gpio for 500ms, than turn off */
	turn_on_gpio(gpio_out_modem);
	init_timer(&jiq_timer);
	jiq_timer.function = (void*)turn_off_gpio;
	jiq_timer.data = (int)gpio_out_modem;
	//jiq_timer.expires = jiffies + HZ / 2; /* 500ms */
	jiq_timer.expires = jiffies + HZ; /* 1000ms */
	add_timer(&jiq_timer);

	// turn off green led: reverse logic
	turn_on_gpio(gpio_out_green);

	// an interrupt handler must be hooked to gpio_in_streamctl pin
	// Configure the interrupt type for this GPIO 
	set_GPIO_IRQ_edge(gpio_in_streamctl,GPIO_RISING_EDGE);
	PDEBUG("Registering irq %d for GPIO %d.\n", IRQ_GPIO(gpio_in_streamctl), gpio_in_streamctl);
	// use request_irq (from include/linux/interrupt.h) to register the interrupt handler
	if (( rc = request_irq(IRQ_GPIO(gpio_in_streamctl), irq_handler, SA_SHIRQ, "gpio", &data)) != 0 ) {
		printk(KERN_ALERT "Unable to register irq %d for GPIO %d. Return value: %d\n", IRQ_GPIO(gpio_in_streamctl), gpio_in_streamctl, rc);
	}

	PDEBUG("process_pid: %d.\n", process_pid);

	proc_parent = create_proc_entry("gpio", S_IFDIR | S_IRUGO | S_IXUGO, NULL);
	if(!proc_parent) return -ENOMEM;
	
	proc_pid = create_proc_entry(PROCFS_PID_NAME, 0644, NULL);
	if (proc_pid == NULL) {
		remove_proc_entry(PROCFS_PID_NAME, &proc_root);
		printk(KERN_ALERT MODULE_NAME "Error: Could not initialize /proc/%s\n",
				PROCFS_PID_NAME);
		return -ENOMEM;
	}

	proc_cmd = create_proc_entry(PROCFS_CMD_NAME, 0644, NULL);
	if (proc_cmd == NULL) {
		remove_proc_entry(PROCFS_CMD_NAME, &proc_root);
		printk(KERN_ALERT MODULE_NAME "Error: Could not initialize /proc/%s\n",
				PROCFS_CMD_NAME);
		return -ENOMEM;
	}

	proc_pid->read_proc  = proc_pid_read;
	proc_pid->write_proc = proc_pid_write;
	proc_pid->owner	     = THIS_MODULE;
	proc_pid->mode 	     = S_IFREG | S_IRUGO;
	proc_pid->uid 	     = 0;
	proc_pid->gid 	     = 0;
	proc_pid->size 	     = 37;

    PDEBUG("/proc/%s created\n", PROCFS_PID_NAME);	

	proc_cmd->write_proc = proc_cmd_write;
	proc_cmd->owner	     = THIS_MODULE;
	proc_cmd->mode 	     = S_IFREG | S_IRUGO;
	proc_cmd->uid 	     = 0;
	proc_cmd->gid 	     = 0;
	proc_cmd->size 	     = 37;
		
	PDEBUG("/proc/%s created\n", PROCFS_CMD_NAME);

	// A non 0 return means init_module failed; module can't be loaded.
	return 0;

}

// the module last called function
void cleanup_module(void) {

	remove_proc_entry(PROCFS_PID_NAME, &proc_root);
	remove_proc_entry(PROCFS_CMD_NAME, &proc_root);

	PDEBUG("Freeing registered irq %d for GPIO %d\n", IRQ_GPIO(gpio_in_streamctl), gpio_in_streamctl);
	free_irq(IRQ_GPIO(gpio_in_streamctl), &data);

	turn_off_gpio(gpio_out_modem);
	// turn off led green: reverse logic
	turn_on_gpio(gpio_out_green);
	del_timer(&jiq_pulse_timer);

	printk(KERN_INFO MODULE_NAME ": Stopping gpio module...\n");

}  

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);		/* Who wrote this module? */
MODULE_DESCRIPTION(DRIVER_DESC);	/* What does this module do */
