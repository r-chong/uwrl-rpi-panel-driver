#include <linux/module.h>
#include <linux/of.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

static const struct of_device_id vs035_of_ids[] = {
	{ .compatible = "raspberrypi,7inch-touchscreen-panel" },
	{ } /* sentinel */  // <- copied from rpi touchscreen, idk what this is
};
MODULE_DEVICE_TABLE(vs035_of_ids);

struct vs035_ctx {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    bool prepared;
    // Might need more stuff here...
};

static inline struct vs035_ctx *to_ctx(struct drm_panel *panel) {
    return container_of(panel, struct vs_035_ctx, panel);
}

static int vs035_probe(struct mipi_dsi_device *dsi) {
    // To specify (random values for now):
    dsi->lanes = 0;
    dsi->format = MIPI_DSI_FMT_RGB565;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO;

    mipi_dsi_attach(dsi);

    return 0;
}
static void vs035_remove(struct mipi_dsi_device *dsi) {}
static int vs035_prepare(struct drm_panel *panel) { return 0; }
static int vs035_unprepare(struct drm_panel *panel) { return 0; }
static int vs035_enable(struct drm_panel *panel) { return 0; }
static int vs035_disable(struct drm_panel *panel) { return 0; }
static int vs035_get_modes(struct drm_panel *panel, struct drm_connector *connector) { return 0; }

static const struct drm_panel_funcs vs035_funcs = {
    .prepare   = vs035_prepare,
    .unprepare = vs035_unprepare,
    .enable    = vs035_enable,
    .disable   = vs035_disable,
    .get_modes = vs035_get_modes,
};

static struct mipi_dsi_driver vs035_driver = {
    .driver = {
        .name = "panel-boe-vs035zsm-nw0-69p0",
        .of_match_table = vs035_of_ids
    },
    .probe = vs035_probe,
    .remove = vs035_remove
};

module_mipi_dsi_driver(vs035_driver);

MODULE_AUTHOR("author");
MODULE_DESCRIPTION("BOE VS035ZSM NW0 69P0 1440x1600 video mode panel driver");
MODULE_LICENSE("GPL");
