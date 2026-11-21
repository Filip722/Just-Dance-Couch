"""Apply title-specific hooks to generated code in the external build cache."""

import re


HOOKS = {
    "8004e3c8": "extern void jd2014_probe_system_splash(void); jd2014_probe_system_splash();",
    "801ace48": "extern void jd2014_probe_logo(void); jd2014_probe_logo();",
    "80141100": "extern void jd2014_probe_boot_wait(void); jd2014_probe_boot_wait();",
    "8014e428": "extern void jd2014_probe_gameplay(void); jd2014_probe_gameplay();",
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


JD2015_HOOKS = {
    "8007b294": "extern void jd2015_probe_splash(void); jd2015_probe_splash();",
    "80183ce0": "extern void jd2015_probe_boot_wait(void); jd2015_probe_boot_wait();",
    "801fb314": "extern void jd2015_probe_logo(void); jd2015_probe_logo();",
    "8023af10": (
        "extern int jd2015_probe_direct(void); "
        "if (jd2015_probe_direct()) { cpu.r[3] = 1; return; }"
    ),
    "80189828": "extern void jd2015_probe_map(void); jd2015_probe_map();",
    "80261ce0": (
        "extern int jd2015_probe_controller_prompt(void); "
        "if (jd2015_probe_controller_prompt()) { cpu.r[3] = 1; return; }"
    ),
    "80125af0": "extern void jd2014_probe_lobby_ready(void); jd2014_probe_lobby_ready();",
    "80124238": "extern void jd2015_probe_lobby_update(void); jd2015_probe_lobby_update();",
    "801e2fe0": "extern void jd2015_probe_lobby_phase(void); jd2015_probe_lobby_phase();",
    # Different disc routines enter different Classic gameplay states.
    "8018ff1c": "extern void jd2014_probe_gameplay(void); jd2014_probe_gameplay();",
    "80194f48": "extern void jd2014_probe_gameplay(void); jd2014_probe_gameplay();",
    "8022fdec": "extern void jd2014_probe_move_score(void); jd2014_probe_move_score();",
    "801346fc": "extern void jd2014_probe_results(void); jd2014_probe_results();",
}


def instrument(directory, disc="SJOE41"):
    pending = (JD2015_HOOKS if disc == "SE3E41" else HOOKS).copy()
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
        raise ValueError(f"Missing {disc} probe hook addresses {', '.join(pending)}")


JD3_SUSPEND = " if (1) { cpu.lr = 0x8004f960u; f_8032ed10(); }"
JD3_STREAM_WAIT = """{
  // The DVD callback clears file+172 before resuming the waiting thread.
  // Keep interrupts disabled from the pending check through OS suspend.
  cpu.lr = 0x8004f960u;
  f_8032acf0();
  unsigned interrupts_enabled = cpu.r[3];
  if (rd32(cpu.r[27] + 172)) {
    cpu.r[3] = cpu.r[28];
    f_8032ed10();
  }
  cpu.r[3] = interrupts_enabled;
  f_8032ad30();
}"""


def fix_jd3_stream_wait(directory):
    for source in directory.glob("*.c"):
        original = source.read_text()
        if JD3_STREAM_WAIT in original:
            return
        if JD3_SUSPEND in original:
            if "void f_8004f874(void)" not in original or original.count(JD3_SUSPEND) != 1:
                raise ValueError("Unexpected JD3 streaming wait")
            source.write_text(original.replace(JD3_SUSPEND, JD3_STREAM_WAIT))
            return
    raise ValueError("Missing JD3 streaming wait at 8004f95c")
