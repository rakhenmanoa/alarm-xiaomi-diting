# Vibreur (PM8350B) : test hors de l'arbre du noyau

Même pilote que le patch noyau `0904`, à compiler seul contre l'arbre de
linux-diting déjà construit, pour tester avant de recompiler le paquet :

    make -C <arbre linux-7.2.10 construit> M=$PWD KBUILD_MODPOST_WARN=1 modules

Le nœud DT (`haptics@f000` sous `pmic@3`) s'ajoute au DTB avec `fdtput`,
voir le README principal.
