// Ultra-minimal DRM MIPI-DSI panel "hello world"
// - No GPIOs, no DCS writes, no delays
// - Registers a panel, sets lanes/format/mode_flags, exposes one mode

#include <linux/module.h>
#include <linux/of.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

/* =======================================================================
 * DISPLAY LAYER (DRM panel API)
 * - This is the interface the graphics stack (DRM/KMS) talks to.
 * - DRM will call prepare/enable/get_modes/etc when it wants the panel on.
 * ======================================================================= */

 // Shared state used by both the display code and the driver code.
struct vs035_ctx {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    
    /* Power / reset */
    struct regulator *iovdd;          /* 1.8V I/O rail */
    struct gpio_desc *iovdd_en_gpio;  /* alternatively can enable the pin to hardcode it */

    struct gpio_desc *reset_gpio;     /* RESX (external reset pin which stops the chip from running, not necessarily preventing power off), active-low (meaning low voltage = ON) */

    /* Bias rails (+5.7/-5.7) controlled by external IC */
    struct regmap *bias_regmap;      
    struct gpio_desc *bias_en_gpio;  
    /* optional: pgood */
    struct gpio_desc *bias_pgood_gpio;

    /* Backlight */
    struct backlight_device *backlight; 
    struct gpio_desc *bl_en_gpio;        /* alternatively we just hardcode */

    bool prepared;
    bool enabled;
};

static inline struct vs035_ctx *to_ctx(struct drm_panel *p)
{
    return container_of(p, struct vs035_ctx, panel);
}

static int vs035_prepare(struct drm_panel *panel)
{
    struct vs035_ctx *ctx = to_ctx(panel);

    // if already prepared, stop
    if (ctx->prepared) return 0;

    // TODO: turn off I/O logic rail
    // TODO: turn off analog bias rails
    // TODO: hold panel/bridge while power unstable

    // TODO: turn on I/O logic rail

    // TODO: turn on bias/panel rails

    // TODO: release reset
    
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

/* =======================================================================
 * DRIVER LAYER (Linux driver model + MIPI-DSI bus)
 * - This is how the kernel binds your code to a DT node and calls probe().
 * - probe() bridges DRIVER layer (DSI device) to DISPLAY layer (drm_panel).
 * ======================================================================= */

//
static int vs035_probe(struct mipi_dsi_device *dsi) {
    struct device *dev = &dsi->dev;
    struct vs035_ctx *ctx;
    int ret;

    // allocate and store state
    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx) return -ENOMEM;

    ctx->dsi = dsi;
    mipi_dsi_set_drvdata(dsi, ctx);

    // TODO: verify DSI link parameters are set correctly
    dsi->lanes      = 4;
    dsi->format     = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

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

// informally, our overlay adds "boe,vs035zsm" ("key") to the device tree on boot. Setting this param HERE associates "boe,vs035zsm" with this driver.
// Thus, whenever the kernel boots, it uses this as the "value"
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
