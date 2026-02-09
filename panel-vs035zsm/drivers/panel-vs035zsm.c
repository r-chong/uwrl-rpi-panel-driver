// SPDX-License-Identifier: GPL-2.0
/*
 * VS035ZSM Panel Minimal Test Driver
 *
 * Minimal driver to:
 * 1. Power on display via tps65132 regulator (±5.7V)
 * 2. Assert reset GPIO
 * 3. Read display ID via MIPI DCS LP mode
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct vs035zsm {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct vs035zsm *panel_to_vs035zsm(struct drm_panel *panel)
{
	return container_of(panel, struct vs035zsm, panel);
}

static int vs035zsm_read_display_info(struct vs035zsm *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	u8 id[3] = {0};
	u8 power_mode = 0;
	u8 address_mode = 0;
	int ret;

	/* Read Display ID (DCS command 0x04) - returns 3 bytes */
	ret = mipi_dsi_dcs_read(dsi, 0x04, id, sizeof(id));
	if (ret < 0) {
		dev_err(&dsi->dev, "Failed to read display ID: %d\n", ret);
	} else {
		dev_info(&dsi->dev, "Display ID: %02x %02x %02x\n",
			 id[0], id[1], id[2]);
	}

	/* Read Power Mode (DCS command 0x0A) - returns 1 byte */
	ret = mipi_dsi_dcs_read(dsi, 0x0A, &power_mode, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "Failed to read power mode: %d\n", ret);
	} else {
		dev_info(&dsi->dev, "Power Mode: 0x%02x\n", power_mode);
	}

	/* Read Address Mode (DCS command 0x0B) - returns 1 byte */
	ret = mipi_dsi_dcs_read(dsi, 0x0B, &address_mode, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "Failed to read address mode: %d\n", ret);
	} else {
		dev_info(&dsi->dev, "Address Mode: 0x%02x\n", address_mode);
	}

	return 0;
}

static int vs035zsm_prepare(struct drm_panel *panel)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);
	struct device *dev = &ctx->dsi->dev;
	
	if (ctx->prepared)
		return 0;

	/* Step 3: Wait 120ms */
	msleep(120);

	/* Step 4: Assert reset (pull high) */
	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		dev_info(dev, "Reset GPIO asserted (high)\n");
	}

	/* Step 5: Wait for display to stabilize (per example code: 25ms) */
	msleep(100);

	ctx->prepared = true;

	/* Read display info via MIPI LP mode */
	dev_info(dev, "Reading display information via MIPI LP mode...\n");
	vs035zsm_read_display_info(ctx);

	return 0;

	return 0;
}

static int vs035zsm_unprepare(struct drm_panel *panel)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);

	if (!ctx->prepared)
		return 0;

	/* Deassert reset */
	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	msleep(10);

	ctx->prepared = false;

	return 0;
}

static int vs035zsm_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	/*
	 * VS035ZSM Display Timing:
	 * Resolution: 1440 x 1600
	 * Active area: 59.4 x 66.0 mm
	 * HSYNC=40, HFP=80, HBP=80
	 * VSYNC=40, VFP=40, VBP=40
	 * htotal = 1440 + 80 + 40 + 80 = 1640
	 * vtotal = 1600 + 40 + 40 + 40 = 1720
	 */
	static const struct drm_display_mode mode = {
		.clock = 169296,        /* htotal * vtotal * 60Hz / 1000 */
		.hdisplay = 1440,
		.hsync_start = 1440 + 80,       /* hdisplay + HFP */
		.hsync_end = 1440 + 80 + 40,    /* + HSYNC */
		.htotal = 1440 + 80 + 40 + 80,  /* + HBP = 1640 */
		.vdisplay = 1600,
		.vsync_start = 1600 + 40,       /* vdisplay + VFP */
		.vsync_end = 1600 + 40 + 40,    /* + VSYNC */
		.vtotal = 1600 + 40 + 40 + 40,  /* + VBP = 1720 */
		.width_mm = 59,
		.height_mm = 66,
		.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	};
	struct drm_display_mode *m;

	m = drm_mode_duplicate(connector->dev, &mode);
	if (!m)
		return -ENOMEM;

	drm_mode_set_name(m);
	drm_mode_probed_add(connector, m);

	connector->display_info.width_mm = mode.width_mm;
	connector->display_info.height_mm = mode.height_mm;
	connector->display_info.bpc = 8;

	return 1;
}

static const struct drm_panel_funcs vs035zsm_panel_funcs = {
	.prepare = vs035zsm_prepare,
	.unprepare = vs035zsm_unprepare,
	.get_modes = vs035zsm_get_modes,
};

static int vs035zsm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct vs035zsm *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	/* Get reset GPIO */
	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "Failed to get reset GPIO: %d\n", ret);
		return ret;
	}

	/* Configure MIPI DSI */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &vs035zsm_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "VS035ZSM panel driver probed successfully\n");

	return 0;
}

static void vs035zsm_remove(struct mipi_dsi_device *dsi)
{
	struct vs035zsm *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id vs035zsm_of_match[] = {
	{ .compatible = "boe,vs035zsm" },
	{ }
};
MODULE_DEVICE_TABLE(of, vs035zsm_of_match);

static struct mipi_dsi_driver vs035zsm_driver = {
	.driver = {
		.name = "panel-vs035zsm",
		.of_match_table = vs035zsm_of_match,
	},
	.probe = vs035zsm_probe,
	.remove = vs035zsm_remove,
};
module_mipi_dsi_driver(vs035zsm_driver);

MODULE_AUTHOR("VS035ZSM Test Driver");
MODULE_DESCRIPTION("Minimal VS035ZSM MIPI DSI Panel Test Driver");
MODULE_LICENSE("GPL");
