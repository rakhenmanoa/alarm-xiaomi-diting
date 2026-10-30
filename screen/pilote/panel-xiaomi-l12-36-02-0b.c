// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi 12T Pro (diting, project L12) panel "36 02 0b": 1220x2712 AMOLED,
 * Novatek DDIC, DSC 1.1 (10 bpc, 8 bpp), MIPI DSI command mode, 4 lanes.
 *
 * Initialisation sequence generated with linux-mdss-dsi-panel-driver-generator
 * from the vendor device tree of a 22081212UG unit
 * (qcom,mdss_dsi_l12_36_02_0b_dsc_cmd); supplies, power sequence and
 * brightness handling follow the same vendor data. Local bring-up driver.
 *
 * The diting also ships with a second panel, "42 02 0a" (vendor
 * qcom,mdss_dsi_l12_42_02_0a_dsc_cmd): same size, supplies and DSC slice
 * layout, but its own init sequence, refresh rate commands and the standard
 * DSC 1.1 (pre-SCR) rate control. The bootloader names the panel it found in
 * the kernel command line (msm_drm.dsi_display0=...), which selects the
 * variant at probe time.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/string.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/*
 * DSC rate control used by the host encoder; it must match the DDIC's
 * decoder. Selectable at load time for bring-up of new panel variants
 * (e.g. options panel_xiaomi_l12_36_02_0b dsc_rc=2 dsc_fl=12):
 *   dsc_rc: -1 = panel default, 0 = standard DSC 1.1 table (msm default),
 *            1 = Novatek-tuned table, 2 = Qualcomm DSC 1.1 SCR table
 *   dsc_fl: first_line_bpg_offset, 0 = panel default
 */
static int dsc_rc = -1;
module_param(dsc_rc, int, 0444);
MODULE_PARM_DESC(dsc_rc, "DSC RC table: -1 auto, 0 DSC 1.1, 1 Novatek, 2 DSC 1.1 SCR");
static int dsc_fl;
module_param(dsc_fl, int, 0444);
MODULE_PARM_DESC(dsc_fl, "DSC first_line_bpg_offset (0 = auto)");

/* Vendor normal range; 2048..4095 is the high-brightness (HBM) range. */
#define XIAOMI_L12_MAX_BRIGHTNESS	2047

struct xiaomi_l12;
struct xiaomi_l12_mode;

struct xiaomi_l12_desc {
	const char *name;
	int (*on)(struct xiaomi_l12 *ctx);
	int (*set_rate)(struct xiaomi_l12 *ctx, const struct xiaomi_l12_mode *m);
	const struct xiaomi_l12_mode *modes;
	unsigned int num_modes;
	unsigned int reset_last_ms;
	int rc_table;		/* default dsc_rc */
	u8 first_line_bpg_offset;
};

struct xiaomi_l12 {
	struct drm_panel panel;
	const struct xiaomi_l12_desc *desc;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct drm_connector *connector;
	struct gpio_desc *reset_gpio;
	struct regulator *vddio;
	struct regulator *vddd;
	struct regulator *vci;
};

static inline struct xiaomi_l12 *to_xiaomi_l12(struct drm_panel *panel)
{
	return container_of_const(panel, struct xiaomi_l12, panel);
}

/*
 * Vendor qcom,mdss-dsi-reset-sequence: high 11 ms, low 1 ms, then high for
 * 20 ms (36 02 0b) or 11 ms (42 02 0a)
 */
static void xiaomi_l12_reset(struct xiaomi_l12 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(ctx->desc->reset_last_ms);
}

/* Vendor dsi_panel_pwr_supply_L12: vddio, vddd, vci with these delays */
static int xiaomi_l12_power_on(struct xiaomi_l12 *ctx)
{
	int ret;

	ret = regulator_enable(ctx->vddio);
	if (ret)
		return ret;
	msleep(20);

	ret = regulator_enable(ctx->vddd);
	if (ret)
		goto err_vddio;
	msleep(20);

	usleep_range(10000, 11000);
	ret = regulator_enable(ctx->vci);
	if (ret)
		goto err_vddd;
	usleep_range(15000, 16000);

	return 0;

err_vddd:
	regulator_disable(ctx->vddd);
err_vddio:
	regulator_disable(ctx->vddio);
	return ret;
}

