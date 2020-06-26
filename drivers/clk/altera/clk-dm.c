// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Intel Corporation <www.intel.com>
 */

#include <common.h>
#include <asm/arch/clock_manager.h>
#include <asm/io.h>
#include <clk-uclass.h>
#include <dm.h>
#include <dm/lists.h>
#include <dm/util.h>
#include <dt-bindings/clock/dm-clock.h>

DECLARE_GLOBAL_DATA_PTR;

struct socfpga_clk_platdata {
	void __iomem *regs;
};

#ifndef CONFIG_TARGET_SOCFPGA_DM_SIMICS
/*
 * function to write the bypass register which requires a poll of the
 * busy bit
 */
static void clk_write_bypass_mainpll(struct socfpga_clk_platdata *plat, u32 val)
{
	CM_REG_WRITEL(plat, val, CLKMGR_MAINPLL_BYPASS);
	cm_wait_for_fsm();
}

static void clk_write_bypass_perpll(struct socfpga_clk_platdata *plat, u32 val)
{
	CM_REG_WRITEL(plat, val, CLKMGR_PERPLL_BYPASS);
	cm_wait_for_fsm();
}

/* function to write the ctrl register which requires a poll of the busy bit */
static void clk_write_ctrl(struct socfpga_clk_platdata *plat, u32 val)
{
	CM_REG_WRITEL(plat, val, CLKMGR_CTRL);
	cm_wait_for_fsm();
}

/*
 * Setup clocks while making no assumptions about previous state of the clocks.
 */
