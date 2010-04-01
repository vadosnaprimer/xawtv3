#################################################
# config

# 0: Temic PAL tuner
# 1: Philips PAL_I tuner
# 2: Philips NTSC tuner
# 3: Philips SECAM tuner
# 4: no tuner
# 5: Philips PAL tuner
# 6: Temic NTSC tuner
# 7: Temic PAL tuner
TUNER=0

# 0: Auto-Detect
# 1: Miro
# 2: Hauppauge
# 3: STB
# 4: Intel
# 5: Diamond
# 6: AVerMedia
CARD=0

# Official major device number is 81
# Older kernels might have problems with such high numbers.
# But I rather recommend updating to kernel 2.0.31 or higher.
BTTV_MAJOR=81

# currently running kernel
CURRENT=$(shell uname -r)

# where the kernel sources are located
#KERNEL_LOCATION=/usr/src/kernel/$(CURRENT)
KERNEL_LOCATION=/usr/src/linux


#################################################
# some magic for using linux kernel settings
# when compiling module(s)

M_OBJS       = bttv.o i2c.o msp3400.o tuner.o videodev.o 
EXTRA_CFLAGS = -DTUNER_DEFAULT=$(TUNER) -DCARD_DEFAULT=$(CARD) \
	-DBTTV_MAJOR=$(BTTV_MAJOR) $(INTERFACE) -DEXPORT_SYMTAB 

here:
	DIR=`pwd`; (cd $(KERNEL_LOCATION); make SUBDIRS=$$DIR modules)

install:
	su -c "cp -v $(M_OBJS) /lib/modules/$(CURRENT)/misc"

clean:
	-rm -f $(M_OBJS) .*.o.flags *~

include $(KERNEL_LOCATION)/Rules.make