static void xiaomi_l12_power_off(struct xiaomi_l12 *ctx)
{
	regulator_disable(ctx->vci);
	usleep_range(10000, 11000);
	regulator_disable(ctx->vddd);
	usleep_range(10000, 11000);
	regulator_disable(ctx->vddio);
	usleep_range(10000, 11000);
}

static int xiaomi_l12_36_on(struct xiaomi_l12 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0,
				     0x41, 0x73, 0x73, 0x73, 0x83);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2,
				     0x80, 0xef, 0x66, 0x86, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe5,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x08, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x08, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea,
				     0x02, 0x00, 0xe0, 0x00, 0xe0, 0x70, 0x00,
				     0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea,
				     0x00, 0x00, 0x80, 0x00, 0x80, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x88, 0xe0, 0xcc, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x86, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea,
				     0x02, 0x00, 0xe0, 0x00, 0xe0, 0x70, 0x00,
				     0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea, 0x80, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0xfc, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x83);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x10);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x04c3);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0a97);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x00);
	/* tear effect on; vendor passes 0x30 (V-blank only + vendor bits) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_TEAR_ON, 0x30);
	mipi_dsi_dcs_set_tear_scanline_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x04);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0xff03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91,
				     0xab, 0x28, 0x00, 0x0c, 0xd2, 0x00, 0x02,
				     0x4c, 0x01, 0x24, 0x00, 0x08, 0x09, 0x75,
				     0x07, 0x7b, 0x10, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfd, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0x01, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0x01, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0x01, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0x01, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0xdd, 0x06, 0x22, 0x11, 0xff, 0x00, 0x06,
				     0x20, 0x11, 0xff, 0x00, 0x04, 0x01, 0x11,
				     0x05, 0x95, 0x18, 0x04, 0x01, 0x11, 0x05,
				     0x95, 0x18, 0x04, 0x01, 0x11, 0x05, 0x95,
				     0x18, 0x04, 0x01, 0x11, 0x05, 0x95, 0x18,
				     0x04, 0x01, 0x11, 0x05, 0x95, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2,
				     0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x80, 0x08, 0x08, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6,
				     0xad, 0xaa, 0xaa, 0xa3, 0xad, 0xaa, 0xaa,
				     0xa3, 0xad, 0xaa, 0xaa, 0xa3, 0xad, 0xaa,
				     0xaa, 0xa3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf6, 0x70, 0x70, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf6, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x00, 0x00, 0x00, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x80, 0x05, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x05, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x80, 0x05, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x60, 0x60, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x84, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x84, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x84, 0x00, 0x00, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x84, 0x00, 0x00, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x08, 0x06, 0x58);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

/* Vendor qcom,mdss-dsi-on-command of qcom,mdss_dsi_l12_42_02_0a_dsc_cmd (60 Hz) */
static int xiaomi_l12_42_on(struct xiaomi_l12 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0,
				     0x0f, 0x00, 0x00, 0x0f, 0x00, 0x02, 0x0f,
				     0x00, 0x00, 0x0f, 0x00, 0x00, 0x0f, 0x00,
				     0x00, 0x0f, 0x00, 0x00, 0x0f, 0x00, 0x00,
				     0x0f, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x0f,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0, 0x00, 0x07, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x00, 0x0a, 0x9e, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x32, 0x31, 0x0f, 0x32, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x32, 0x31, 0x0f, 0x32, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x03, 0x12, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x00, 0x14, 0x00, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91,
				     0xab, 0x28, 0x00, 0x0c, 0xc2, 0x00, 0x02,
				     0x32, 0x01, 0x31, 0x00, 0x08, 0x08, 0xbb,
				     0x07, 0x7b, 0x10, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_MEMORY_START, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				     0x00, 0x00, 0x0f, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_TEAR_ON, 0x00);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x04c3);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0a97);
	/* 60 Hz (vendor timing@0 switch command) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x01);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x9b, 0x01, 0x94, 0xd0, 0x22, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0xd8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x98);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x0a, 0x0a);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int xiaomi_l12_off(struct xiaomi_l12 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

/*
 * Command mode: the horizontal "porches" only stand for transfer overhead
 * and set the DSI link rate. The vendor drives the link at 1.1 Gbit/s per
 * lane for all refresh rates (qcom,mdss-dsi-panel-clockrate, frame transfer
 * in 7.3 ms). The msm host derives the link rate from the mode clock with
 * the DSC-compressed width (1220 * 8 / 30 = 326 pixels per line), so the
 * front porch of each mode is sized to land at that rate.
 */
