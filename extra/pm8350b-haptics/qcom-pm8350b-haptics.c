// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal driver for the Qualcomm PM8350B high-voltage haptics block
 * (HAP_CFG / HAP_PTN / HAP_HBOOST), driving an LRA in DIRECT_PLAY mode.
 *
 * Register layout and sequence from the downstream qcom-hv-haptics driver
 * (haptics_hw_init(), haptics_load_constant_effect(), haptics_enable_play()).
 * Only constant vibration is supported: no FIFO/pattern effects, no
 * SoundWire play, no close-loop calibration and no hBoost keep-alive.
 */

#include <linux/bits.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* HAP_CFG */
#define HAP_CFG_REVISION2		0x01
#define HAP_CFG_EN_CTL			0x46
#define  HAPTICS_EN			BIT(7)
#define HAP_CFG_VMAX			0x48
#define  VMAX_STEP_MV			50
#define HAP_CFG_DRV_WF_SEL		0x49
#define  DRV_WF_FMT			BIT(4)
#define  DRV_WF_SEL_MASK		GENMASK(1, 0)
#define HAP_CFG_SPMI_PLAY		0x4c
#define  PLAY_EN			BIT(7)
#define  PAT_SRC_DIRECT_PLAY		1
#define HAP_CFG_TLRA_OL_HIGH		0x5c
#define  TLRA_STEP_US			5
#define  TLRA_MAX_US			20475
#define HAP_CFG_AUTORES_CFG		0x63
#define  AUTORES_EN			BIT(7)
#define HAP_CFG_FAULT_CLR		0x66
#define  FAULT_CLR_ALL			(BIT(2) | BIT(1) | BIT(0))
#define HAP_CFG_VSET_CFG		0x68
#define  FORCE_VREG_RDY			BIT(0)
#define HAP_CFG_CAL_EN			0x72
#define  CAL_RC_CLK_MASK		GENMASK(3, 2)
#define  CAL_RC_CLK_AUTO		(1 << 2)

/* HAP_PTN */
#define HAP_PTN_DIRECT_PLAY		0x26

/* HAP_HBOOST: 5 V clamp status, register depends on the hBoost revision */
#define HAP_BOOST_REVISION1		0x00
#define HAP_BOOST_V0P0_CLAMP		0xf1
#define HAP_BOOST_V0P1_CLAMP		0x70
#define  CLAMP_5V			BIT(0)

#define MAX_VMAX_MV			11000
#define CLAMPED_VMAX_MV			5000
/* this driver refuses anything higher (the diting runs at 1250 mV) */
#define SAFE_VMAX_MV			3600

struct pm8350b_haptics {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *input;
	struct work_struct work;
	u32 cfg, ptn, hbst;
	u32 vmax_mv;
	u8 level;		/* requested DIRECT_PLAY amplitude, 0 = off */
	bool playing;
};

static int hap_write(struct pm8350b_haptics *h, u32 base, u32 reg, u8 val)
{
	return regmap_write(h->regmap, base + reg, val);
}

static int hap_update(struct pm8350b_haptics *h, u32 base, u32 reg, u8 mask,
		      u8 val)
{
	return regmap_update_bits(h->regmap, base + reg, mask, val);
}

static int pm8350b_haptics_start(struct pm8350b_haptics *h, u8 level)
{
	int ret;

	ret = hap_write(h, h->cfg, HAP_CFG_FAULT_CLR, FAULT_CLR_ALL);
	if (ret)
		return ret;

	/* hBoost follows the play request (no keep-alive vote here) */
	ret = hap_update(h, h->cfg, HAP_CFG_VSET_CFG, FORCE_VREG_RDY, 0);
	if (ret)
		return ret;

	ret = hap_write(h, h->cfg, HAP_CFG_VMAX, h->vmax_mv / VMAX_STEP_MV);
	if (ret)
		return ret;

	ret = hap_write(h, h->ptn, HAP_PTN_DIRECT_PLAY, level);
	if (ret)
		return ret;

	/* as downstream for DIRECT_PLAY: open loop at the configured T_LRA */
	ret = hap_update(h, h->cfg, HAP_CFG_AUTORES_CFG, AUTORES_EN, 0);
	if (ret)
		return ret;

	return hap_write(h, h->cfg, HAP_CFG_SPMI_PLAY,
			 PAT_SRC_DIRECT_PLAY | PLAY_EN);
}

