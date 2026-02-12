// SPDX-License-Identifier: GPL-2.0
/*
 * BOE VS035ZSM MIPI DSI Panel Driver
 *
 * 1440x1600 @ 60Hz, 4-lane MIPI DSI, RGB888, Video Burst Mode
 * Uses TPS65132 for ±5.7V bias supply (direct I2C programming)
 *
 * Ported from SSD2828 reference implementation.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/pwm.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct vs035zsm {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vddio_gpio;
	struct gpio_desc *vpos_gpio;   /* TPS65132 ENP */
	struct gpio_desc *vneg_gpio;   /* TPS65132 ENN */
	struct pwm_device *pwm;
	struct i2c_client *tps_client;
	struct i2c_adapter *tps_adap;
	bool prepared;
	bool enabled;
};

static inline struct vs035zsm *panel_to_vs035zsm(struct drm_panel *panel)
{
	return container_of(panel, struct vs035zsm, panel);
}

#define TPS65132_REG_VPOS    0x00
#define TPS65132_REG_VNEG    0x01
#define TPS65132_REG_CTL     0xFF
#define TPS65132_VOLTAGE_57V 0x11

static int vs035zsm_tps65132_check_and_program(struct vs035zsm *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	struct i2c_client *client = ctx->tps_client;
	int vpos, vneg, ret;

	if (!client)
		return -ENODEV;

	vpos = i2c_smbus_read_byte_data(client, TPS65132_REG_VPOS);
	vneg = i2c_smbus_read_byte_data(client, TPS65132_REG_VNEG);

	if (vpos < 0 || vneg < 0) {
		dev_err(dev, "TPS65132 read failed: vpos=%d vneg=%d\n",
			vpos, vneg);
		return vpos < 0 ? vpos : vneg;
	}

	dev_info(dev, "TPS65132: VPOS=0x%02x VNEG=0x%02x\n", vpos, vneg);

	if (vpos == TPS65132_VOLTAGE_57V && vneg == TPS65132_VOLTAGE_57V)
		return 0;

	dev_info(dev, "TPS65132: programming 5.7V + EEPROM burn\n");

	ret = i2c_smbus_write_byte_data(client, TPS65132_REG_VPOS,
					TPS65132_VOLTAGE_57V);
	if (ret)
		return ret;

	ret = i2c_smbus_write_byte_data(client, TPS65132_REG_VNEG,
					TPS65132_VOLTAGE_57V);
	if (ret)
		return ret;

	ret = i2c_smbus_write_byte_data(client, TPS65132_REG_CTL, 0x80);
	if (ret)
		return ret;

	msleep(50);

	vpos = i2c_smbus_read_byte_data(client, TPS65132_REG_VPOS);
	vneg = i2c_smbus_read_byte_data(client, TPS65132_REG_VNEG);
	dev_info(dev, "TPS65132 verify: VPOS=0x%02x VNEG=0x%02x\n",
		 vpos, vneg);

	return 0;
}

// /* ------------------------------------------------------------------ */
// /* Panel init sequence — ported from VS035ZSM_start()                 */
// /* All commands sent in LP mode (host handles this via MODE_LPM flag) */
// /* ------------------------------------------------------------------ */
// static int vs035zsm_init_sequence(struct vs035zsm *ctx)
// {
// 	struct mipi_dsi_device *dsi = ctx->dsi;
// 	struct device *dev = &dsi->dev;
// 	int ret;

// 	dev_info(dev, "vs035zsm_init_sequence\n");

// 	/* --- HSSRAM parameter (page 0xE0) --- */
// 	mipi_dsi_dcs_write(dsi, 0xFF, (u8[]){0xE0}, 1);
// 	mipi_dsi_dcs_write(dsi, 0xFB, (u8[]){0x01}, 1);  /* RELOAD */
// 	mipi_dsi_dcs_write(dsi, 0x53, (u8[]){0x22}, 1);

