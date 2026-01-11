#include <linux/module.h>
#include <linux/of.h>


#include <drm/drm_modes.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

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

static int vs035_probe(struct mipi_dsi_device *dsi) {
    
    struct vs035_ctx *ctx;
    int ret;

    // Allocate memory for vs035_ctx object
    ctx = devm_kzalloc(dsi, sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;
    
    // Set up drm_panel instance within vs035_ctx object
    drm_panel_init(&ctx->panel, dsi, &vs035_funcs, DRM_MODE_CONNECTOR_DSI);

    // Register panel instance with DRM panel framework
    drm_panel_add(&(ctx->panel)); 

    // Device-specific specification
    dsi->lanes = 4;
    dsi->format = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = (MIPI_DSI_MODE_VIDEO | 
                       MIPI_DSI_MODE_VIDEO_BURST | 
                       MIPI_DSI_MODE_LPM);

    ret = mipi_dsi_attach(dsi);

    if (ret)
		dev_err(&dsi->dev, "failed to attach dsi to host: %d\n", ret);
    
	return ret;
}

static void vs035_remove(struct mipi_dsi_device *dsi) {
    struct vs035_ctx *ctx = mipi_dsi_get_drvdata(dsi);
}

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
