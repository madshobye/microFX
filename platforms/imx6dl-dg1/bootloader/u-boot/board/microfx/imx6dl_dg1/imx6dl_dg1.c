// SPDX-License-Identifier: GPL-2.0-or-later

#include <init.h>
#include <asm/arch/iomux.h>
#include <asm/arch/mx6-pins.h>
#include <asm/arch/sys_proto.h>
#include <asm/global_data.h>
#include <asm/mach-imx/iomux-v3.h>

DECLARE_GLOBAL_DATA_PTR;

#define UART_PAD_CTRL (PAD_CTL_PUS_100K_UP | PAD_CTL_SPEED_MED | \
		       PAD_CTL_DSE_40ohm | PAD_CTL_SRE_FAST | PAD_CTL_HYS)

static const iomux_v3_cfg_t uart1_pads[] = {
	IOMUX_PADS(PAD_CSI0_DAT10__UART1_TX_DATA |
		   MUX_PAD_CTRL(UART_PAD_CTRL)),
	IOMUX_PADS(PAD_CSI0_DAT11__UART1_RX_DATA |
		   MUX_PAD_CTRL(UART_PAD_CTRL)),
};

int dram_init(void)
{
	gd->ram_size = imx_ddr_size();
	return 0;
}

int board_early_init_f(void)
{
	SETUP_IOMUX_PADS(uart1_pads);
	return 0;
}

int board_init(void)
{
	gd->bd->bi_boot_params = PHYS_SDRAM + 0x100;
	return 0;
}

int checkboard(void)
{
	puts("Board: microFX i.MX6DL DG1 prototype\n");
	return 0;
}
