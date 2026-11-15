// SPDX-License-Identifier: GPL-2.0
/*
 * Profils de consommation (platform profile) du Xiaomi 12T Pro (diting).
 *
 * Expose /sys/class/platform-profile/diting/ avec low-power, balanced et
 * performance. Chaque profil règle, par QoS (donc sans écraser les réglages
 * sysfs ni le gouverneur) :
 *   - le min/max des 3 groupes de cœurs (policy0, policy4, policy7) ;
 *   - le min/max du GPU Adreno (devfreq 3d00000.gpu).
 * power-profiles-daemon et Plasma lisent cette interface pour afficher le
 * curseur de profil.
 */
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/pm_qos.h>

#define GPU_DEVICE_NAME	"3d00000.gpu"
#define UNLIMITED	FREQ_QOS_MAX_DEFAULT_VALUE

/* Cœurs de tête de chaque groupe : A510 (0-3), A710 (4-6), X2 (7). */
static const int cluster_cpu[] = { 0, 4, 7 };
#define NCLUSTER ARRAY_SIZE(cluster_cpu)

struct profile_cfg {
	enum platform_profile_option opt;
	s32 cpu_min[NCLUSTER];	/* kHz */
	s32 cpu_max[NCLUSTER];	/* kHz */
	s32 gpu_min;		/* kHz, 0 = pas de minimum */
	s32 gpu_max;		/* kHz */
};

/*
 * Valeurs de départ, à ajuster après mesure (voir README).
 * balanced = ce que faisait diting-perf.service.
 * Les max de low-power et le GPU sont à régler d'après
 * /sys/class/devfreq/3d00000.gpu/available_frequencies.
 */
static const struct profile_cfg profiles[] = {
	{
		.opt = PLATFORM_PROFILE_LOW_POWER,
		.cpu_min = { 300000, 633600, 787200 },
		.cpu_max = { 1440000, 1324800, 1401600 },
		.gpu_min = 0,
		.gpu_max = UNLIMITED,
	},
	{
		.opt = PLATFORM_PROFILE_BALANCED,
		.cpu_min = { 1056000, 1113600, 787200 },
		.cpu_max = { 2016000, 2745600, 3187200 },
		.gpu_min = 439000,
		.gpu_max = UNLIMITED,
	},
	{
		.opt = PLATFORM_PROFILE_PERFORMANCE,
		.cpu_min = { 1440000, 1555200, 1536000 },
		.cpu_max = { 2016000, 2745600, 3187200 },
		.gpu_min = 439000,
		.gpu_max = UNLIMITED,
	},
};

struct diting_pp {
	struct freq_qos_request cpu_min_req[NCLUSTER];
	struct freq_qos_request cpu_max_req[NCLUSTER];
	struct dev_pm_qos_request gpu_min_req;
	struct dev_pm_qos_request gpu_max_req;
	struct device *gpu_dev;
	enum platform_profile_option current_opt;
};

static struct platform_device *pdev;
static struct diting_pp *pp;

static const struct profile_cfg *find_cfg(enum platform_profile_option opt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(profiles); i++)
		if (profiles[i].opt == opt)
			return &profiles[i];
	return NULL;
}

static int apply(struct diting_pp *p, const struct profile_cfg *c)
{
	int i, ret;

	/* On relève le max d'abord, puis le min, pour éviter min > max. */
	for (i = 0; i < NCLUSTER; i++) {
		ret = freq_qos_update_request(&p->cpu_max_req[i], c->cpu_max[i]);
		if (ret < 0)
			return ret;
		ret = freq_qos_update_request(&p->cpu_min_req[i], c->cpu_min[i]);
		if (ret < 0)
			return ret;
	}
	if (p->gpu_dev) {
		ret = dev_pm_qos_update_request(&p->gpu_max_req, c->gpu_max);
		if (ret < 0)
			return ret;
		ret = dev_pm_qos_update_request(&p->gpu_min_req, c->gpu_min);
		if (ret < 0)
			return ret;
	}
	p->current_opt = c->opt;
	return 0;
}

static int dpp_probe(void *drvdata, unsigned long *choices)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(profiles); i++)
		set_bit(profiles[i].opt, choices);
	return 0;
}