// 	/* --- CDM2 settings (page 0x25) --- */
// 	mipi_dsi_dcs_write(dsi, 0xFF, (u8[]){0x25}, 1);
// 	mipi_dsi_dcs_write(dsi, 0xFB, (u8[]){0x01}, 1);
// 	mipi_dsi_dcs_write(dsi, 0x65, (u8[]){0x01}, 1);
// 	mipi_dsi_dcs_write(dsi, 0x66, (u8[]){0x50}, 1);
// 	mipi_dsi_dcs_write(dsi, 0x67, (u8[]){0x55}, 1);  /* 10% duty cycle setting */
// 	mipi_dsi_dcs_write(dsi, 0xC4, (u8[]){0x90}, 1);

// 	/* --- Page 0x26 settings --- */
// 	mipi_dsi_dcs_write(dsi, 0xFF, (u8[]){0x26}, 1);
// 	mipi_dsi_dcs_write(dsi, 0xFB, (u8[]){0x01}, 1);
// 	mipi_dsi_dcs_write(dsi, 0x02, (u8[]){0xB5}, 1);
// 	mipi_dsi_dcs_write(dsi, 0x4D, (u8[]){0x8B}, 1);

// 	/* --- User command set (page 0x10) --- */
// 	mipi_dsi_dcs_write(dsi, 0xFF, (u8[]){0x10}, 1);
// 	mipi_dsi_dcs_write(dsi, 0xFB, (u8[]){0x01}, 1);

// 	/* VESA DSC setting */
// 	// mipi_dsi_dcs_write(dsi, 0xC0, (u8[]){0x80}, 1); // original, seems to enable DSC
// 	mipi_dsi_dcs_write(dsi, 0xC0, (u8[]){0x00}, 1);

// 	/*
// 	 * NOTE: These two generic long writes were commented out in the
// 	 * SSD2828 reference code. Including them here since they appear
// 	 * to be DSC-related config. Remove if display misbehaves.
// 	 *
// 	 * 0x3B with payload {0x00, 0x0A, 0x00, 0x0A}
// 	 * 0xBE with payload {0x00, 0x0A, 0x00, 0x0A}
// 	 */
// 	// mipi_dsi_generic_write(dsi, (u8[]){0x3B, 0x00, 0x0A, 0x00, 0x0A}, 5);
// 	// mipi_dsi_generic_write(dsi, (u8[]){0xBE, 0x00, 0x0A, 0x00, 0x0A}, 5);

// 	/* Compression / stream config */
// 	// mipi_dsi_dcs_write(dsi, 0xBB, (u8[]){0x13}, 1); // original, seems to enable DSC
// 	mipi_dsi_dcs_write(dsi, 0xBB, (u8[]){0x03}, 1);

// 	/*
// 	 * BA register: 0x30 = dual port, 0x07 = single port
// 	 * Using single port for our setup.
// 	 */
// 	mipi_dsi_dcs_write(dsi, 0xBA, (u8[]){0x07}, 1);

// 	/* Tear effect on (TE pin output) */
// 	mipi_dsi_dcs_write(dsi, 0x35, (u8[]){0x00}, 1);

// 	/* Address mode: normal scan */
// 	mipi_dsi_dcs_write(dsi, 0x36, (u8[]){0x00}, 1);

// 	/*
// 	 * Page address set (0x2B): rows 0 to 1600 (0x0640)
// 	 * The SSD2828 reference labels this "PARTIAL_RES_X" but
// 	 * 0x2B is PASET which sets the vertical range.
// 	 */
// 	{
// 		u8 payload[] = {0x00, 0x00, 0x06, 0x40};
// 		// mipi_dsi_dcs_write(dsi, 0x2B, payload, 4);
// 	}

// 	/* In init sequence, after page 0x10 select and RELOAD */
// 	{
// 		u8 readback = 0xFF;

// 		/* Try to disable DSC */
// 		mipi_dsi_dcs_write(dsi, 0xC0, (u8[]){0x00}, 1);
// 		msleep(10);
// 		mipi_dsi_dcs_read(dsi, 0xC0, &readback, 1);
// 		dev_info(dev, "0xC0 after write 0x00: 0x%02x\n", readback);

