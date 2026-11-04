// SPDX-License-Identifier: GPL-2.0
/*
 * Essai : voter la bande passante du groupe QUP n°1 pour le compte de
 * TrustZone, qui lit le capteur d'empreintes sur le SE10 (spi@a88000).
 *
 * Linux ne pilote pas ce SE (il appartient à TrustZone), donc personne ne vote
 * le chemin « qup-core » du groupe : l'horloge du cœur QUP peut rester au
 * minimum et la lecture de l'image à 25 MHz échouer (CRC faux, 1028).
 *
 *   insmod qup1-vote.ko [core_mbps=200] [config_mbps=100]
 *
 * Le vote tient tant que le module est chargé.
 */
#include <linux/err.h>
#include <linux/interconnect.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static unsigned int core_mbps = 200;
module_param(core_mbps, uint, 0444);
MODULE_PARM_DESC(core_mbps, "vote qup-core, Mo/s");

static unsigned int config_mbps = 100;
module_param(config_mbps, uint, 0444);
MODULE_PARM_DESC(config_mbps, "vote qup-config, Mo/s");

static struct platform_device *pdev;
static struct icc_path *core, *config;

static int __init qup1_vote_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_node_by_path("/soc@0/geniqup@ac0000/spi@a88000");
	if (!np)
		return -ENODEV;

	pdev = platform_device_alloc("qup1-vote", PLATFORM_DEVID_NONE);
	if (!pdev) {
		of_node_put(np);
		return -ENOMEM;
	}
	pdev->dev.of_node = np;

	core = of_icc_get(&pdev->dev, "qup-core");
	if (IS_ERR(core)) {
		ret = PTR_ERR(core);
		goto err_put;
	}
	config = of_icc_get(&pdev->dev, "qup-config");
	if (IS_ERR(config)) {
		ret = PTR_ERR(config);
		goto err_core;
	}

	ret = icc_set_bw(core, MBps_to_icc(core_mbps), MBps_to_icc(core_mbps));
	if (!ret)
		ret = icc_set_bw(config, MBps_to_icc(config_mbps),
				 MBps_to_icc(config_mbps));
	if (ret)
		goto err_config;

	pr_info("qup1-vote: qup-core %u Mo/s, qup-config %u Mo/s\n",
		core_mbps, config_mbps);
	return 0;

err_config:
	icc_put(config);
err_core:
	icc_put(core);
err_put:
	of_node_put(np);
	pdev->dev.of_node = NULL;
	platform_device_put(pdev);
	pr_err("qup1-vote: %d\n", ret);
	return ret;
}

static void __exit qup1_vote_exit(void)
{
	struct device_node *np = pdev->dev.of_node;

	icc_put(config);
	icc_put(core);
	pdev->dev.of_node = NULL;
	platform_device_put(pdev);
	of_node_put(np);
}

module_init(qup1_vote_init);
module_exit(qup1_vote_exit);
MODULE_DESCRIPTION("Vote de bande passante QUP1 pour le SE10 de TrustZone (essai)");
MODULE_LICENSE("GPL");
