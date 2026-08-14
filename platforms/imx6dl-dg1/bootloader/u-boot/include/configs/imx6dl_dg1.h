/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MICROFX_IMX6DL_DG1_CONFIG_H
#define __MICROFX_IMX6DL_DG1_CONFIG_H

#include "mx6_common.h"

#define CFG_MXC_UART_BASE UART1_BASE
#define CFG_SYS_FSL_ESDHC_ADDR USDHC3_BASE_ADDR

#define PHYS_SDRAM MMDC0_ARB_BASE_ADDR
#define CFG_SYS_SDRAM_BASE PHYS_SDRAM
#define CFG_SYS_INIT_RAM_ADDR IRAM_BASE_ADDR
#define CFG_SYS_INIT_RAM_SIZE IRAM_SIZE

/*
 * The active slot is an ordinary text environment on microfx-boot. Linux can
 * replace it atomically after validating a candidate. U-Boot itself never
 * writes an environment, so a prototype cannot corrupt the raw boot area.
 */
#define CFG_EXTRA_ENV_SETTINGS \
	"console=ttymxc0,115200\0" \
	"mmcdev=0\0" \
	"bootpart=1\0" \
	"active_slot=a\0" \
	"kernel_addr=0x12000000\0" \
	"slot_addr=0x14000000\0" \
	"fdt_addr=0x18000000\0" \
	"kernel_file=/microfx-imx6dl-dg1.img\0" \
	"fdt_file=/microfx-imx6dl-dg1.dtb\0" \
	"slot_file=/microfx-slot.env\0" \
	"fdt_high=0xffffffff\0" \
	"initrd_high=0xffffffff\0" \
	"import_slot=if ext4load mmc ${mmcdev}:${bootpart} ${slot_addr} ${slot_file}; then env import -t ${slot_addr} ${filesize}; fi\0" \
	"select_a=setenv rootpart 2; setenv rootlabel microfx-root-a\0" \
	"select_b=setenv rootpart 3; setenv rootlabel microfx-root-b\0" \
	"load_kernel=ext4load mmc ${mmcdev}:${bootpart} ${kernel_addr} ${kernel_file}\0" \
	"load_fdt=ext4load mmc ${mmcdev}:${bootpart} ${fdt_addr} ${fdt_file}\0" \
	"try_slot=if run load_kernel && run load_fdt; then setenv bootargs console=${console} root=LABEL=${rootlabel} rootwait rw; bootz ${kernel_addr} - ${fdt_addr}; fi\0" \
	"boot_a=echo Trying microFX root A; run select_a; run try_slot\0" \
	"boot_b=echo Trying microFX root B; run select_b; run try_slot\0"

#endif
