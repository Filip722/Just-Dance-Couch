"""Instrument the generated JD2014 title in the external build cache."""

import re


HOOKS = {
    "800f307c": "extern void jd2014_probe_lobby_ready(void); jd2014_probe_lobby_ready();",
    "800f1f24": "extern void jd2014_probe_lobby_update(void); jd2014_probe_lobby_update();",
    "801d3d64": (
        "extern int jd2014_probe_direct(void); "
        "if (jd2014_probe_direct()) { cpu.r[3] = 1; return; }"
    ),
    "80142474": "extern void jd2014_probe_map(void); jd2014_probe_map();",
    "801cc4e8": "extern void jd2014_probe_move_score(void); jd2014_probe_move_score();",
    "800f8424": "extern void jd2014_probe_results(void); jd2014_probe_results();",
}


def instrument(directory):
    pending = HOOKS.copy()
    for source in directory.glob("*.c"):
        original = source.read_text()
        modified = original
        for address, hook in tuple(pending.items()):
            pattern = rf"(?m)^void f_{address}\(void\) \{{ POLL(?:_COUNTED)?\(\d*\);\n"
            match = re.search(pattern, modified)
            if match:
                after = match.end()
                line = f"  {hook}\n"
                if not modified.startswith(line, after):
                    modified = modified[:after] + line + modified[after:]
                del pending[address]
        if modified != original:
            source.write_text(modified)
        if not pending:
            break
    if pending:
        raise ValueError(f"Missing JD2014 probe hook addresses {', '.join(pending)}")