/* both panels: vfp + vsync + vbp = 44 lines (24/4/16 and 20/4/20) */
#define XIAOMI_L12_VTOTAL	(2712 + 24 + 4 + 16)
#define XIAOMI_L12_PCLK		146666667	/* 1.1 Gbit/s * 4 lanes / 30 bpp */
#define XIAOMI_L12_HFP(fps)	(XIAOMI_L12_PCLK / (XIAOMI_L12_VTOTAL * (fps)) - \
				 326 - 16 - 86)
#define XIAOMI_L12_MODE(fps, preferred) XIAOMI_L12_MODE_V(fps, preferred, 24)
#define XIAOMI_L12_MODE_V(fps, preferred, vfp) {				\
	.clock = (1220 + XIAOMI_L12_HFP(fps) + 16 + 86) *			\
		 XIAOMI_L12_VTOTAL * (fps) / 1000,				\
	.hdisplay = 1220,							\
	.hsync_start = 1220 + XIAOMI_L12_HFP(fps),				\
	.hsync_end = 1220 + XIAOMI_L12_HFP(fps) + 16,				\
	.htotal = 1220 + XIAOMI_L12_HFP(fps) + 16 + 86,				\
	.vdisplay = 2712,							\
	.vsync_start = 2712 + (vfp),						\
	.vsync_end = 2712 + (vfp) + 4,						\
	.vtotal = XIAOMI_L12_VTOTAL,						\
	.width_mm = 70,								\
	.height_mm = 155,							\
	.type = DRM_MODE_TYPE_DRIVER | (preferred),				\
}

/* Vendor qcom,mdss-dsi-timing-switch-command per refresh rate */
struct xiaomi_l12_mode {
	struct drm_display_mode mode;
	u8 rate[2];		/* 0x2f: frame rate select */
	u8 e2_0[5];		/* 0xe2 */
	u8 e2_12[3];		/* 0xe2 at offset 0x12 */
	u8 ea_0[9];		/* 0xea */
	u8 ea_17[4];		/* 0xea at offset 0x17 */
	/* 42 02 0a */
	u8 rate42;		/* 0x2f */
	u8 c0_offset;		/* 0x6f before 0xc0 */
	u8 c0;			/* 0xc0 */
	unsigned int ce_wait_ms;
};

static const struct xiaomi_l12_mode xiaomi_l12_36_modes[] = {
	{
		.mode = XIAOMI_L12_MODE(60, DRM_MODE_TYPE_PREFERRED),
		.rate = { 0x2f, 0x00 },
		.e2_0 = { 0xe2, 0x88, 0xe0, 0xcc, 0x0c },
		.e2_12 = { 0xe2, 0x86, 0x01 },
		.ea_0 = { 0xea, 0x02, 0x00, 0xe0, 0x00, 0xe0, 0x70, 0x00, 0x70 },
		.ea_17 = { 0xea, 0x80, 0x00, 0x80 },
	}, {
		.mode = XIAOMI_L12_MODE(90, 0),
		.rate = { 0x2f, 0x01 },
		.e2_0 = { 0xe2, 0x42, 0xee, 0x04, 0x20 },
		.e2_12 = { 0xe2, 0xfd, 0x03 },
		.ea_0 = { 0xea, 0x02, 0x00, 0xb8, 0x00, 0xb8, 0x58, 0x00, 0x58 },
		.ea_17 = { 0xea, 0x48, 0x00, 0x48 },
	}, {
		.mode = XIAOMI_L12_MODE(120, 0),
		.rate = { 0x2f, 0x02 },
		.e2_0 = { 0xe2, 0xd6, 0x4e, 0x84, 0x88 },
		.e2_12 = { 0xe2, 0xfd, 0x03 },
		.ea_0 = { 0xea, 0x02, 0x00, 0xe0, 0x00, 0xe0, 0x80, 0x00, 0x80 },
		.ea_17 = { 0xea, 0x38, 0x00, 0x38 },
	},
};