// 		mipi_dsi_dcs_write(dsi, 0xBB, (u8[]){0x03}, 1);
// 		msleep(10);
// 		mipi_dsi_dcs_read(dsi, 0xBB, &readback, 1);
// 		dev_info(dev, "0xBB after write 0x03: 0x%02x\n", readback);
// 	}

// 	/* Also read compression mode via standard DCS command */
// 	{
// 		u8 comp_mode = 0xFF;
// 		mipi_dsi_dcs_read(dsi, 0x03, &comp_mode, 1);
// 		dev_info(dev, "Get compression mode for DCS (0x03): 0x%02x\n", comp_mode);
// 	}

// 	{
// 		u8 ba_val = 0xFF;
// 		mipi_dsi_dcs_read(dsi, 0xBA, &ba_val, 1);
// 		dev_info(dev, "Port config (0xBA): 0x%02x\n", ba_val);
// 	}

// 	/* Sleep Out */
// 	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
// 	if (ret < 0) {
// 		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
// 		return ret;
// 	}

// 	/* SSD2828 reference waits 200ms here */
// 	msleep(200);

// 	return 0;
// }

static int vs035zsm_init_sequence(struct vs035zsm *ctx)
{
    struct mipi_dsi_device *dsi = ctx->dsi;
    int ret;

    mipi_dsi_dcs_write(dsi, 0xFF, (u8[]){0x10}, 1);
    mipi_dsi_dcs_write(dsi, 0xFB, (u8[]){0x01}, 1);

    mipi_dsi_dcs_write(dsi, 0xC0, (u8[]){0x00}, 1);
    mipi_dsi_dcs_write(dsi, 0xBB, (u8[]){0x03}, 1);
    mipi_dsi_dcs_write(dsi, 0xBA, (u8[]){0x07}, 1);

    mipi_dsi_dcs_write(dsi, 0x36, (u8[]){0x00}, 1);
    mipi_dsi_dcs_write(dsi, 0x3A, (u8[]){0x77}, 1);
    mipi_dsi_dcs_write(dsi, 0x35, (u8[]){0x00}, 1);

    ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
    if (ret < 0)
        return ret;
    msleep(120);

    return 0;
}

/* ------------------------------------------------------------------ */
/* DRM panel callbacks                                                */
/* ------------------------------------------------------------------ */

static int vs035zsm_prepare(struct drm_panel *panel)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);
	struct device *dev = &ctx->dsi->dev;
	// struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	if (ctx->prepared)
		return 0;

	/*
	 * Power-on sequence per panel spec:
	 *
	 * 1. VDDIO (1.8V logic) must be up first
	 * 2. VPOS (ENP HIGH) — TPS65132 wakes, loads EEPROM, starts VPOS
	 * 3. VNEG (ENN HIGH) — starts VNEG rail
	 * 4. Wait for supplies to stabilize
	 * 5. Check/program TPS65132 via I2C (chip is alive after ENP HIGH)
	 * 6. Assert reset
	 * 7. Init sequence
	 */

	/* 1. VDDIO */
	if (ctx->vddio_gpio)
		gpiod_set_value_cansleep(ctx->vddio_gpio, 1);
	msleep(50);

	/* 2. ENP — TPS65132 starts, loads EEPROM into DAC */
	if (ctx->vpos_gpio)
		gpiod_set_value_cansleep(ctx->vpos_gpio, 1);
	msleep(50);

	/* 3. ENN */
	if (ctx->vneg_gpio)
		gpiod_set_value_cansleep(ctx->vneg_gpio, 1);
	msleep(50);  /* TPS65132 soft-start + stabilization */

	/* 4. Verify/program TPS65132 — only needed once, saved to EEPROM */
	ret = vs035zsm_tps65132_check_and_program(ctx);
	if (ret)
		dev_warn(dev, "TPS65132 check failed: %d (continuing)\n", ret);

	msleep(50);
	
	/* 5. Assert reset (active high, held high for operation) */
	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(10);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	}
	msleep(200);

	

	ctx->prepared = true;
	return 0;

