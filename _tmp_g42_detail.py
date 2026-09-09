# -*- coding: utf-8 -*-
import math
import pathlib
import re
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PATH = pathlib.Path(
    r"E:\For_rabota\Projects\Stalker\hardware\pda_v5\STALKER_PDA_V5\STALKER_PDA_V5\STALKER_PDA_V5.kicad_pcb"
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
            tracks.append(tuple(map(float, sm.group(1, 2, 3, 4))))
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


def seed_near(px, py, tracks, vias, rad=1.5):
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


def hit_pads(nodes, pads, tol=1.0):
    out = []
    seen = set()
    for ref, val, pname, x, y in pads:
        for nx, ny in nodes:
            if abs(x - nx) <= tol and abs(y - ny) <= tol:
                t = (ref, pname, val, round(x, 2), round(y, 2))
                if t not in seen:
                    seen.add(t)
                    out.append(t)
                break
    return sorted(out)


pads, tracks, vias = parse(PATH)

# Focus G42 at (17.78, 27.94) which near_cluster=True
for ref, val, pname, x, y in pads:
    if pname == "G42" and abs(x - 17.78) < 0.2 and abs(y - 27.94) < 0.2:
        nodes = seed_near(x, y, tracks, vias, rad=1.5)
        hits = hit_pads(nodes, pads)
        print(f"G42 detail ({x:.2f},{y:.2f}) nodes={len(nodes)}")
        for h in hits:
            print(f"  {h[0]}.{h[1]} ({h[2]}) @({h[3]},{h[4]})")
        print("  nodes in cluster box:")
        for nx, ny in nodes:
            if 50 <= nx <= 80 and 20 <= ny <= 45:
                print(f"    ({nx:.2f},{ny:.2f})")

print()
# G18 full net (R2.2 hit)
for ref, val, pname, x, y in pads:
    if pname == "G18" and "INTER" in val.upper():
        nodes = seed_near(x, y, tracks, vias, rad=1.5)
        hits = hit_pads(nodes, pads)
        print(f"G18 {ref} ({x:.2f},{y:.2f}) nodes={len(nodes)}")
        for h in hits:
            print(f"  {h[0]}.{h[1]} ({h[2]}) @({h[3]},{h[4]})")

print()
# Emitter of Q1 MMBT - should be GND
for ref, val, pname, x, y in pads:
    if val == "MMBT2222A" and pname == "E" and 50 < x < 70:
        nodes = seed_near(x, y, tracks, vias, rad=1.5)
        hits = hit_pads(nodes, pads)
        print(f"Q1.E ({x:.2f},{y:.2f}) nodes={len(nodes)}")
        for h in hits:
            print(f"  {h[0]}.{h[1]} ({h[2]})")

print()
# Collector
for ref, val, pname, x, y in pads:
    if val == "MMBT2222A" and pname == "C" and 50 < x < 70:
        nodes = seed_near(x, y, tracks, vias, rad=1.5)
        hits = hit_pads(nodes, pads)
        print(f"Q1.C ({x:.2f},{y:.2f}) nodes={len(nodes)}")
        for h in hits:
            print(f"  {h[0]}.{h[1]} ({h[2]})")

print()
# Trace path from R2.2 leftward - list track chain manually from (57.09,26.67)
print("Tracks near R2 / Q1 MMBT:")
for x1, y1, x2, y2 in tracks:
    pts = [(x1, y1), (x2, y2)]
    if any(50 <= x <= 75 and 18 <= y <= 40 for x, y in pts):
        if any(math.hypot(x - 58, y - 28) < 15 for x, y in pts):
            print(f"  ({x1:.2f},{y1:.2f})-({x2:.2f},{y2:.2f})")