/* Vendor timing@0 (60 Hz), timing@2 (90 Hz), timing@1 (120 Hz) */
static const struct xiaomi_l12_mode xiaomi_l12_42_modes[] = {
	{
		.mode = XIAOMI_L12_MODE_V(60, DRM_MODE_TYPE_PREFERRED, 20),
		.rate42 = 0x02, .c0_offset = 0x09, .c0 = 0x00, .ce_wait_ms = 20,
	}, {
		.mode = XIAOMI_L12_MODE_V(90, 0, 20),
		.rate42 = 0x04, .c0_offset = 0x0b, .c0 = 0x20, .ce_wait_ms = 13,
	}, {
		.mode = XIAOMI_L12_MODE_V(120, 0, 20),
		.rate42 = 0x03, .c0_offset = 0x0a, .c0 = 0x20, .ce_wait_ms = 10,
	},
};

static const struct xiaomi_l12_mode *xiaomi_l12_current_mode(struct xiaomi_l12 *ctx)
{
	struct drm_connector *connector = ctx->connector;
	struct drm_crtc_state *crtc_state;
	int i;

	const struct xiaomi_l12_mode *modes = ctx->desc->modes;

	if (!connector || !connector->state || !connector->state->crtc)
		return &modes[0];

	crtc_state = connector->state->crtc->state;
	for (i = 0; i < ctx->desc->num_modes; i++)
		if (drm_mode_match(&crtc_state->mode, &modes[i].mode,
				   DRM_MODE_MATCH_TIMINGS | DRM_MODE_MATCH_CLOCK))
			return &modes[i];

	return &modes[0];
}

static int xiaomi_l12_36_set_rate(struct xiaomi_l12 *ctx,
				  const struct xiaomi_l12_mode *m)
{
	struct mipi_dsi_multi_context dsi_ctx_ = { .dsi = ctx->dsi };
	struct mipi_dsi_multi_context *dsi_ctx = &dsi_ctx_;

	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, m->rate, sizeof(m->rate));
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x01);
	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, m->e2_0, sizeof(m->e2_0));
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x12);
	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, m->e2_12, sizeof(m->e2_12));
	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, m->ea_0, sizeof(m->ea_0));
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6f, 0x17);
	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, m->ea_17, sizeof(m->ea_17));
	/* back to the standard command page */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);

	return dsi_ctx->accum_err;
}

/* Vendor qcom,mdss-dsi-timing-switch-command of the 42 02 0a */
static int xiaomi_l12_42_set_rate(struct xiaomi_l12 *ctx,
				  const struct xiaomi_l12_mode *m)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	const u8 rate[] = { 0x2f, m->rate42 };
	const u8 offset[] = { 0x6f, m->c0_offset };
	const u8 c0[] = { 0xc0, m->c0 };

	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, rate, sizeof(rate));
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, offset, sizeof(offset));
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, c0, sizeof(c0));
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x01);
	mipi_dsi_msleep(&dsi_ctx, m->ce_wait_ms);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);

	return dsi_ctx.accum_err;
}

