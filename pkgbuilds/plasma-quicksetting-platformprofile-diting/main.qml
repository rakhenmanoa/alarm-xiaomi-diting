// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tuile des réglages rapides de Plasma Mobile : profil d'énergie.
// Un appui passe au profil suivant (économie -> équilibré -> performance).
// Passe par power-profiles-daemon (powerprofilesctl), donc par le pilote
// diting-platform-profile (/sys/class/platform-profile/diting).

import QtQuick

import org.kde.plasma.private.mobileshell.quicksettingsplugin as QS
import org.kde.plasma.plasma5support as P5Support

QS.QuickSetting {
    id: root

    // Valeurs de power-profiles-daemon, dans l'ordre du cycle.
    readonly property var order: ["power-saver", "balanced", "performance"]
    readonly property var labels: ({
        "power-saver": i18nc("@info:status", "Power saver"),
        "balanced": i18nc("@info:status", "Balanced"),
        "performance": i18nc("@info:status", "Performance")
    })
    // mêmes icônes que le widget « Énergie et batterie » de KDE
    readonly property var icons: ({
        "power-saver": "battery-profile-powersave-symbolic",
        "balanced": "speedometer",
        "performance": "battery-profile-performance-symbolic"
    })

    property string current: ""
    property var choices: ["power-saver", "balanced", "performance"]

    text: i18nc("@action:button", "Power profile")
    status: current === "" ? "" : (labels[current] ?? current)
    icon: icons[current] ?? "speedometer"
    // allumée dès qu'on s'écarte du profil par défaut
    enabled: current !== "" && current !== "balanced"
    // masquée si power-profiles-daemon est absent
    available: true

    function toggle(): void {
        if (current === "" || choices.length === 0) {
            return;
        }
        const i = choices.indexOf(current);
        const next = choices[(i + 1) % choices.length];
        // affichage immédiat, confirmé par la relecture qui suit
        current = next;
        shell.run("powerprofilesctl set " + next);
    }

    function refresh(): void {
        shell.run("powerprofilesctl get");
    }

    function parseChoices(out: string): void {
        // lignes « * balanced: » / « performance: » de « powerprofilesctl list »
        const found = [];
        for (const line of out.split("\n")) {
            const m = line.match(/^[\s*]*(power-saver|balanced|performance):/);
            if (m) {
                found.push(m[1]);
            }
        }
        if (found.length > 0) {
            choices = order.filter(p => found.includes(p));
        }
    }

    P5Support.DataSource {
        id: shell
        engine: "executable"
        connectedSources: []

        onNewData: (source, data) => {
            disconnectSource(source);
            const out = (data["stdout"] ?? "").trim();
            const ok = data["exit code"] === 0;
            if (source === "powerprofilesctl get") {
                root.available = ok && out !== "";
                if (ok) {
                    root.current = out;
                }
            } else if (source === "powerprofilesctl list") {
                if (ok) {
                    root.parseChoices(out);
                }
            } else if (source.startsWith("powerprofilesctl set")) {
                // relit le profil réellement appliqué (refus polkit, etc.)
                root.refresh();
            }
        }

        function run(cmd: string): void {
            connectSource(cmd);
        }
    }

    // Suit aussi les changements faits ailleurs (powerprofilesctl, autre appli).
    Timer {
        interval: 10000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.refresh()
    }

    Component.onCompleted: shell.run("powerprofilesctl list")
}
