#!/usr/bin/env python3
"""Génère l'overlay du nœud ICP à partir du device tree en cours.

Usage : faire-overlay.py ancien.dtb > icp.dts
Les phandles sont lus dans l'ancien .dtb (il n'a pas de __symbols__).
"""
import re, subprocess, sys

dts = subprocess.run(["dtc", "-I", "dtb", "-O", "dts", sys.argv[1]],
                     capture_output=True, text=True, check=True).stdout

phandles, stack = {}, []
for line in dts.splitlines():
    s = line.strip()
    m = re.match(r"^(?:[\w-]+:\s*)?([\w@,.+-]+|/)\s*\{$", s)
    if m:
        stack.append(m.group(1))
        continue
    if s == "};":
        stack.pop()
        continue
    m = re.match(r"^phandle = <(0x[0-9a-f]+)>;", s)
    if m and stack:
        path = "/" + "/".join(x for x in stack if x != "/")
        phandles[path] = int(m.group(1), 16)

def ph(suffix):
    found = [p for p in phandles if p.endswith(suffix)]
    if len(found) != 1:
        sys.exit(f"phandle introuvable ou ambigu pour {suffix}: {found}")
    return f"0x{phandles[found[0]]:x}"

camcc = ph("/clock-controller@ade0000")
gcc = ph("/clock-controller@100000")
cfg = ph("/interconnect@1500000")
mmss = ph("/interconnect@1740000")
gem = ph("/interconnect@19100000")
mc = ph("/interconnect-1")
smmu = ph("/iommu@15000000")
mem = ph("/reserved-memory/memory@9f500000")
T = 7  # QCOM_ICC_TAG_ALWAYS

def cl(h, ids):
    return ",\n\t\t\t\t ".join(f"<{h} {i}>" for i in ids)

clocks = [(camcc, i) for i in (47, 48, 112)] + [(gcc, 16), (gcc, 17)] + \
    [(camcc, i) for i in (12, 11, 14, 4, 0, 3, 1, 13, 68, 71, 73, 69, 72, 19)]
clk = ",\n\t\t\t\t ".join(f"<{h} {i}>" for h, i in clocks)

print(f"""/dts-v1/;
/plugin/;

/ {{
	fragment@0 {{
		target-path = "/soc@0/isp@acb7000";
		__overlay__ {{
			#address-cells = <2>;
			#size-cells = <2>;
			ranges;

			icp@ac01000 {{
				compatible = "qcom,sm8475-camss-icp";
				reg = <0 0x0ac01000 0 0x400>,
				      <0 0x0ac01800 0 0x400>,
				      <0 0x0ac04000 0 0x1000>;
				reg-names = "csr", "cirq", "wd";
				interrupts = <0 463 1>;
				clocks = {clk};
				clock-names = "ahb", "core", "debug_xo",
					      "gcc_hf_axi", "gcc_sf_axi",
					      "cpas_ahb", "core_ahb", "cpas_fast_ahb",
					      "camnoc_axi",
					      "bps_ahb", "bps_fast_ahb", "bps", "cpas_bps",
					      "ipe_ahb", "ipe_nps_fast_ahb", "ipe_pps_fast_ahb",
					      "ipe_nps", "ipe_pps", "cpas_ipe";
				assigned-clocks = <{camcc} 2>, <{camcc} 70>, <{camcc} 49>;
				assigned-clock-rates = <480000000>, <480000000>, <480000000>;
				power-domains = <{camcc} 8>, <{camcc} 0>, <{camcc} 1>;
				power-domain-names = "top", "bps", "ipe";
				iommus = <{smmu} 0x2000 0x420>,
					 <{smmu} 0x2040 0x420>,
					 <{smmu} 0x2100 0x420>,
					 <{smmu} 0x2080 0x400>;
				dma-coherent;
				interconnects = <{gem} 2 {T} {cfg} 5 {T}>,
						<{mmss} 0 {T} {mc} 1 {T}>,
						<{mmss} 2 {T} {mc} 1 {T}>,
						<{mmss} 1 {T} {mc} 1 {T}>;
				interconnect-names = "ahb", "hf_0", "sf_0", "sf_icp";
				memory-region = <{mem}>;
				firmware-name = "qcom/sm8475/xiaomi/diting/CAMERA_ICP";
				status = "okay";
			}};
		}};
	}};
}};""")