static int pm8350b_haptics_stop(struct pm8350b_haptics *h)
{
	return hap_write(h, h->cfg, HAP_CFG_SPMI_PLAY, PAT_SRC_DIRECT_PLAY);
}

static void pm8350b_haptics_work(struct work_struct *work)
{
	struct pm8350b_haptics *h = container_of(work, struct pm8350b_haptics,
						 work);
	u8 level = READ_ONCE(h->level);
	int ret;

	if (level) {
		ret = pm8350b_haptics_start(h, level);
		h->playing = !ret;
	} else if (h->playing) {
		ret = pm8350b_haptics_stop(h);
		h->playing = false;
	} else {
		return;
	}

	if (ret)
		dev_err(h->dev, "failed to %s vibration: %d\n",
			level ? "start" : "stop", ret);
}

static int pm8350b_haptics_play(struct input_dev *input, void *data,
				struct ff_effect *effect)
{
	struct pm8350b_haptics *h = input_get_drvdata(input);
	u32 mag;

	/* ff-memless turns FF_CONSTANT and FF_PERIODIC into rumble levels */
	mag = max(effect->u.rumble.strong_magnitude,
		  effect->u.rumble.weak_magnitude);
	WRITE_ONCE(h->level, mag ? max(1u, mag >> 8) : 0);
	schedule_work(&h->work);

	return 0;
}

static void pm8350b_haptics_close(struct input_dev *input)
{
	struct pm8350b_haptics *h = input_get_drvdata(input);

	cancel_work_sync(&h->work);
	if (h->playing)
		pm8350b_haptics_stop(h);
	h->playing = false;
}

static int pm8350b_haptics_hw_init(struct pm8350b_haptics *h, u32 t_lra_us,
				   u32 drv_wf)
{
	unsigned int clamp;
	u8 rev[2];
	u32 tlra;
	u8 val[2];
	int ret;

	ret = regmap_bulk_read(h->regmap, h->hbst + HAP_BOOST_REVISION1, rev, 2);
	if (ret)
		return ret;
	ret = regmap_read(h->regmap, h->hbst + ((rev[1] << 8 | rev[0]) == 0 ?
			  HAP_BOOST_V0P0_CLAMP : HAP_BOOST_V0P1_CLAMP), &clamp);
	if (ret)
		return ret;
	if ((clamp & CLAMP_5V) && h->vmax_mv > CLAMPED_VMAX_MV)
		h->vmax_mv = CLAMPED_VMAX_MV;

	ret = hap_write(h, h->cfg, HAP_CFG_VMAX, h->vmax_mv / VMAX_STEP_MV);
	if (ret)
		return ret;

	/* drive shape (sine/square), 2's complement sample format */
	ret = hap_update(h, h->cfg, HAP_CFG_DRV_WF_SEL,
			 DRV_WF_SEL_MASK | DRV_WF_FMT, drv_wf);
	if (ret)
		return ret;

	/* RC clock calibration in AUTO mode, T_LRA scaled as downstream */
	ret = hap_update(h, h->cfg, HAP_CFG_CAL_EN, CAL_RC_CLK_MASK,
			 CAL_RC_CLK_AUTO);
	if (ret)
		return ret;

	tlra = div_u64((u64)t_lra_us * 1024, 1000) / TLRA_STEP_US;
	val[0] = (tlra >> 8) & GENMASK(3, 0);
	val[1] = tlra & GENMASK(7, 0);
	ret = hap_write(h, h->cfg, HAP_CFG_TLRA_OL_HIGH, val[0]);
	if (!ret)
		ret = hap_write(h, h->cfg, HAP_CFG_TLRA_OL_HIGH + 1, val[1]);
	if (ret)
		return ret;

	ret = pm8350b_haptics_stop(h);
	if (ret)
		return ret;

	return hap_update(h, h->cfg, HAP_CFG_EN_CTL, HAPTICS_EN, HAPTICS_EN);
}

