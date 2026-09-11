"""Сборка строк CONFIG_WRITE / CONFIG:* из словаря (без pygame)."""

from typing import Mapping


def _i(d: Mapping, key: str, default: int = 0) -> int:
    try:
        return int(d.get(key, default) or 0)
    except (TypeError, ValueError):
        return default


def build_chip_config(d: Mapping) -> str:
    t = _i(d, "chip_type")
    st = _i(d, "chip_sub")
    u = max(0, min(255, _i(d, "uses", 1)))
    p = list(d.get("params") or [0] * 16)
    while len(p) < 16:
        p.append(0)
    p = [int(x) for x in p[:16]]
    extra = []
    name = (d.get("reg_name") or "").strip()
    if t == 3 and st == 7 and name:
        extra.append("name=" + name.replace(",", " ").replace("\n", " ")[:24])
    parts = [f"type={t}", f"sub={st}", f"uses={u}"]
    parts += [f"p{i}={p[i]}" for i in range(16)]
    parts += extra
    return ",".join(parts)


def build_anomaly_config(d: Mapping) -> str:
    mask = _i(d, "anom_dmg_mask", 1)
    first_bit = next((i for i in range(8) if mask & (1 << i)), 0)
    rad_on = bool(d.get("anom_rad_on"))
    return (
        f"cat=0,sub={first_bit},dmg_mask={mask},"
        f"dmg={_i(d, 'anom_dmg', 10)},"
        f"dmg_max={_i(d, 'anom_dmg_max', 20)},"
        f"dmg_stp={_i(d, 'anom_dmg_step', 1)},"
        f"freq={_i(d, 'anom_freq', 60)},"
        f"rad_dmg={_i(d, 'anom_rad_dmg') if rad_on else 0},"
        f"rad_frq={_i(d, 'anom_rad_freq', 10) if rad_on else 0},"
        f"rech={_i(d, 'anom_recharge')},"
        f"tgt={_i(d, 'anom_target')},"
        f"erupt={_i(d, 'anom_erupt', 30)},"
        f"hits={_i(d, 'anom_hits', 3)},"
        f"rad={_i(d, 'anom_radius', 5)},"
        f"uwb_id={_i(d, 'anom_uwb_id')}"
    )


def build_shelter_config(d: Mapping) -> str:
    prot = list(d.get("sz_prot") or [0] * 8)
    while len(prot) < 8:
        prot.append(0)
    hp_freq = _i(d, "sz_hp_freq", 30)
    regen = _i(d, "sz_regen", 5)
    hp_tick = round(regen * hp_freq / 60) if hp_freq > 0 else regen
    prot_parts = ",".join(
        f"sz_p{i}={max(0, min(100, int(p)))}" for i, p in enumerate(prot[:8])
    )
    return (
        f"cat=1,sz_hp={hp_tick},sz_hp_frq={hp_freq},"
        f"sz_rad={_i(d, 'sz_rad', 2)},sz_rad_frq={_i(d, 'sz_rad_freq', 60)},"
        f"sz_emission={1 if d.get('sz_emission') else 0},"
        f"{prot_parts},rad={_i(d, 'sz_radius', 10)},"
        f"uwb_id={_i(d, 'sz_uwb_id')}"
    )


def build_config_for_device(device: str, d: Mapping) -> str:
    """device: CHIP / ANOMALY / SAFE_ZONE / PDA."""
    kind = (device or "").upper()
    if kind in ("CHIP", "CHIP_BOX"):
        return build_chip_config(d)
    if kind == "ANOMALY":
        return build_anomaly_config(d)
    if kind in ("SAFE_ZONE", "SHELTER", "УБЕЖИЩЕ"):
        return build_shelter_config(d)
    if kind == "PDA":
        return build_pda_config(d)
    raise ValueError(f"Неизвестное устройство: {device}")


def build_pda_config(d: Mapping) -> str:
    if _i(d, "pda_mode") == 0:
        return f"CONFIG:FUNC:flags={_i(d, 'pda_func_flags', 0xFF)}"
    bp = list(d.get("pda_base_prot") or [0] * 8)
    while len(bp) < 8:
        bp.append(0)
    prot_s = ",".join(f"r{i}={max(0, min(100, int(bp[i])))}" for i in range(8))
    return (
        f"CONFIG:PRESET:maxhp={_i(d, 'pda_maxhp', 1000)},"
        f"starthp={_i(d, 'pda_starthp', 1000)},"
        f"maxrad={_i(d, 'pda_maxrad', 1000)},"
        f"money={_i(d, 'pda_money', 1000)},"
        f"lvl={_i(d, 'pda_level', 2)},"
        f"xp={_i(d, 'pda_xp')},"
        f"{prot_s}"
    )


def build_terminal_cfg(d: Mapping) -> str:
    """USB-конфиг терминала после одноразовой заливки .ino. 0 = без лимита."""
    role = (d.get("role") or "").strip().upper()
    parts = []
    if role:
        parts.append(f"role={role}")
    parts.append(f"limit_purchase={max(0, _i(d, 'limit_purchase'))}")
    parts.append(f"limit_withdraw={max(0, _i(d, 'limit_withdraw'))}")
    parts.append(f"limit_deposit={max(0, _i(d, 'limit_deposit'))}")
    return "TERMINAL_CFG:" + ",".join(parts)