static int xiaomi_l12_prepare(struct drm_panel *panel)
{
	struct xiaomi_l12 *ctx = to_xiaomi_l12(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	const struct xiaomi_l12_mode *mode;
	int ret;

	ret = xiaomi_l12_power_on(ctx);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	xiaomi_l12_reset(ctx);

	ret = ctx->desc->on(ctx);
	if (ret < 0) {
		dev_err(dev, "failed to initialize panel: %d\n", ret);
		goto err_off;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(dev, "failed to transmit PPS: %d\n", ret);
		goto err_off;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		goto err_off;
	}

	/* on() sets up 60 Hz; switch like the vendor driver does at runtime */
	mode = xiaomi_l12_current_mode(ctx);
	if (mode != &ctx->desc->modes[0]) {
		ret = ctx->desc->set_rate(ctx, mode);
		if (ret < 0) {
			dev_err(dev, "failed to set refresh rate: %d\n", ret);
			goto err_off;
		}
	}

	msleep(28);

	return 0;

err_off:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	xiaomi_l12_power_off(ctx);
	return ret;
}

static int xiaomi_l12_unprepare(struct drm_panel *panel)
{
	struct xiaomi_l12 *ctx = to_xiaomi_l12(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = xiaomi_l12_off(ctx);
	if (ret < 0)
		dev_err(dev, "failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	xiaomi_l12_power_off(ctx);

	return 0;
}

/* First vendor timing (60 Hz); 90/120 Hz modes are not described yet. */
static int xiaomi_l12_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct xiaomi_l12 *ctx = to_xiaomi_l12(panel);
	struct drm_display_mode *mode;
	int i;

	ctx->connector = connector;

	for (i = 0; i < ctx->desc->num_modes; i++) {
		mode = drm_mode_duplicate(connector->dev, &ctx->desc->modes[i].mode);
		if (!mode)
			return -ENOMEM;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = ctx->desc->modes[0].mode.width_mm;
	connector->display_info.height_mm = ctx->desc->modes[0].mode.height_mm;

	return ctx->desc->num_modes;
}

static const struct drm_panel_funcs xiaomi_l12_panel_funcs = {
	.prepare = xiaomi_l12_prepare,
	.unprepare = xiaomi_l12_unprepare,
	.get_modes = xiaomi_l12_get_modes,
};

/* 16-bit DBV, big-endian (vendor qcom,mdss-dsi-bl-inverted-dbv) */
static int xiaomi_l12_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return ret < 0 ? ret : 0;
}

static const struct backlight_ops xiaomi_l12_bl_ops = {
	.update_status = xiaomi_l12_bl_update_status,
};

static struct backlight_device *
xiaomi_l12_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = XIAOMI_L12_MAX_BRIGHTNESS / 2,
		.max_brightness = XIAOMI_L12_MAX_BRIGHTNESS,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &xiaomi_l12_bl_ops, &props);
}

/*
 * The DDIC decodes with fixed Novatek-tuned rate control parameters (vendor
 * sde_dsc_helper.c: DSC 1.1 SCR, 10 bpc / 8 bpp, "nvt" min/max QP and BPG
 * tables, first_line_bpg_offset 13 as used for NT37705-class panels). The
 * vendor 0x91 command is derived from the same values. The host encoder has
 * to use them as well.
 */
static void xiaomi_l12_set_rc_params(struct drm_dsc_config *dsc, int table,
				     u8 first_line_bpg_offset)
{
	/* Novatek-tuned (vendor "nvt") */
	static const u8 nvt_min_qp[DSC_NUM_BUF_RANGES] = {
		0, 4, 5, 5, 7, 7, 7, 7, 7, 8, 9, 9, 9, 12, 16
	};
	static const u8 nvt_max_qp[DSC_NUM_BUF_RANGES] = {
		8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17
	};
	static const s8 nvt_bpg[DSC_NUM_BUF_RANGES] = {
		2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12
	};
	/* Qualcomm sde_dsc_helper.c, DSC v1.1 SCR, 10 bpc / 8 bpp */
	static const u8 scr_min_qp[DSC_NUM_BUF_RANGES] = {
		0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16
	};
	static const u8 scr_max_qp[DSC_NUM_BUF_RANGES] = {
		8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17
	};
	static const s8 scr_bpg[DSC_NUM_BUF_RANGES] = {
		2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12
	};
	const u8 *min_qp, *max_qp;
	const s8 *bpg_offset;
	int i;

	if (table == 1) {
		min_qp = nvt_min_qp; max_qp = nvt_max_qp; bpg_offset = nvt_bpg;
	} else {
		min_qp = scr_min_qp; max_qp = scr_max_qp; bpg_offset = scr_bpg;
	}

	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		dsc->rc_range_params[i].range_min_qp = min_qp[i];
		dsc->rc_range_params[i].range_max_qp = max_qp[i];
		/* 6-bit two's complement, as in the PPS */
		dsc->rc_range_params[i].range_bpg_offset =
			bpg_offset[i] & DSC_RANGE_BPG_OFFSET_MASK;
	}
	dsc->first_line_bpg_offset = first_line_bpg_offset;
	dsc->initial_offset = 6144;
	dsc->initial_xmit_delay = 512;
	dsc->flatness_min_qp = 7;
	dsc->flatness_max_qp = 16;
	dsc->rc_quant_incr_limit0 = 15;
	dsc->rc_quant_incr_limit1 = 15;
}

static const struct xiaomi_l12_desc xiaomi_l12_36_02_0b_desc = {
	.name = "36 02 0b",
	.on = xiaomi_l12_36_on,
	.set_rate = xiaomi_l12_36_set_rate,
	.modes = xiaomi_l12_36_modes,
	.num_modes = ARRAY_SIZE(xiaomi_l12_36_modes),
	.reset_last_ms = 20,
	.rc_table = 1,
	.first_line_bpg_offset = 13,
};