static void clk_basic_init(struct udevice *dev,
			   const struct cm_config * const cfg)
{
	struct socfpga_clk_platdata *plat = dev_get_platdata(dev);

	if (!cfg)
		return;

#ifdef CONFIG_SPL_BUILD
	/* Always force clock manager into boot mode before any configuration */
	clk_write_ctrl(plat,
		       CM_REG_READL(plat, CLKMGR_CTRL) | CLKMGR_CTRL_BOOTMODE);
#else
	/* Skip clock configuration in SSBL if it's not in boot mode */
	if (!(CM_REG_READL(plat, CLKMGR_CTRL) & CLKMGR_CTRL_BOOTMODE))
		return;
#endif

	/* Put both PLLs in bypass */
	clk_write_bypass_mainpll(plat, CLKMGR_BYPASS_MAINPLL_ALL);
	clk_write_bypass_perpll(plat, CLKMGR_BYPASS_PERPLL_ALL);

	/* Put both PLLs in Reset */
	CM_REG_SETBITS(plat, CLKMGR_MAINPLL_PLLCTRL,
		       CLKMGR_PLLCTRL_RST_N_MASK | CLKMGR_PLLCTRL_BYPASS_MASK);
	CM_REG_SETBITS(plat, CLKMGR_PERPLL_PLLCTRL,
		       CLKMGR_PLLCTRL_RST_N_MASK | CLKMGR_PLLCTRL_BYPASS_MASK);

	/* setup main PLL */
	CM_REG_WRITEL(plat, cfg->main_pll_pllglob, CLKMGR_MAINPLL_PLLGLOB);
	CM_REG_WRITEL(plat, cfg->main_pll_plldiv, CLKMGR_MAINPLL_PLLDIV);
	CM_REG_WRITEL(plat, cfg->main_pll_plloutdiv, CLKMGR_MAINPLL_PLLOUTDIV);
	CM_REG_WRITEL(plat, cfg->main_pll_mpuclk, CLKMGR_MAINPLL_MPUCLK);
	CM_REG_WRITEL(plat, cfg->main_pll_nocclk, CLKMGR_MAINPLL_NOCCLK);
	CM_REG_WRITEL(plat, cfg->main_pll_nocdiv, CLKMGR_MAINPLL_NOCDIV);

	/* setup peripheral */
	CM_REG_WRITEL(plat, cfg->per_pll_pllglob, CLKMGR_PERPLL_PLLGLOB);
	CM_REG_WRITEL(plat, cfg->per_pll_plldiv, CLKMGR_PERPLL_PLLDIV);
	CM_REG_WRITEL(plat, cfg->per_pll_plloutdiv, CLKMGR_PERPLL_PLLOUTDIV);
	CM_REG_WRITEL(plat, cfg->per_pll_emacctl, CLKMGR_PERPLL_EMACCTL);
	CM_REG_WRITEL(plat, cfg->per_pll_gpiodiv, CLKMGR_PERPLL_GPIODIV);

	/* Take both PLL out of reset and power up */
	CM_REG_CLRBITS(plat, CLKMGR_MAINPLL_PLLCTRL,
		       CLKMGR_PLLCTRL_RST_N_MASK | CLKMGR_PLLCTRL_BYPASS_MASK);
	CM_REG_CLRBITS(plat, CLKMGR_PERPLL_PLLCTRL,
		       CLKMGR_PLLCTRL_RST_N_MASK | CLKMGR_PLLCTRL_BYPASS_MASK);

	cm_wait_for_lock(CLKMGR_STAT_ALLPLL_LOCKED_MASK);

	/* Configure ping pong counters in altera group */
	CM_REG_WRITEL(plat, cfg->alt_emacactr, CLKMGR_ALTR_EMACACTR);
	CM_REG_WRITEL(plat, cfg->alt_emacbctr, CLKMGR_ALTR_EMACBCTR);
	CM_REG_WRITEL(plat, cfg->alt_emacptpctr, CLKMGR_ALTR_EMACPTPCTR);
	CM_REG_WRITEL(plat, cfg->alt_gpiodbctr, CLKMGR_ALTR_GPIODBCTR);
	CM_REG_WRITEL(plat, cfg->alt_sdmmcctr, CLKMGR_ALTR_SDMMCCTR);
	CM_REG_WRITEL(plat, cfg->alt_s2fuser0ctr, CLKMGR_ALTR_S2FUSER0CTR);
	CM_REG_WRITEL(plat, cfg->alt_s2fuser1ctr, CLKMGR_ALTR_S2FUSER1CTR);
	CM_REG_WRITEL(plat, cfg->alt_psirefctr, CLKMGR_ALTR_PSIREFCTR);

	CM_REG_WRITEL(plat, CLKMGR_LOSTLOCK_SET_MASK, CLKMGR_MAINPLL_LOSTLOCK);
	CM_REG_WRITEL(plat, CLKMGR_LOSTLOCK_SET_MASK, CLKMGR_PERPLL_LOSTLOCK);

	CM_REG_WRITEL(plat, CM_REG_READL(plat, CLKMGR_MAINPLL_PLLGLOB) |
			CLKMGR_PLLGLOB_CLR_LOSTLOCK_BYPASS_MASK,
			CLKMGR_MAINPLL_PLLGLOB);
	CM_REG_WRITEL(plat, CM_REG_READL(plat, CLKMGR_PERPLL_PLLGLOB) |
			CLKMGR_PLLGLOB_CLR_LOSTLOCK_BYPASS_MASK,
			CLKMGR_PERPLL_PLLGLOB);

	/* Take all PLLs out of bypass */
	clk_write_bypass_mainpll(plat, 0);
	clk_write_bypass_perpll(plat, 0);

	/* Clear the loss of lock bits (write 1 to clear) */
	CM_REG_CLRBITS(plat, CLKMGR_INTRCLR,
		       CLKMGR_INTER_PERPLLLOST_MASK |
		       CLKMGR_INTER_MAINPLLLOST_MASK);

	/* Take all ping pong counters out of reset */
	CM_REG_CLRBITS(plat, CLKMGR_ALTR_EXTCNTRST,
		       CLKMGR_ALT_EXTCNTRST_ALLCNTRST);

	/* Out of boot mode */
	clk_write_ctrl(plat,
		       CM_REG_READL(plat, CLKMGR_CTRL) & ~CLKMGR_CTRL_BOOTMODE);
}

static u64 clk_get_vco_clk_hz(struct socfpga_clk_platdata *plat,
			      u32 pllglob_reg, u32 plldiv_reg)
{
	u64 fref, div, reg;

	reg = CM_REG_READL(plat, pllglob_reg);

	fref = (reg & CLKMGR_PLLGLOB_VCO_PSRC_MASK) >>
		CLKMGR_PLLGLOB_VCO_PSRC_OFFSET;

	switch (fref) {
	case CLKMGR_VCO_PSRC_EOSC1:
		fref = cm_get_osc_clk_hz();
		break;
	case CLKMGR_VCO_PSRC_INTOSC:
		fref = cm_get_intosc_clk_hz();
		break;
	case CLKMGR_VCO_PSRC_F2S:
		fref = cm_get_fpga_clk_hz();
		break;
	}

	div = CM_REG_READL(plat, plldiv_reg) & CLKMGR_PLLDIV_REFCLKDIV_MASK;

	return fref / div;
}

