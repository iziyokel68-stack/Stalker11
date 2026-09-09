# -*- coding: utf-8 -*-
"""Targeted: which GPIO feeds SMD BU03 key cluster on STALKER_PDA_V5.kicad_pcb"""
import math
import pathlib
import re
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PATH = pathlib.Path(
    r"E:\For_rabota\Projects\Stalker\hardware\pda_v5\STALKER_PDA_V5\STALKER_PDA_V5\STALKER_PDA_V5.kicad_pcb"
)
POWER = pathlib.Path(
    r"E:\For_rabota\Projects\Stalker\hardware\pda_v5\STALKER_PDA_V5\STALKER_PDA_V5\STALKER_PDA_POWER_V5.kicad_pcb"
)


def extract_blocks(text, keyword):
    out = []
    needle = "(" + keyword
    i = 0
    while True:
        i = text.find(needle, i)
        if i < 0:
            break
        after = i + len(needle)
        if after < len(text) and text[after] not in " \n\t\r":
            i = after
            continue
        depth = 0
        for j in range(i, len(text)):
            if text[j] == "(":
                depth += 1
            elif text[j] == ")":
                depth -= 1
                if depth == 0:
                    out.append(text[i : j + 1])
                    i = j + 1
                    break
        else:
            break
    return out


def rotate(x, y, ang):
    a = math.radians(ang)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