static int dpp_get(struct device *dev, enum platform_profile_option *opt)
{
	struct diting_pp *p = dev_get_drvdata(dev);

	*opt = p->current_opt;
	return 0;
}

static int dpp_set(struct device *dev, enum platform_profile_option opt)
{
	const struct profile_cfg *c = find_cfg(opt);

	if (!c)
		return -EOPNOTSUPP;
	return apply(dev_get_drvdata(dev), c);
}

static const struct platform_profile_ops dpp_ops = {
	.probe = dpp_probe,
	.profile_get = dpp_get,
	.profile_set = dpp_set,
};

static void cleanup(struct diting_pp *p, int ncpu)
{
	int i;

	if (p->gpu_dev) {
		if (dev_pm_qos_request_active(&p->gpu_min_req))
			dev_pm_qos_remove_request(&p->gpu_min_req);
		if (dev_pm_qos_request_active(&p->gpu_max_req))
			dev_pm_qos_remove_request(&p->gpu_max_req);
		put_device(p->gpu_dev);
	}
	for (i = 0; i < ncpu; i++) {
		freq_qos_remove_request(&p->cpu_min_req[i]);
		freq_qos_remove_request(&p->cpu_max_req[i]);
	}
}

static int __init dpp_init(void)
{
	const struct profile_cfg *c = find_cfg(PLATFORM_PROFILE_BALANCED);
	struct device *pdev_dev, *ppdev;
	int i, ret, ncpu = 0;

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	for (i = 0; i < NCLUSTER; i++) {
		struct cpufreq_policy *pol = cpufreq_cpu_get(cluster_cpu[i]);

		if (!pol) {
			ret = -ENODEV;
			goto err;
		}
		ret = freq_qos_add_request(&pol->constraints, &pp->cpu_min_req[i],
					   FREQ_QOS_MIN, 0);
		if (ret >= 0) {
			ret = freq_qos_add_request(&pol->constraints,
						   &pp->cpu_max_req[i],
						   FREQ_QOS_MAX, UNLIMITED);
			if (ret < 0)
				freq_qos_remove_request(&pp->cpu_min_req[i]);
		}
		cpufreq_cpu_put(pol);
		if (ret < 0)
			goto err;
		ncpu++;
	}

	/* Le GPU est facultatif : sans lui, seuls les cœurs sont réglés. */
	pp->gpu_dev = bus_find_device_by_name(&platform_bus_type, NULL,
					      GPU_DEVICE_NAME);
	if (pp->gpu_dev) {
		ret = dev_pm_qos_add_request(pp->gpu_dev, &pp->gpu_min_req,
					     DEV_PM_QOS_MIN_FREQUENCY, 0);
		if (ret >= 0)
			ret = dev_pm_qos_add_request(pp->gpu_dev, &pp->gpu_max_req,
						     DEV_PM_QOS_MAX_FREQUENCY,
						     UNLIMITED);
		if (ret < 0)
			goto err;
	} else {
		pr_warn("diting-platform-profile: GPU %s introuvable\n",
			GPU_DEVICE_NAME);
	}

	pdev = platform_device_register_simple("diting-platform-profile", -1,
					       NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto err;
	}
	pdev_dev = &pdev->dev;
	dev_set_drvdata(pdev_dev, pp);

	ret = apply(pp, c);
	if (ret < 0)
		goto err_dev;

	ppdev = devm_platform_profile_register(pdev_dev, "diting", pp, &dpp_ops);
	if (IS_ERR(ppdev)) {
		ret = PTR_ERR(ppdev);
		goto err_dev;
	}
	return 0;

err_dev:
	platform_device_unregister(pdev);
err:
	cleanup(pp, ncpu);
	kfree(pp);
	return ret;
}

static void __exit dpp_exit(void)
{
	platform_device_unregister(pdev);
	cleanup(pp, NCLUSTER);
	kfree(pp);
}

module_init(dpp_init);
module_exit(dpp_exit);

MODULE_DESCRIPTION("Platform profiles CPU/GPU for the Xiaomi 12T Pro (diting)");
MODULE_LICENSE("GPL");
