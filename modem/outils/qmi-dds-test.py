#!/usr/bin/env python3
# Test data QMI sur diting : tout dans un seul processus, car en QRTR
# le client WDS disparaît à la fin de chaque commande qmicli.
#
#   sudo python3 qmi-dds-test.py lire
#   sudo python3 qmi-dds-test.py test primary|secondary|default
#
# "lire" ne modifie rien. "test" lie un client WDS temporaire à
# l'abonnement choisi (pour ce client seulement, rien de persistant),
# tente une session IPv4 sur l'APN "internet", puis la referme.

import sys
import gi
gi.require_version("Qmi", "1.0")
gi.require_version("Qrtr", "1.0")
from gi.repository import GLib, Qmi, Qrtr

NODE = 0          # qrtr://0
MUX_ID = 5        # canal libre, à l'écart de ceux de ModemManager
APN = "internet"

loop = GLib.MainLoop()
_res = {}


def _cb(_obj, res, *_):
    _res["r"] = res
    loop.quit()


def wait():
    loop.run()
    return _res.pop("r")


def subs_name(v):
    return {0: "default", 1: "primary", 2: "secondary", 3: "tertiary", 255: "any"}.get(int(v), str(v))


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("lire", "test"):
        print(__doc__ or "usage: lire | test primary|secondary|default")
        sys.exit(1)
    mode = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else None
    subs = {"default": Qmi.SubscriptionType.DEFAULT,
            "primary": Qmi.SubscriptionType.PRIMARY,
            "secondary": Qmi.SubscriptionType.SECONDARY}
    if mode == "test" and target not in subs:
        print("usage: test primary|secondary|default")
        sys.exit(1)

    Qrtr.Bus.new(1000, None, _cb)
    bus = Qrtr.Bus.new_finish(wait())
    bus.wait_for_node(NODE, 5000, None, _cb)
    node = bus.wait_for_node_finish(wait())
    Qmi.Device.new_from_node(node, None, _cb)
    dev = Qmi.Device.new_from_node_finish(wait())
    dev.open(Qmi.DeviceOpenFlags.NONE, 15, None, _cb)
    dev.open_finish(wait())

    dev.allocate_client(Qmi.Service.WDS, Qmi.CID_NONE, 10, None, _cb)
    wds = dev.allocate_client_finish(wait())
    print(f"Client WDS alloué (CID {wds.get_cid()})")

    try:
        wds.get_bind_subscription(None, 10, None, _cb)
        out = wds.get_bind_subscription_finish(wait())
        out.get_result()
        print("Abonnement par défaut de ce client :", subs_name(out.get_subscription_id()))
    except GLib.Error as e:
        print("Get Bind Subscription refusé :", e.message)

    if mode == "test":
        inp = Qmi.MessageWdsBindMuxDataPortInput.new()
        inp.set_endpoint_info(Qmi.DataEndpointType.EMBEDDED, 1)
        inp.set_mux_id(MUX_ID)
        wds.bind_mux_data_port(inp, 10, None, _cb)
        try:
            wds.bind_mux_data_port_finish(wait()).get_result()
            print(f"Liaison au canal data (mux {MUX_ID}) : OK")
        except GLib.Error as e:
            print("Liaison au canal data refusée :", e.message)

        inp = Qmi.MessageWdsBindSubscriptionInput.new()
        inp.set_subscription_id(subs[target])
        wds.bind_subscription(inp, 10, None, _cb)
        try:
            wds.bind_subscription_finish(wait()).get_result()
            print("Client lié à l'abonnement", target)
        except GLib.Error as e:
            print("Bind Subscription refusé :", e.message)

        inp = Qmi.MessageWdsStartNetworkInput.new()
        inp.set_apn(APN)
        inp.set_ip_family_preference(Qmi.WdsIpFamily.IPV4)
        wds.start_network(inp, 60, None, _cb)
        out = wds.start_network_finish(wait())
        try:
            out.get_result()
            handle = out.get_packet_data_handle()
            print(f"SESSION DATA OUVERTE (handle {handle})")
            stop = Qmi.MessageWdsStopNetworkInput.new()
            stop.set_packet_data_handle(handle)
            wds.stop_network(stop, 30, None, _cb)
            wds.stop_network_finish(wait())
            print("Session refermée.")
        except GLib.Error as e:
            print("Start Network refusé :", e.message)
            try:
                t, r = out.get_verbose_call_end_reason()
                print(f"  raison détaillée : type {int(t)}, code {int(r)}")
            except GLib.Error:
                pass

    dev.release_client(wds, Qmi.DeviceReleaseClientFlags.RELEASE_CID, 10, None, _cb)
    dev.release_client_finish(wait())
    dev.close_async(10, None, _cb)
    dev.close_finish(wait())


if __name__ == "__main__":
    main()
