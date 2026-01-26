// Ultra-minimal DRM MIPI-DSI panel "hello world"
// - No GPIOs, no DCS writes, no delays
// - Registers a panel, sets lanes/format/mode_flags, exposes one mode

#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/device.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <linux/regulator/consumer.h>


struct vs035_ctx {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    bool prepared;
    struct regulator *vpos; 
    struct regulator *vneg; 
};

static inline struct vs035_ctx *to_ctx(struct drm_panel *p)
{
    return container_of(p, struct vs035_ctx, panel);
}

/* No-op power up/down (kept so the panel API is satisfied) */
static int vs035_prepare(struct drm_panel *panel)
{
    struct vs035_ctx *ctx = to_ctx(panel);
    if (ctx->prepared) return 0;
    ctx->prepared = true;
    return 0;
}
static int vs035_unprepare(struct drm_panel *panel)
{
    struct vs035_ctx *ctx = to_ctx(panel);
    if (!ctx->prepared) return 0;   
    ctx->prepared = false;
    return 0;
}
static int vs035_enable(struct drm_panel *panel)  { return 0; }
static int vs035_disable(struct drm_panel *panel) { return 0; }

/* One fixed mode (placeholder porches/clock; replace later) */
static int vs035_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
    struct drm_display_mode *m = drm_mode_create(connector->dev);
    if (!m) return 0;

    m->hdisplay    = 1440;
    m->hsync_start = 1440 + 16;   /* HFP */
    m->hsync_end   = m->hsync_start + 8;  /* HSYNC */
    m->htotal      = m->hsync_end + 16;   /* HBP */

    m->vdisplay    = 1600;
    m->vsync_start = 1600 + 8;    /* VFP */
    m->vsync_end   = m->vsync_start + 4;  /* VSYNC */
    m->vtotal      = m->vsync_end + 8;    /* VBP */

    m->clock       = (m->htotal * m->vtotal * 60) / 1000; /* 60 Hz */

    m->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
    drm_mode_set_name(m);
    drm_mode_probed_add(connector, m);
    return 1;
}

static const struct drm_panel_funcs vs035_funcs = {
    .prepare   = vs035_prepare,
    .unprepare = vs035_unprepare,
    .enable    = vs035_enable,
    .disable   = vs035_disable,
    .get_modes = vs035_get_modes,
};

/* DSI driver: bind with default link settings, no DCS */
static int vs035_probe(struct mipi_dsi_device *dsi)
{
    struct device *dev = &dsi->dev;

    struct vs035_ctx *ctx;
    int ret;

    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx) return -ENOMEM;

    ctx->dsi = dsi;
    mipi_dsi_set_drvdata(dsi, ctx);

    dsi->lanes      = 4;
    dsi->format     = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

    ctx->vpos = devm_regulator_get(dev, "vpos");
    if (IS_ERR(ctx->vpos)) return dev_err_probe(dev, PTR_ERR(ctx->vpos), "failed to get vpos\n");
    ctx->vneg = devm_regulator_get(dev, "vneg");
    if (IS_ERR(ctx->vneg)) return dev_err_probe(dev, PTR_ERR(ctx->vneg), "failed to get vneg\n");

    drm_panel_init(&ctx->panel, dev, &vs035_funcs, DRM_MODE_CONNECTOR_DSI);
    drm_panel_add(&ctx->panel);


    ret = mipi_dsi_attach(dsi);
    if (ret) {
        drm_panel_remove(&ctx->panel);
        return ret;
    }
    return 0;
}

static void vs035_remove(struct mipi_dsi_device *dsi)
{
    struct vs035_ctx *ctx = mipi_dsi_get_drvdata(dsi);
    mipi_dsi_detach(dsi);
    drm_panel_remove(&ctx->panel);
}

/* OF match + driver boilerplate */
static const struct of_device_id vs035_of_match[] = {
    { .compatible = "boe,vs035zsm" }, { }
};
MODULE_DEVICE_TABLE(of, vs035_of_match);

static struct mipi_dsi_driver vs035_driver = {
    .probe  = vs035_probe,
    .remove = vs035_remove,
    .driver = {
        .name = "panel-vs035zsm",
        .of_match_table = vs035_of_match,
    },
};

module_mipi_dsi_driver(vs035_driver);

MODULE_DESCRIPTION("VS035ZSM ultra-minimal DRM MIPI-DSI panel");
MODULE_LICENSE("GPL");

