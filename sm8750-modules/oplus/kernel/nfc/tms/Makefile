#
# Makefile for TMS(THN31) NFC/ESE device + oplus_nfc drivers.
#

# --- TMS NFC/ESE device driver -> tms_device_modules.ko ---
tms_device_modules-y += thn31/tms_common.o \
                        thn31/nfc/nfc_common.o \
                        thn31/nfc/nfc_driver.o \
                        thn31/ese/ese_common.o \
                        thn31/ese/ese_driver.o \
                        thn31/debuger/debuger.o \
                        thn31/debuger/logger.o \
                        thn31/debuger/checker.o
obj-m += tms_device_modules.o

# --- oplus_nfc driver -> oplus_nfc.ko ---
oplus_nfc-y := oplus_nfc/oplus_nfc.o
obj-m += oplus_nfc.o

ccflags-y := -I$(srctree)/$(src) \
             -I$(srctree)/$(src)/thn31 \
             -I$(srctree)/$(src)/thn31/nfc \
             -I$(srctree)/$(src)/thn31/ese \
             -I$(srctree)/$(src)/thn31/debuger \
             -I$(srctree)/$(src)/oplus_nfc \
             -I$(srctree)/include \
             -I$(srctree)/../sm8750-modules/oplus/kernel \
             -I$(srctree)/../sm8750-modules/oplus/kernel/boot/include

ccflags-y += -DCONFIG_TMS_NFC_DEVICE \
             -DCONFIG_TMS_ESE_DEVICE \
             -DTMS_DEBUGER_LOGGER \
             -DTMS_DEBUGER_CHECKER
