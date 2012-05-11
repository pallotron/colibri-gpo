# To build this module you need to install arm toolchain provided by the
# Toradex Colibri Linux SDK (colibri-bsp-2.2.tar.gz) ...
#
# Then you have to export this Environment variables (supposing you've
# installed the toolchain in /usr/local/arm-linux):
#
# $ export PATH=${PATH}:/usr/local/arm-linux/bin
# $ source ../../arm_env
# $ KERNELDIR = <KERNEL_2.4_PATH> make
#

# Comment/uncomment the following line to disable/enable debugging
DEBUG := y
#KERNELDIR := /home/pallotron/work/colibri/colibri-bsp-2.2/src/linux-2.4.29-vrs1-pxa1-intc4-col3
# change this variable to you kernel path

# Add debugging flag (or not) to CFLAGS
ifeq ($(DEBUG), y)
  DEBFLAGS := -O2 -DSCULL_DEBUG 
else
  DEBFLAGS := -O2
endif

TARGET_GPIO     := gpio
TARGET_BATTERY  := colibri-battery
WARN    := -W -Wall -Wstrict-prototypes -Wmissing-prototypes
INCLUDE := -isystem ${KERNELDIR}/include -I ${KERNELDIR}/drivers/misc
CFLAGS  := $(DEBFLAGS) -DMODULE -D__KERNEL__ -DARM_PXA ${WARN} ${INCLUDE}
CC      := arm-linux-gcc

all: ${TARGET_GPIO}.o ${TARGET_BATTERY}.o cscope ctags

${TARGET_GPIO}.o: ${TARGET_GPIO}.c
${TARGET_BATTERY}.o: ${TARGET_BATTERY}.c

.PHONY: clean

clean:
	rm -rf *.o
	rm -f cscope.out
	rm -f tags

cscope: 
	cscope -bR

ctags:
	ctags -R