err_power_off:
	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(5);
	if (ctx->vneg_gpio)
		gpiod_set_value_cansleep(ctx->vneg_gpio, 0);
	msleep(5);
	if (ctx->vpos_gpio)
		gpiod_set_value_cansleep(ctx->vpos_gpio, 0);
	msleep(5);
	if (ctx->vddio_gpio)
		gpiod_set_value_cansleep(ctx->vddio_gpio, 0);
	return ret;
}

static int vs035zsm_enable(struct drm_panel *panel)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->enabled)
		return 0;

	ret = vs035zsm_init_sequence(ctx);
	if (ret) {
		dev_err(dev, "Init sequence failed: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write(ctx->dsi, 0xFF, (u8[]){0x10}, 1);

	ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(80);

	/* Debug reads */
	{
		u8 power_mode = 0;
		u8 display_status[5] = {0};
		u8 dsc_en = 0xFF;
		u8 bb_val = 0xFF;

		mipi_dsi_dcs_read(ctx->dsi, 0x0A, &power_mode, 1);
		dev_info(dev, "Power mode (0x0A): 0x%02x\n", power_mode);

		mipi_dsi_dcs_read(ctx->dsi, 0x09, display_status, 5);
		dev_info(dev, "Display status (0x09): %02x %02x %02x %02x %02x\n",
			 display_status[0], display_status[1],
			 display_status[2], display_status[3],
			 display_status[4]);

		mipi_dsi_dcs_read(ctx->dsi, 0xC0, &dsc_en, 1);
		mipi_dsi_dcs_read(ctx->dsi, 0xBB, &bb_val, 1);
		dev_info(dev, "DSC (0xC0): 0x%02x  Compression (0xBB): 0x%02x\n",
			 dsc_en, bb_val);
	}

	ctx->enabled = true;
	dev_info(dev, "Display enabled\n");
	return 0;
}

static int vs035zsm_disable(struct drm_panel *panel)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	dev_info(dev, "vs035zsm_disable\n");

	if (!ctx->enabled)
		return 0;

	/* Display Off */
	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_warn(dev, "Failed to set display off: %d\n", ret);

	msleep(20);

	/* Enter Sleep */
	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_warn(dev, "Failed to enter sleep mode: %d\n", ret);

	/* Must wait >= 120ms after sleep in per MIPI spec */
	msleep(120);

	ctx->enabled = false;
	return 0;
}

static int vs035zsm_unprepare(struct drm_panel *panel)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);

	dev_info(&ctx->dsi->dev, "vs035zsm_unprepare\n");

	if (!ctx->prepared)
		return 0;

	/* Deassert reset */
	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	msleep(10);

	/*
	 * TODO: Power down TPS65132 (reverse order: VNEG then VPOS to 0,
	 * or use enable GPIO / regulator framework). For now the supplies
	 * stay on.
	 */

	ctx->prepared = false;
	return 0;
}