static int pm8350b_haptics_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pm8350b_haptics *h;
	u32 t_lra_us, drv_wf = 1;
	unsigned int rev;
	int ret;

	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->dev = dev;

	h->regmap = dev_get_regmap(dev->parent, NULL);
	if (!h->regmap)
		return -ENODEV;

	if (of_property_read_u32_index(np, "reg", 0, &h->cfg) ||
	    of_property_read_u32_index(np, "reg", 1, &h->ptn) ||
	    of_property_read_u32_index(np, "reg", 2, &h->hbst))
		return dev_err_probe(dev, -EINVAL, "need cfg, ptn and hboost reg\n");

	ret = of_property_read_u32(np, "qcom,vmax-mv", &h->vmax_mv);
	if (ret || !h->vmax_mv || h->vmax_mv > SAFE_VMAX_MV)
		return dev_err_probe(dev, -EINVAL,
				     "qcom,vmax-mv missing or above %u\n",
				     SAFE_VMAX_MV);

	ret = of_property_read_u32(np, "qcom,lra-period-us", &t_lra_us);
	if (ret || !t_lra_us || t_lra_us > TLRA_MAX_US)
		return dev_err_probe(dev, -EINVAL, "bad qcom,lra-period-us\n");

	of_property_read_u32(np, "qcom,drv-sig-shape", &drv_wf);
	if (drv_wf > 2)
		return dev_err_probe(dev, -EINVAL, "bad qcom,drv-sig-shape\n");

	ret = regmap_read(h->regmap, h->cfg + HAP_CFG_REVISION2, &rev);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read HAP_CFG\n");
	dev_info(dev, "HAP_CFG revision %#x, Vmax %u mV, T_LRA %u us\n",
		 rev, h->vmax_mv, t_lra_us);

	INIT_WORK(&h->work, pm8350b_haptics_work);

	ret = pm8350b_haptics_hw_init(h, t_lra_us, drv_wf);
	if (ret)
		return dev_err_probe(dev, ret, "hardware init failed\n");

	h->input = devm_input_allocate_device(dev);
	if (!h->input)
		return -ENOMEM;

	h->input->name = "pm8350b-haptics";
	h->input->close = pm8350b_haptics_close;
	input_set_drvdata(h->input, h);
	input_set_capability(h->input, EV_FF, FF_RUMBLE);

	ret = input_ff_create_memless(h->input, NULL, pm8350b_haptics_play);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, h);
	return input_register_device(h->input);
}

static void pm8350b_haptics_remove(struct platform_device *pdev)
{
	struct pm8350b_haptics *h = platform_get_drvdata(pdev);

	cancel_work_sync(&h->work);
	pm8350b_haptics_stop(h);
}

static int pm8350b_haptics_suspend(struct device *dev)
{
	struct pm8350b_haptics *h = dev_get_drvdata(dev);

	cancel_work_sync(&h->work);
	if (h->playing)
		pm8350b_haptics_stop(h);
	h->playing = false;
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pm8350b_haptics_pm, pm8350b_haptics_suspend,
				NULL);

static const struct of_device_id pm8350b_haptics_of_match[] = {
	{ .compatible = "qcom,pm8350b-haptics" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8350b_haptics_of_match);

static struct platform_driver pm8350b_haptics_driver = {
	.probe = pm8350b_haptics_probe,
	.remove = pm8350b_haptics_remove,
	.driver = {
		.name = "pm8350b-haptics",
		.of_match_table = pm8350b_haptics_of_match,
		.pm = pm_sleep_ptr(&pm8350b_haptics_pm),
	},
};
module_platform_driver(pm8350b_haptics_driver);

MODULE_DESCRIPTION("Qualcomm PM8350B haptics (DIRECT_PLAY only)");
MODULE_LICENSE("GPL");