static u64 clk_get_main_vco_clk_hz(struct socfpga_clk_platdata *plat)
{
	return clk_get_vco_clk_hz(plat, CLKMGR_MAINPLL_PLLGLOB,
				 CLKMGR_MAINPLL_PLLDIV);
}

static u64 clk_get_per_vco_clk_hz(struct socfpga_clk_platdata *plat)
{
	return clk_get_vco_clk_hz(plat, CLKMGR_PERPLL_PLLGLOB,
				 CLKMGR_PERPLL_PLLDIV);
}

static u32 clk_get_5_1_clk_src(struct socfpga_clk_platdata *plat, u64 reg)
{
	u32 clksrc = CM_REG_READL(plat, reg);

	return (clksrc & CLKMGR_CLKSRC_MASK) >> CLKMGR_CLKSRC_OFFSET;
}

static u64 clk_get_clksrc_hz(struct socfpga_clk_platdata *plat, u32 clksrc_reg,
			     u32 main_div, u32 per_div)
{
	u64 clock = 0;
	u32 clklsrc = clk_get_5_1_clk_src(plat, clksrc_reg);

	switch (clklsrc) {
	case CLKMGR_CLKSRC_MAIN:
		clock = clk_get_main_vco_clk_hz(plat);
		clock /= main_div;
		break;

	case CLKMGR_CLKSRC_PER:
		clock = clk_get_per_vco_clk_hz(plat);
		clock /= per_div;
		break;

	case CLKMGR_CLKSRC_OSC1:
		clock = cm_get_osc_clk_hz();
		break;

	case CLKMGR_CLKSRC_INTOSC:
		clock = cm_get_intosc_clk_hz();
		break;

	case CLKMGR_CLKSRC_FPGA:
		clock = cm_get_fpga_clk_hz();
		break;
	default:
		return 0;
	}

	return clock;
}

static u64 clk_get_mpu_clk_hz(struct socfpga_clk_platdata *plat)
{
	u32 mainpll_c0cnt = (CM_REG_READL(plat, CLKMGR_MAINPLL_PLLOUTDIV) &
			  CLKMGR_PLLOUTDIV_C0CNT_MASK) >>
			  CLKMGR_PLLOUTDIV_C0CNT_OFFSET;

	u32 perpll_c0cnt = (CM_REG_READL(plat, CLKMGR_PERPLL_PLLOUTDIV) &
			  CLKMGR_PLLOUTDIV_C0CNT_MASK) >>
			  CLKMGR_PLLOUTDIV_C0CNT_OFFSET;

	u64 clock = clk_get_clksrc_hz(plat, CLKMGR_MAINPLL_MPUCLK,
				      mainpll_c0cnt, perpll_c0cnt);

	clock /= 1 + (CM_REG_READL(plat, CLKMGR_MAINPLL_MPUCLK) &
		 CLKMGR_CLKCNT_MSK);

	return clock;
}

static u32 clk_get_l3_main_clk_hz(struct socfpga_clk_platdata *plat)
{
	u32 mainpll_c1cnt = (CM_REG_READL(plat, CLKMGR_MAINPLL_PLLOUTDIV) &
			  CLKMGR_PLLOUTDIV_C1CNT_MASK) >>
			  CLKMGR_PLLOUTDIV_C1CNT_OFFSET;

	u32 perpll_c1cnt = (CM_REG_READL(plat, CLKMGR_PERPLL_PLLOUTDIV) &
			  CLKMGR_PLLOUTDIV_C1CNT_MASK) >>
			  CLKMGR_PLLOUTDIV_C1CNT_OFFSET;

	return clk_get_clksrc_hz(plat, CLKMGR_MAINPLL_NOCCLK,
				      mainpll_c1cnt, perpll_c1cnt);
}

static u32 clk_get_l4_main_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_l3_main_clk_hz(plat);

	clock /= BIT((CM_REG_READL(plat, CLKMGR_MAINPLL_NOCDIV) >>
		  CLKMGR_NOCDIV_L4MAIN_OFFSET) &
		  CLKMGR_NOCDIV_DIVIDER_MASK);

	return clock;
}