static int vs035zsm_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct vs035zsm *ctx = panel_to_vs035zsm(panel);
	dev_info(&ctx->dsi->dev, "vs035zsm_get_modes\n");
	/*
	 * VS035ZSM native timing:
	 *   1440 x 1600 @ 60 Hz
	 *   HSYNC=40  HFP=80  HBP=80  → htotal=1640
	 *   VSYNC=40  VFP=40  VBP=40  → vtotal=1720
	 *   pixel clock = 1640 * 1720 * 60 / 1000 = 169,296 kHz
	 *
	 * SSD2828 reference PLL: 912 MHz for 4 lanes
	 *   lane rate = 912 Mbps/lane → total = 3648 Mbps
	 *   RGB888 = 24 bpp → 3648 / 24 = 152 Mpix/s effective
	 *   In burst mode this is fine for ~169 Mpix/s active + blanking
	 */
	#define VS035ZSM_HDISPLAY    1440
	#define VS035ZSM_HFP         80
	#define VS035ZSM_HSYNC       40
	#define VS035ZSM_HBP         80
	#define VS035ZSM_VDISPLAY    1600
	#define VS035ZSM_VFP         20
	#define VS035ZSM_VSYNC       40
	#define VS035ZSM_VBP         20
	#define VS035ZSM_FPS         20

	#define VS035ZSM_HTOTAL      (VS035ZSM_HDISPLAY + VS035ZSM_HFP + VS035ZSM_HSYNC + VS035ZSM_HBP)
	#define VS035ZSM_VTOTAL      (VS035ZSM_VDISPLAY + VS035ZSM_VFP + VS035ZSM_VSYNC + VS035ZSM_VBP)
	#define VS035ZSM_CLOCK       (VS035ZSM_HTOTAL * VS035ZSM_VTOTAL * VS035ZSM_FPS / 1000)

	static const struct drm_display_mode mode = {
		.clock       = VS035ZSM_CLOCK,
		.hdisplay    = VS035ZSM_HDISPLAY,
		.hsync_start = VS035ZSM_HDISPLAY + VS035ZSM_HFP,
		.hsync_end   = VS035ZSM_HDISPLAY + VS035ZSM_HFP + VS035ZSM_HSYNC,
		.htotal      = VS035ZSM_HTOTAL,
		.vdisplay    = VS035ZSM_VDISPLAY,
		.vsync_start = VS035ZSM_VDISPLAY + VS035ZSM_VFP,
		.vsync_end   = VS035ZSM_VDISPLAY + VS035ZSM_VFP + VS035ZSM_VSYNC,
		.vtotal      = VS035ZSM_VTOTAL,
		.width_mm    = 59,
		.height_mm   = 66,
		.type        = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	};

	// static const struct drm_display_mode mode = {
	// 	/* 
	// 	 * Recalculated Clock for 60Hz:
	// 	 * Htotal (1720) * Vtotal (1640) * 60Hz = 169,248 kHz 
	// 	 */
	// 	.clock       = 169296,
		
	// 	/* Horizontal (formerly Vertical) */
	// 	.hdisplay    = 1600,
	// 	.hsync_start = 1600 + 40,      /* Was VFP */
	// 	.hsync_end   = 1600 + 40 + 40, /* Was VSync Length */
	// 	.htotal      = 1600 + 40 + 40 + 40, /* Was VBP */
		
	// 	/* Vertical (formerly Horizontal) */
	// 	.vdisplay    = 1440,
	// 	.vsync_start = 1440 + 80,      /* Was HFP */
	// 	.vsync_end   = 1440 + 80 + 40, /* Was HSync Length */
	// 	.vtotal      = 1440 + 80 + 40 + 80, /* Was HBP */
		
	// 	/* Physical Dimensions Swapped */
	// 	.width_mm    = 66,
	// 	.height_mm   = 59,
		
	// 	.type        = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	// };


	// static const struct drm_display_mode mode = {
	// 	.clock       = 169296, 
	// 	.hdisplay    = 1440,
	// 	.hsync_start = 1440 + 80,
	// 	.hsync_end   = 1440 + 80 + 40,
	// 	.htotal      = 1440 + 80 + 40 + 80,
	// 	.vdisplay    = 1600,
	// 	.vsync_start = 1600 + 40,
	// 	.vsync_end   = 1600 + 40 + 40,
	// 	.vtotal      = 1600 + 40 + 40 + 40,
	// 	.width_mm    = 59,
	// 	.height_mm   = 66,
	// 	// .flags       = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	// 	.type        = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	// };
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
	.prepare   = vs035zsm_prepare,
	.enable    = vs035zsm_enable,
	.disable   = vs035zsm_disable,
	.unprepare = vs035zsm_unprepare,
	.get_modes = vs035zsm_get_modes,
};