static const struct xiaomi_l12_desc xiaomi_l12_42_02_0a_desc = {
	.name = "42 02 0a",
	.on = xiaomi_l12_42_on,
	.set_rate = xiaomi_l12_42_set_rate,
	.modes = xiaomi_l12_42_modes,
	.num_modes = ARRAY_SIZE(xiaomi_l12_42_modes),
	.reset_last_ms = 11,
	/*
	 * Vendor 0x91 command: first_line_bpg_offset 12. The standard DSC 1.1
	 * table (msm default) gives corrupted output on this DDIC; it is
	 * Novatek-based like the 36 02 0b.
	 */
	.rc_table = 1,
	.first_line_bpg_offset = 12,
};

/* The bootloader passes msm_drm.dsi_display0=qcom,mdss_dsi_<panel>... */
static const struct xiaomi_l12_desc *xiaomi_l12_detect(struct device *dev,
						      const struct xiaomi_l12_desc *dt)
{
	struct device_node *chosen;
	const char *bootargs = NULL;
	const struct xiaomi_l12_desc *desc = dt;

	chosen = of_find_node_by_path("/chosen");
	if (chosen) {
		of_property_read_string(chosen, "bootargs", &bootargs);
		if (bootargs && strstr(bootargs, "mdss_dsi_l12_42_02_0a"))
			desc = &xiaomi_l12_42_02_0a_desc;
		else if (bootargs && strstr(bootargs, "mdss_dsi_l12_36_02_0b"))
			desc = &xiaomi_l12_36_02_0b_desc;
		of_node_put(chosen);
	}
	dev_info(dev, "panel %s%s\n", desc->name,
		 desc == dt ? "" : " (from the bootloader)");
	return desc;
}

static int xiaomi_l12_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct xiaomi_l12 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct xiaomi_l12, panel,
				   &xiaomi_l12_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->vddio))
		return dev_err_probe(dev, PTR_ERR(ctx->vddio), "failed to get vddio\n");
	ctx->vddd = devm_regulator_get(dev, "vddd");
	if (IS_ERR(ctx->vddd))
		return dev_err_probe(dev, PTR_ERR(ctx->vddd), "failed to get vddd\n");
	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci))
		return dev_err_probe(dev, PTR_ERR(ctx->vci), "failed to get vci\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset-gpios\n");

	ctx->desc = xiaomi_l12_detect(dev, device_get_match_data(dev));

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = xiaomi_l12_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to create backlight\n");

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret)
		return ret;

	/* This panel only supports DSC */
	dsi->dsc = &ctx->dsc;
	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = 12;
	ctx->dsc.slice_width = 610;
	ctx->dsc.slice_count = 1220 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 10;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;
	{
		int table = dsc_rc >= 0 ? dsc_rc : ctx->desc->rc_table;
		u8 fl = dsc_fl > 0 ? dsc_fl : ctx->desc->first_line_bpg_offset;

		/* table 0: leave rc_range_params empty, msm uses DSC 1.1 */
		if (table > 0)
			xiaomi_l12_set_rc_params(&ctx->dsc, table, fl);
		dev_info(dev, "DSC rate control: table %d, first_line_bpg_offset %u\n",
			 table, table > 0 ? fl : 12);
	}

	return devm_mipi_dsi_attach(dev, dsi);
}

static const struct of_device_id xiaomi_l12_of_match[] = {
	{ .compatible = "xiaomi,l12-36-02-0b", .data = &xiaomi_l12_36_02_0b_desc },
	{ .compatible = "xiaomi,l12-42-02-0a", .data = &xiaomi_l12_42_02_0a_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, xiaomi_l12_of_match);

static struct mipi_dsi_driver xiaomi_l12_driver = {
	.probe = xiaomi_l12_probe,
	.driver = {
		.name = "panel-xiaomi-l12-36-02-0b",
		.of_match_table = xiaomi_l12_of_match,
	},
};
module_mipi_dsi_driver(xiaomi_l12_driver);

MODULE_DESCRIPTION("DRM driver for the Xiaomi 12T Pro 36 02 0b / 42 02 0a DSC command mode panels");
MODULE_LICENSE("GPL");