def parse(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    pads = []
    for fp in extract_blocks(text, "footprint"):
        ref_m = re.search(r'\(property\s+"Reference"\s+"([^"]*)"', fp)
        val_m = re.search(r'\(property\s+"Value"\s+"([^"]*)"', fp)
        ref = ref_m.group(1) if ref_m else "?"
        val = val_m.group(1) if val_m else "?"
        m = re.search(r"\(at\s+([-\d.]+)\s+([-\d.]+)(?:\s+([-\d.]+))?", fp)
        if not m:
            continue
        fx, fy, fang = float(m.group(1)), float(m.group(2)), float(m.group(3) or 0)
        pos = 0
        while True:
            p0 = fp.find('(pad "', pos)
            if p0 < 0:
                break
            p1 = fp.find('"', p0 + 6)
            pname = fp[p0 + 6 : p1]
            a0 = fp.find("(at ", p0)
            nxt = fp.find('(pad "', p0 + 1)
            if a0 < 0 or (nxt > 0 and a0 > nxt):
                pos = p0 + 1
                continue
            am = re.match(r"\(at\s+([-\d.]+)\s+([-\d.]+)", fp[a0:])
            if not am:
                pos = p0 + 1
                continue
            lx, ly = float(am.group(1)), float(am.group(2))
            rx, ry = rotate(lx, ly, fang)
            pads.append((ref, val, pname, fx + rx, fy + ry))
            pos = p0 + 1
    tracks = []
    for s in extract_blocks(text, "segment"):
        sm = re.search(
            r"\(start\s+([-\d.]+)\s+([-\d.]+)\)\s*\(end\s+([-\d.]+)\s+([-\d.]+)\)",
            s,
        )
        if sm:
            tracks.append(
                tuple(map(float, sm.group(1, 2, 3, 4)))
            )
    vias = []
    for v in extract_blocks(text, "via"):
        am = re.search(r"\(at\s+([-\d.]+)\s+([-\d.]+)\)", v)
        if am:
            vias.append((float(am.group(1)), float(am.group(2))))
    return pads, tracks, vias


def flood(px, py, tracks, vias, tol=0.6):
    pts = [(px, py)]
    seen = set()
    nodes = []
    qi = 0
    while qi < len(pts):
        x, y = pts[qi]
        qi += 1
        key = (round(x, 2), round(y, 2))
        if key in seen:
            continue
        seen.add(key)
        nodes.append((x, y))
        for x1, y1, x2, y2 in tracks:
            if abs(x - x1) <= tol and abs(y - y1) <= tol:
                pts.append((x2, y2))
            if abs(x - x2) <= tol and abs(y - y2) <= tol:
                pts.append((x1, y1))
        for vx, vy in vias:
            if abs(x - vx) <= tol and abs(y - vy) <= tol:
                pts.append((vx, vy))
    return nodes


def seed_near(px, py, tracks, vias, rad=1.2):
    """Flood from pad by also seeding nearby track endpoints (pad body contact)."""
    seeds = [(px, py)]
    for x1, y1, x2, y2 in tracks:
        if math.hypot(px - x1, py - y1) <= rad:
            seeds.append((x1, y1))
        if math.hypot(px - x2, py - y2) <= rad:
            seeds.append((x2, y2))
    for vx, vy in vias:
        if math.hypot(px - vx, py - vy) <= rad:
            seeds.append((vx, vy))
    all_nodes = []
    seen = set()
    for sx, sy in seeds:
        for n in flood(sx, sy, tracks, vias):
            k = (round(n[0], 2), round(n[1], 2))
            if k not in seen:
                seen.add(k)
                all_nodes.append(n)
    return all_nodes


def hit_pads(nodes, pads, tol=0.8):
    out = []
    seen = set()
    for ref, val, pname, x, y in pads:
        for nx, ny in nodes:
            if abs(x - nx) <= tol and abs(y - ny) <= tol:
                t = (ref, pname, val)
                if t not in seen:
                    seen.add(t)
                    out.append(t)
                break
    return sorted(out)


def analyze_cluster(name, pads, tracks, vias):
    print("=" * 72)
    print(name)
    # SMD BU03 key cluster (user screenshot region ~55-75, 25-40)
    cluster_pads = [
        p
        for p in pads
        if 50 <= p[3] <= 80 and 20 <= p[4] <= 45
        and (
            p[2] in ("B", "C", "E", "G", "S", "D", "1", "2")
            or p[0] in ("Q1", "Q2", "R2", "R3", "R4", "C5", "C2")
            or "AO3401" in p[1]
            or "MMBT" in p[1]
        )
    ]
    print("\nSMD cluster pads (x=50..80, y=20..45):")
    for p in sorted(cluster_pads, key=lambda t: (t[0], t[2])):
        print(f"  {p[0]}.{p[2]:4} {p[1]:16} ({p[3]:.2f},{p[4]:.2f})")

    for label, pred in [
        ("MMBT2222A.B (BU03 NPN base)", lambda p: p[1] == "MMBT2222A" and p[2] == "B" and 50 < p[3] < 70),
        ("AO3401.G", lambda p: p[1] == "AO3401" and p[2] == "G"),
        ("AO3401.D", lambda p: p[1] == "AO3401" and p[2] == "D"),
        ("R2 220 near cluster", lambda p: p[0] == "R2" and p[1] == "220" and p[2] == "1" and 50 < p[3] < 70),
        ("R2 220 pad2", lambda p: p[0] == "R2" and p[1] == "220" and p[2] == "2" and 50 < p[3] < 70),
    ]:
        matches = [p for p in pads if pred(p)]
        for ref, val, pname, x, y in matches:
            nodes = seed_near(x, y, tracks, vias, rad=1.3)
            hits = hit_pads(nodes, pads)
            gpios = [h for h in hits if h[1] in ("G42", "G17", "G21", "G1", "G2", "G18") or h[1].startswith("G")]
            print(f"\n[{label}] {ref}.{pname} ({x:.2f},{y:.2f}) nodes={len(nodes)}")
            print("  GPIO-ish hits:")
            for h in gpios:
                print(f"    {h[0]}.{h[1]} ({h[2]})")
            print("  All key hits:")
            for h in hits:
                if h[0].startswith(("Q", "R", "C", "J", "U", "1")) or h[1] in (
                    "G42",
                    "G17",
                    "G21",
                    "3V3",
                    "TX1",
                    "RX1",
                    "B",
                    "C",
                    "E",
                    "G",
                    "S",
                    "D",
                ):
                    print(f"    {h[0]}.{h[1]} ({h[2]})")

    # Explicit: flood from every J_INTER G42/G17 with pad-body seed, see if reaches AO3401
    print("\n--- Does any G42 reach AO3401/MMBT cluster? ---")
    for ref, val, pname, x, y in pads:
        if pname not in ("G42", "G17"):
            continue
        if "INTER" not in val.upper() and ref not in ("J_INTER", "1"):
            continue
        nodes = seed_near(x, y, tracks, vias, rad=1.0)
        hits = hit_pads(nodes, pads, tol=1.0)
        interesting = [
            h
            for h in hits
            if "AO3401" in h[2]
            or "MMBT" in h[2]
            or h[0] in ("Q1", "Q2", "R2", "R3")
            and ("220" in h[2] or "10k" in h[2] or "AO" in h[2] or "MMBT" in h[2] or h[2] == "")
        ]
        # also check if any node near cluster
        near_cluster = any(50 <= nx <= 80 and 20 <= ny <= 45 for nx, ny in nodes)
        if interesting or near_cluster:
            print(f"  {ref}.{pname} ({x:.2f},{y:.2f}) near_cluster={near_cluster}")
            for h in hits:
                if (
                    "AO3401" in h[2]
                    or "MMBT" in h[2]
                    or h[1] in ("B", "C", "E", "G", "S", "D")
                    or (h[0] in ("R2", "R3", "R4") and 50 < 100)
                ):
                    # filter R2/R3 only if coords in cluster via pad lookup
                    print(f"    -> {h[0]}.{h[1]} ({h[2]})")


def analyze_power(name, pads, tracks, vias):
    print("=" * 72)
    print(name, "POWER FOCUS")
    for pname in ("G42", "G17", "G21"):
        for ref, val, pad, x, y in pads:
            if pad != pname:
                continue
            nodes = seed_near(x, y, tracks, vias, rad=1.2)
            hits = hit_pads(nodes, pads, tol=1.0)
            print(f"\n{ref}.{pad} ({x:.2f},{y:.2f}) nodes={len(nodes)}")
            for h in hits:
                print(f"  -> {h[0]}.{h[1]} ({h[2]})")

    # PMOS drain connectivity to BU03
    print("\n--- PMOS + BU03 VCC ---")
    for ref, val, pad, x, y in pads:
        if val == "PMOS" or (ref == "Q2" and val == "PMOS"):
            nodes = seed_near(x, y, tracks, vias, rad=1.2)
            hits = hit_pads(nodes, pads, tol=1.0)
            print(f"Q2.{pad} ({x:.2f},{y:.2f})")
            for h in hits:
                print(f"  -> {h[0]}.{h[1]} ({h[2]})")
        if "BU03" in val and pad == "3V3":
            nodes = seed_near(x, y, tracks, vias, rad=2.0)
            hits = hit_pads(nodes, pads, tol=1.5)
            print(f"BU03.3V3 ({x:.2f},{y:.2f}) nodes={len(nodes)}")
            for h in hits:
                print(f"  -> {h[0]}.{h[1]} ({h[2]})")
            # nearest
            dists = []
            for x1, y1, x2, y2 in tracks:
                d = min(math.hypot(x - x1, y - y1), math.hypot(x - x2, y - y2))
                dists.append(d)
            if dists:
                print(f"  nearest segment distance: {min(dists):.2f} mm")


pads, tracks, vias = parse(PATH)
analyze_cluster("STALKER_PDA_V5.kicad_pcb", pads, tracks, vias)

pp, pt, pv = parse(POWER)
analyze_power("STALKER_PDA_POWER_V5.kicad_pcb", pp, pt, pv)
