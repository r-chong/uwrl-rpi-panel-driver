// MVP for display driver

#include <linux/module.h>
#include <linux/of.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

// private driver's state for managing the display panel instance
// Embeds a generic DRM panel structure
// Stores a pointer to the underlying MIPI-DSI device
// Tracks whether the panel is prepared (powered on)
struct vs035_ctx {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	bool prepared;
};

// container_of: get pointer of parent when you have the pointer of a child
// inline: find-and-replace function body instead of function call
static inline struct vs035_ctx *to_ctx(struct drm_panel *p) {
	return container_of(p, struct vs035_ctx, panel);
}

// these functions don’t actually do anything, but they exist because the DRM panel driver framework expects them
// (drm_panel_funcs uses them)
static int vs035_prepare(struct drm_panel *panel){
	struct vs035_ctx *ctx = to_ctx(panel);
	if (ctx->prepared) return 0;
	ctx->prepared = true;
	return 0;
}

static int vs035_unprepared(struct drm_panel *panel){
	struct vs035_ctx *ctx = to_ctx(panel);
	if (!ctx->prepared) return 0;
	ctx->prepared = false;
	return 0;
}

static int vs035_enable(struct drm_panel *panel) {return 0; }

static int vs035_disable(struct drm_panel *panel) { return 0; }

// placeholder porches/clock
static int vs035_get_modes(struct drm_panel *panel, struct drm_connector *connector) {
	struct drm_display_node *m = drm_mode_create(connector->dev);
	if (!m) return 0;

	m->display	= 1440;
	m->hsync_start	= 1440 + 16; /* HFP */
	m->hsync_end	= m->hsync_start + 8; /* HSYNC */
	m->htotal	= m->hsync_end + 16; /* HBP */

	m->vdisplay	= 1600;
	m->vsync_start	= 1600 + 8; /* VFP */
	m->vsync_end	= m->vsync_start + 4;
	m->vtotal	= m->vsync_end + 8;

	m->clock	= (m->htotal * m->vtotal * 60) / 1000; /* 60Hz */

	m->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(m);
	drm_mode_probed_add(connector, m);
	return 1;
}

static const struct drm_panel_funcs vs035_funcs = {
	.prepare	= vs035_prepare,
	.unprepare	= vs035_unprepare,
	.enable		= vs035_enable,
	.disable	= vs035_disable,
	.get_modes	= vs035_get_modes,
}

// vs035 probe is init routine when MIPI-DSI first runs
static int vs035_probe(struct mipi_dsi_device *dsi) {
	// create pointer to reference's member, dev
	struct device *dev = &dsi->dev;
	struct vs035_ctx *ctx;
	int ret;

	// kzalloc - allocate memory on the heap all initialized to zero 
	// devm is device-managed -automatic cleanup when device not used
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) return -ENOMEM;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	// from datasheet:
	dsi->lanes		= 4;
	dsi->format		= MIPI_DSI_FMT_RGB888;
	dsi->mode_flags	= MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

	drm_panel_init(&ctx->panel, dev, &vs035_funcs, DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);

	if (ret) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}
	return 0;
}

static void vs035_remove(struct mipi_dsi_device *dsi) {
	struct vs035_ctx *ctx = mipi_dsi_get_drvdata(dsi);
	mipi_dsi_attach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id vs035_of_match[] = {
	{ .compatible = "boe,v035zsm" }, { }
}

MODULE_DEVICE_TABLE(of, vs035_of_match);

static struct mipi_dsi_driver vs035_driver = {
	.probe	= vs035_probe,
	.remove = vs035_remove,
	.driver	= {
		.name = "panel-vs035zsm",
		.of_match_table = vs035_of_match,
	}
};

module_mipi_dsi_driver(vs035_driver);

MODULE_DESCRIPTION("VS035ZSM ultra-minimal DRM MIPI-DSI panel");
MODULE_LICENSE("GPL");