static u32 clk_get_sdmmc_clk_hz(struct socfpga_clk_platdata *plat)
{
	u32 mainpll_c3cnt = (CM_REG_READL(plat, CLKMGR_MAINPLL_PLLOUTDIV) &
			  CLKMGR_PLLOUTDIV_C3CNT_MASK) >>
			  CLKMGR_PLLOUTDIV_C3CNT_OFFSET;

	u32 perpll_c3cnt = (CM_REG_READL(plat, CLKMGR_PERPLL_PLLOUTDIV) &
			  CLKMGR_PLLOUTDIV_C3CNT_MASK) >>
			  CLKMGR_PLLOUTDIV_C3CNT_OFFSET;

	u64 clock = clk_get_clksrc_hz(plat, CLKMGR_ALTR_SDMMCCTR,
				      mainpll_c3cnt, perpll_c3cnt);

	clock /= 1 + (CM_REG_READL(plat, CLKMGR_ALTR_SDMMCCTR) &
		 CLKMGR_CLKCNT_MSK);

	return clock / 4;
}

static u32 clk_get_l4_sp_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_l3_main_clk_hz(plat);

	clock /= BIT((CM_REG_READL(plat, CLKMGR_MAINPLL_NOCDIV) >>
			  CLKMGR_NOCDIV_L4SPCLK_OFFSET) &
			  CLKMGR_NOCDIV_DIVIDER_MASK);

	return clock;
}

static u32 clk_get_l4_mp_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_l3_main_clk_hz(plat);

	clock /= BIT((CM_REG_READL(plat, CLKMGR_MAINPLL_NOCDIV) >>
			  CLKMGR_NOCDIV_L4MPCLK_OFFSET) &
			  CLKMGR_NOCDIV_DIVIDER_MASK);

	return clock;
}

static u32 clk_get_l4_sys_free_clk_hz(struct socfpga_clk_platdata *plat)
{
	if (CM_REG_READL(plat, CLKMGR_STAT) & CLKMGR_STAT_BOOTMODE)
		return clk_get_l3_main_clk_hz(plat) / 2;

	return clk_get_l3_main_clk_hz(plat) / 4;
}

static u32 clk_get_emac_clk_hz(struct socfpga_clk_platdata *plat, u32 emac_id)
{
	bool emacsel_a;
	u32 ctl;
	u32 ctr_reg;
	u32 clock;
	u32 div;
	u32 reg;

	/* Get EMAC clock source */
	ctl = CM_REG_READL(plat, CLKMGR_PERPLL_EMACCTL);
	if (emac_id == DM_EMAC0_CLK)
		ctl = (ctl >> CLKMGR_PERPLLGRP_EMACCTL_EMAC0SELB_OFFSET) &
		       CLKMGR_PERPLLGRP_EMACCTL_EMAC0SELB_MASK;
	else if (emac_id == DM_EMAC1_CLK)
		ctl = (ctl >> CLKMGR_PERPLLGRP_EMACCTL_EMAC1SELB_OFFSET) &
		       CLKMGR_PERPLLGRP_EMACCTL_EMAC1SELB_MASK;
	else if (emac_id == DM_EMAC2_CLK)
		ctl = (ctl >> CLKMGR_PERPLLGRP_EMACCTL_EMAC2SELB_OFFSET) &
		       CLKMGR_PERPLLGRP_EMACCTL_EMAC2SELB_MASK;
	else
		return 0;

	if (ctl) {
		/* EMAC B source */
		emacsel_a = false;
		ctr_reg = CLKMGR_ALTR_EMACBCTR;
	} else {
		/* EMAC A source */
		emacsel_a = true;
		ctr_reg = CLKMGR_ALTR_EMACACTR;
	}

	reg = CM_REG_READL(plat, ctr_reg);
	clock = (reg & CLKMGR_ALT_EMACCTR_SRC_MASK)
		 >> CLKMGR_ALT_EMACCTR_SRC_OFFSET;
	div = (reg & CLKMGR_ALT_EMACCTR_CNT_MASK)
		>> CLKMGR_ALT_EMACCTR_CNT_OFFSET;

	switch (clock) {
	case CLKMGR_CLKSRC_MAIN:
		clock = clk_get_main_vco_clk_hz(plat);
		if (emacsel_a) {
			clock /= (CM_REG_READL(plat, CLKMGR_MAINPLL_PLLOUTDIV) &
				  CLKMGR_PLLOUTDIV_C2CNT_MASK) >>
				  CLKMGR_PLLOUTDIV_C2CNT_OFFSET;
		} else {
			clock /= (CM_REG_READL(plat, CLKMGR_MAINPLL_PLLOUTDIV) &
				  CLKMGR_PLLOUTDIV_C3CNT_MASK) >>
				  CLKMGR_PLLOUTDIV_C3CNT_OFFSET;
		}
		break;

	case CLKMGR_CLKSRC_PER:
		clock = clk_get_per_vco_clk_hz(plat);
		if (emacsel_a) {
			clock /= (CM_REG_READL(plat, CLKMGR_PERPLL_PLLOUTDIV) &
				  CLKMGR_PLLOUTDIV_C2CNT_MASK) >>
				  CLKMGR_PLLOUTDIV_C2CNT_OFFSET;
		} else {
			clock /= (CM_REG_READL(plat, CLKMGR_PERPLL_PLLOUTDIV) &
				  CLKMGR_PLLOUTDIV_C3CNT_MASK >>
				  CLKMGR_PLLOUTDIV_C3CNT_OFFSET);
		}
		break;

	case CLKMGR_CLKSRC_OSC1:
		clock = cm_get_osc_clk_hz();
		break;

	case CLKMGR_CLKSRC_INTOSC:
		clock = cm_get_intosc_clk_hz();
		break;

	case CLKMGR_CLKSRC_FPGA:
		clock = cm_get_fpga_clk_hz();
		break;
	}

	clock /= 1 + div;

	return clock;
}
#endif

