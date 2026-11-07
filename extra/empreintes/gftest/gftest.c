// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * gftest : module de test — tente d'activer les régulateurs du capteur
 * d'empreinte Goodix (vreg_l7c_3p0 / vreg_l11c_2p5) sans device tree
 * dédié, pour vérifier si l'alimentation est la cause du blocage de la
 * détection capteur côté TrustZone (gfdit.mbn).
 *
 * Compilation : make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 */
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/kernel.h>

static struct regulator *reg_l7c;
static struct regulator *reg_l11c;

static void try_one(const char *name, struct regulator **out)
{
	struct regulator *r = regulator_get(NULL, name);

	if (IS_ERR(r)) {
		pr_info("gftest: regulator_get(%s) = %ld\n", name, PTR_ERR(r));
		*out = NULL;
		return;
	}
	pr_info("gftest: regulator_get(%s) OK, is_enabled avant = %d\n",
		name, regulator_is_enabled(r));
	if (regulator_enable(r))
		pr_info("gftest: regulator_enable(%s) a échoué\n", name);
	else
		pr_info("gftest: regulator_enable(%s) OK, is_enabled apres = %d\n",
			name, regulator_is_enabled(r));
	*out = r;
}

static int __init gftest_init(void)
{
	try_one("vreg_l7c_3p0", &reg_l7c);
	try_one("vreg_l11c_2p5", &reg_l11c);
	return 0;
}

static void __exit gftest_exit(void)
{
	if (reg_l7c) {
		regulator_disable(reg_l7c);
		regulator_put(reg_l7c);
	}
	if (reg_l11c) {
		regulator_disable(reg_l11c);
		regulator_put(reg_l11c);
	}
}

module_init(gftest_init);
module_exit(gftest_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test d'activation des régulateurs du capteur Goodix (diting)");