/* ------------------------------------------------------------------ */
/* MIPI DSI driver probe / remove                                     */
/* ------------------------------------------------------------------ */

static int vs035zsm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct vs035zsm *ctx;
	int ret;
	struct i2c_board_info tps_info = {
		I2C_BOARD_INFO("tps65132_raw", 0x3e),
	};

	dev_info(dev, "VS035ZSM driver compiled on %s\n", BUILD_TIMESTAMP);

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	/* Power GPIOs — all start LOW (off) */
	ctx->vddio_gpio = devm_gpiod_get_optional(dev, "vddio", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->vddio_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->vddio_gpio),
				     "Failed to get vddio GPIO\n");

	ctx->vpos_gpio = devm_gpiod_get_optional(dev, "vpos", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->vpos_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->vpos_gpio),
				     "Failed to get vpos GPIO\n");

	ctx->vneg_gpio = devm_gpiod_get_optional(dev, "vneg", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->vneg_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->vneg_gpio),
				     "Failed to get vneg GPIO\n");

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset GPIO\n");

	/* TPS65132 I2C — create client now, communicate later in prepare */
	ctx->tps_adap = i2c_get_adapter(10);
	if (!ctx->tps_adap) {
		dev_warn(dev, "No I2C adapter 10 — TPS65132 control unavailable\n");
	} else {
		ctx->tps_client = i2c_new_client_device(ctx->tps_adap, &tps_info);
		if (IS_ERR(ctx->tps_client)) {
			dev_warn(dev, "Failed to create TPS65132 client: %ld\n",
				 PTR_ERR(ctx->tps_client));
			ctx->tps_client = NULL;
			i2c_put_adapter(ctx->tps_adap);
			ctx->tps_adap = NULL;
		}
	}

	/* PWM backlight */
	ctx->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(ctx->pwm)) {
		ret = PTR_ERR(ctx->pwm);
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_warn(dev, "No PWM backlight: %d\n", ret);
		ctx->pwm = NULL;
	}

	if (ctx->pwm) {
		struct pwm_state state;
		pwm_init_state(ctx->pwm, &state);
		state.period     = 20000;
		state.duty_cycle = 1000;
		state.enabled    = true;
		ret = pwm_apply_might_sleep(ctx->pwm, &state);
		if (ret) {
			dev_err(dev, "Failed to apply PWM: %d\n", ret);
			return ret;
		}
	}

	dsi->lanes      = 4;
	dsi->format     = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
			// | MIPI_DSI_MODE_VIDEO_BURST
			| MIPI_DSI_MODE_LPM;
	// dsi->mode_flags = MIPI_DSI_MODE_LPM;


	drm_panel_init(&ctx->panel, dev, &vs035zsm_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		dev_err(dev, "Failed to attach DSI: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "VS035ZSM probed\n");
	return 0;
}

static void vs035zsm_remove(struct mipi_dsi_device *dsi)
{
	struct vs035zsm *ctx = mipi_dsi_get_drvdata(dsi);

	dev_info(&dsi->dev, "vs035zsm_remove\n");

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	/* Kill backlight */
	if (ctx->pwm) {
		struct pwm_state state;

		pwm_get_state(ctx->pwm, &state);
		state.enabled = false;
		pwm_apply_might_sleep(ctx->pwm, &state);
	}
}

static const struct of_device_id vs035zsm_of_match[] = {
	{ .compatible = "boe,vs035zsm" },
	{ }
};
MODULE_DEVICE_TABLE(of, vs035zsm_of_match);

static struct mipi_dsi_driver vs035zsm_driver = {
	.driver = {
		.name           = "panel-vs035zsm",
		.of_match_table = vs035zsm_of_match,
	},
	.probe  = vs035zsm_probe,
	.remove = vs035zsm_remove,
};
module_mipi_dsi_driver(vs035zsm_driver);

MODULE_DESCRIPTION("BOE VS035ZSM MIPI DSI Panel Driver");
MODULE_LICENSE("GPL");