#ifdef CONFIG_TARGET_SOCFPGA_DM_SIMICS
static ulong socfpga_clk_get_rate(struct clk *clk)
{
	switch (clk->id) {
	case DM_MPU_CLK:
		return 1000000000;
	case DM_L4_MAIN_CLK:
		return 400000000;
	case DM_L4_SYS_FREE_CLK:
		return 100000000;
	case DM_L4_MP_CLK:
		return 200000000;
	case DM_L4_SP_CLK:
		return 76800;
	case DM_SDMMC_CLK:
		return 25000000;
	case DM_EMAC0_CLK:
	case DM_EMAC1_CLK:
	case DM_EMAC2_CLK:
		return 250000000;
	case DM_USB_CLK:
	case DM_NAND_X_CLK:
		return 200000000;
	case DM_NAND_CLK:
		return 50000000;
	default:
		return -ENXIO;
	}
}
#else
static ulong socfpga_clk_get_rate(struct clk *clk)
{
	struct socfpga_clk_platdata *plat = dev_get_platdata(clk->dev);

	switch (clk->id) {
	case DM_MPU_CLK:
		return clk_get_mpu_clk_hz(plat);
	case DM_L4_MAIN_CLK:
		return clk_get_l4_main_clk_hz(plat);
	case DM_L4_SYS_FREE_CLK:
		return clk_get_l4_sys_free_clk_hz(plat);
	case DM_L4_MP_CLK:
		return clk_get_l4_mp_clk_hz(plat);
	case DM_L4_SP_CLK:
		return clk_get_l4_sp_clk_hz(plat);
	case DM_SDMMC_CLK:
		return clk_get_sdmmc_clk_hz(plat);
	case DM_EMAC0_CLK:
	case DM_EMAC1_CLK:
	case DM_EMAC2_CLK:
		return clk_get_emac_clk_hz(plat, clk->id);
	case DM_USB_CLK:
	case DM_NAND_X_CLK:
		return clk_get_l4_mp_clk_hz(plat);
	case DM_NAND_CLK:
		return clk_get_l4_mp_clk_hz(plat) / 4;
	default:
		return -ENXIO;
	}
}
#endif

static int socfpga_clk_enable(struct clk *clk)
{
	return 0;
}

static int socfpga_clk_probe(struct udevice *dev)
{
#ifndef CONFIG_TARGET_SOCFPGA_DM_SIMICS
	const struct cm_config *cm_default_cfg = cm_get_default_config();

	clk_basic_init(dev, cm_default_cfg);
#endif

	return 0;
}

static int socfpga_clk_ofdata_to_platdata(struct udevice *dev)
{
	struct socfpga_clk_platdata *plat = dev_get_platdata(dev);
	fdt_addr_t addr;

	addr = devfdt_get_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	plat->regs = (void __iomem *)addr;

	return 0;
}

static struct clk_ops socfpga_clk_ops = {
	.enable		= socfpga_clk_enable,
	.get_rate	= socfpga_clk_get_rate,
};

static const struct udevice_id socfpga_clk_match[] = {
	{ .compatible = "intel,dm-clkmgr" },
	{}
};

U_BOOT_DRIVER(socfpga_dm_clk) = {
	.name		= "clk-dm",
	.id		= UCLASS_CLK,
	.of_match	= socfpga_clk_match,
	.ops		= &socfpga_clk_ops,
	.probe		= socfpga_clk_probe,
	.ofdata_to_platdata = socfpga_clk_ofdata_to_platdata,
	.platdata_auto_alloc_size = sizeof(struct socfpga_clk_platdata),
};
