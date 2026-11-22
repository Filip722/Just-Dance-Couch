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


# Hook addresses match the verified revisions. The 2016 sites match the
# corresponding 2017 functions, which are named in its disc symbol map.
UBIART_ADDRESSES = {
    "SJNE41": "80550a14 804a64cc 8057a830 804470dc 80445570 804fbf9c 804ac884 80564a90 80455f48 80630724 8049dcec 8051a144",
    "SZ7E41": "80551db4 804a7494 8057bfec 80447c24 804460a8 804fcf4c 804ad854 80566220 80456af8 80632c7c 8049ecac 8051b59c",
    "SE8E41": "80550d08 804a63d8 8057b620 80448444 804468c8 804fbdb8 804ac7cc 805651cc 804572e4 80630cdc 8049dfdc 8051a364",
    "S5EE41": "805414d8 8049b300 8056ba78 8043e2a8 8043c9c8 804ed394 804a16f4 805557b8 8044cbc0 806143a4 80492f0c 8050a23c",
}
UBIART_HOOK_KEYS = "8023af10 80189828 80261ce0 80125af0 80124238 801e2fe0 80194f48 8022fdec 801346fc 8007b294 80183ce0 801fb314"

# JD_GameManager::displayHowTo, called by the lobby entry state.
UBIART_DISPLAY_HOWTO = {
    "SJNE41": "80547488",
    "SZ7E41": "80548844",
    "SE8E41": "80547760",
    "S5EE41": "80537268",
}


# JD1 uses the original game database, menu actions, and Bink playback.
JD1_HOOKS = {
    "8000c6dc": "extern int jd1_probe_direct(void); if (jd1_probe_direct()) return;",
    "8005eaf0": "extern void jd1_probe_video_open(void); jd1_probe_video_open();",
    "802b2e48": "extern void jd1_probe_video_frame(void); jd1_probe_video_frame();",
    "c00d2ac0": "extern int jd1_probe_skip_song(void); if (jd1_probe_skip_song()) return;",
    "c012c954": (
        "extern int jd1_probe_menu_choice(uint32_t); if (jd1_probe_menu_choice(m0_code)) return;"
    ),
}
JD1_SITES = {
    "8000bd58": (
        " if (1) { cpu.lr = 0x8000bff4u; f_802b12f8(); }",
        " extern int jd1_probe_direct(void);\n"
        " if (!jd1_probe_direct()) { cpu.lr = 0x8000bff4u; f_802b12f8(); }",
    ),
    "80077160": (
        "L_80077880:;",
        "L_80077880:;\n"
        " extern void jd1_probe_input(uint32_t, uint32_t, uint32_t, uint32_t);\n"
        " jd1_probe_input(rd32s(cpu.r[1] + 244), cpu.r[25], cpu.r[22], cpu.r[29]);",
    ),
    "802cdc08": (
        " if (1) { cpu.lr = 0x802cdc3cu; f_802cdc50(); }",
        " if (1) { cpu.lr = 0x802cdc3cu; f_802cdc50(); }\n"
        " extern void jd1_probe_flash_value(uint32_t); jd1_probe_flash_value(cpu.r[31]);",
    ),
}

# Priv_Readcpyf32 in the two revisions that store packed vertex words through
# float registers. The host's flush-to-zero mode would erase some UV values.
LYN_VERTEX_COPY_SITES = {"SD2Y41": ("80036a18", 0), "SJ9E41": ("80029430", 1)}


# SCR_u32_Run, WOR_LoadGroupMask, catalog decompression and KPAD return sites.
JD2_ADDRESSES = {
    "SD2Y41": ("800fa45c", "800900f4", "80034ee8", "8003517c", "80022ce4", "803e7510", "803e7d18"),
    "SJ9E41": ("800da50c", "80072bbc", "80027b24", "80027e00", "80015fc4", "803a4f60", "803a5768"),
}


# SCR track runner, LZO exit, KPAD return, coach open, and first decoded frame.
LYN_CLASSIC_ADDRESSES = {
    "SJHE41": ("800ee7cc", "800237a0", "80023b24", "803a28e0", "803a30cc", "802da93c", "803b0030"),
    "SJDE41": ("800fdf40", "8002aadc", "8002ae60", "803d0450", "803d0c3c", "80306074", "803ddd90"),
    "SJXE41": ("8013db14", "800624d8", "8006285c", "80425cc0", "804264ac", None, None),
}


def instrument(directory, disc="SJOE41"):
    sites = JD1_SITES.copy() if disc == "SDNE41" else {}
    if disc in JD2_ADDRESSES:
        track, world, decompress, resume, decode, input_read, input_return = JD2_ADDRESSES[disc]
        pending = {
            track: "extern void jd2_probe_track(void); jd2_probe_track();",
            world: "extern void jd2_probe_map(void); jd2_probe_map();",
        }
        decode_catalog = f" if (1) {{ cpu.lr = 0x{resume}u; f_{decode}(); }}"
        sites[decompress] = (
            decode_catalog,
            "{ uint32_t output = cpu.r[5];\n"
            + decode_catalog
            + "\n  extern void jd2_probe_catalog(uint32_t, uint32_t);\n"
            "  jd2_probe_catalog(output, rd32s(cpu.r[1] + 8)); }",
        )
        sites[input_read] = (
            f"L_{input_return}:;",
            f"L_{input_return}:;\n"
            " extern void jd2_probe_input(uint32_t, uint32_t, uint32_t, uint32_t);\n"
            " jd2_probe_input(rd32s(cpu.r[1] + 580), cpu.r[25], cpu.r[26], cpu.r[29]);",
        )
        address, register = LYN_VERTEX_COPY_SITES[disc]
        sites[address] = (
            f"wr32((cpu.r[26] + cpu.r[31]), double_to_single(cpu.f[{register}]));",
            "wr32((cpu.r[26] + cpu.r[31]), cpu.r[3]);",
        )
    elif disc in LYN_CLASSIC_ADDRESSES:
        track, decode, decode_return, read_pad, pad_return, open_movie, frame = LYN_CLASSIC_ADDRESSES[disc]
        pending = {track: "extern void gh_probe_track(void); gh_probe_track();"}
        if disc == "SJXE41":
            pending["800716f0"] = "extern void jd4_probe_scene(void); jd4_probe_scene();"
            catalog = "jd4_probe_album"
        else:
            pending[open_movie] = "extern void gh_probe_movie(void); gh_probe_movie();"
            pending[frame] = "extern void gh_probe_video_frame(void); gh_probe_video_frame();"
            catalog = "jd2_probe_catalog"
        sites[decode] = (
            f"L_{decode_return}:;",
            f"L_{decode_return}:;\n"
            f" extern void {catalog}(uint32_t, uint32_t);\n"
            f" if (cpu.r[26] && cpu.r[27]) {catalog}(cpu.r[26], rd32(cpu.r[27]));",
        )
        sites[read_pad] = (
            f"L_{pad_return}:;",
            f"L_{pad_return}:;\n"
            " extern void gh_probe_input(uint32_t, uint32_t, uint32_t, uint32_t);\n"
            " gh_probe_input(rd32s(cpu.r[1] + 580), cpu.r[25], cpu.r[26], cpu.r[29]);",
        )
    elif disc in UBIART_ADDRESSES:
        pending = dict(zip(UBIART_ADDRESSES[disc].split(),
                           (JD2015_HOOKS[key] for key in UBIART_HOOK_KEYS.split()), strict=True))
        pending[UBIART_DISPLAY_HOWTO[disc]] = (
            "extern int jd2015_probe_skip_lobby_howto(void); "
            "if (jd2015_probe_skip_lobby_howto()) { cpu.r[3] = 0; return; }"
        )
    else:
        pending = {"SDNE41": JD1_HOOKS, "SE3E41": JD2015_HOOKS}.get(disc, HOOKS).copy()
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
        for address, (before, after) in tuple(sites.items()):
            marker = f"void f_{address}(void)"
            if (start := modified.find(marker)) < 0:
                continue
            end = modified.index("\n}", start)
            body = modified[start:end]
            if after not in body:
                if body.count(before) != 1:
                    raise ValueError(f"Unexpected {disc} probe hook at {address}")
                modified = modified[:start] + body.replace(before, after) + modified[end:]
            del sites[address]
        if modified != original:
            source.write_text(modified)
        if not pending and not sites:
            break
    if pending or sites:
        raise ValueError(f"Missing {disc} probe hook addresses {', '.join(pending | sites)}")
    if disc in LYN_STREAM_SITES:
        fix_lyn_stream_wait(directory, disc)


# STD_b_IOReadFileAsyncWait in the verified LyN executables.
LYN_STREAM_SITES = {
    "SD2Y41": ("8004903c", "80049128", "80343110", "803470b0", "80343150"),
    "SJ9E41": ("8003a9d8", "8003aac0", "80302730", "803064f0", "80302770"),
    "SJDE41": ("8004f874", "8004f960", "8032acf0", "8032ed10", "8032ad30"),
    "SJHE41": ("80046fac", "80047094", "802feaa0", "80302a90", "802feae0"),
}


def fix_lyn_stream_wait(directory, disc):
    function, resume, disable, suspend, restore = LYN_STREAM_SITES[disc]
    call = f" if (1) {{ cpu.lr = 0x{resume}u; f_{suspend}(); }}"
    wait = f"""{{
  // The DVD callback clears file+172 before resuming the waiting thread.
  // Keep interrupts disabled from the pending check through OS suspend.
  cpu.lr = 0x{resume}u;
  f_{disable}();
  unsigned interrupts_enabled = cpu.r[3];
  if (rd32(cpu.r[27] + 172)) {{
    cpu.r[3] = cpu.r[28];
    f_{suspend}();
  }}
  cpu.r[3] = interrupts_enabled;
  f_{restore}();
}}"""
    for source in directory.glob("*.c"):
        original = source.read_text()
        if wait in original:
            return
        if call in original:
            if f"void f_{function}(void)" not in original or original.count(call) != 1:
                raise ValueError(f"Unexpected {disc} streaming wait")
            source.write_text(original.replace(call, wait))
            return
    raise ValueError(f"Missing {disc} streaming wait in {function}")


JD2_OPEN_BIGFILE = " if (1) { cpu.lr = 0x8000e558u; dispatch(cpu.ctr); }"
JD2_BIGFILE_NAME = """  // This revision's FST calls the original archive WD2.bbf.
  if (!__builtin_memcmp(host(cpu.r[4]), "WD2.bf", sizeof "WD2.bf"))
    __builtin_memcpy(host(cpu.r[4]), "WD2.bbf", sizeof "WD2.bbf");
"""


def fix_jd2_bigfile(directory):
    for source in directory.glob("*.c"):
        original = source.read_text()
        if JD2_BIGFILE_NAME in original:
            return
        if JD2_OPEN_BIGFILE in original:
            if original.count(JD2_OPEN_BIGFILE) != 1:
                raise ValueError("Unexpected JD2 BigFile open at 8000e554")
            source.write_text(original.replace(JD2_OPEN_BIGFILE, JD2_BIGFILE_NAME + JD2_OPEN_BIGFILE))
            return
    raise ValueError("Missing JD2 BigFile open at 8000e554")

# JD3 revision 1. The main executable's ViD startup flag enables the disc's
# original compiled script module. The title hook runs after its normal setup.
JD3_COMPILED_SCRIPTS_GATE = "cpu.r[0] = rd32((cpu.r[13] + -27936));"
JD3_COMPILED_SCRIPTS_CALL = " if (1) { cpu.lr = 0x80014444u; f_8018b29c(); }"
JD3_TRANSITIONS = {
    "c00eb2d4": ("jd3_probe_title_ready", "uint32_t, uint32_t", "cpu.r[30], m0_code"),
    "c0076f8c": ("jd3_probe_hud_entered", "void", ""),
}
JD3_HOOKS = {
    "c01780a8": "jd3_probe_map",
    "c00bbb10": "jd3_probe_lobby_entered",
    "c00bdffc": "jd3_probe_lobby_update",
    "c0058644": "jd3_probe_game_started",
    "c01a57d4": "jd3_probe_score",
    "c0155220": "jd3_probe_recap",
}


def instrument_jd3(directory):
    instrument(directory, "SJDE41")
    pending = JD3_HOOKS.copy()
    transitions = JD3_TRANSITIONS.copy()
    enabled = completion = False
    for source in directory.glob("*.c"):
        original = source.read_text()
        modified = original
        if JD3_COMPILED_SCRIPTS_CALL in original:
            end = original.index(JD3_COMPILED_SCRIPTS_CALL)
            gate = original.rindex(JD3_COMPILED_SCRIPTS_GATE, 0, end)
            line = "  extern void jd3_probe_enable_compiled_scripts(void); jd3_probe_enable_compiled_scripts();\n"
            if not original[:gate].endswith(line):
                modified = original[:gate] + line + original[gate:]
            enabled = True
        for address, (name, parameters, arguments) in tuple(transitions.items()):
            marker = f"void f_{address}(void)"
            if marker not in modified:
                continue
            start = modified.index(marker)
            end = modified.index("\n}", start)
            hook = f"  extern void {name}({parameters}); {name}({arguments});\n"
            if hook not in modified[start:end]:
                tail = modified.rindex("cpu.r[0] = rd32s", start, end)
                modified = modified[:tail] + hook + modified[tail:]
            del transitions[address]
        marker = "void f_c00bcb20(void) { POLL_COUNTED(3008);\n"
        if marker in modified:
            hook = ("  extern int jd3_probe_first_lobby_completion(void);"
                    " if (!jd3_probe_first_lobby_completion()) return;\n")
            after = modified.index(marker) + len(marker)
            if not modified.startswith(hook, after):
                modified = modified[:after] + hook + modified[after:]
            completion = True
        for address, hook in tuple(pending.items()):
            match = re.search(rf"(?m)^void f_{address}\(void\) \{{ POLL(?:_COUNTED)?\(\d*\);\n", modified)
            if match:
                line = f"  extern void {hook}(void); {hook}();\n"
                after = match.end()
                if not modified.startswith(line, after):
                    modified = modified[:after] + line + modified[after:]
                del pending[address]
        if modified != original:
            source.write_text(modified)
    if pending or transitions or not enabled or not completion:
        missing = ", ".join(pending | transitions)
        raise ValueError(f"Missing JD3 instrumentation: startup={enabled}, completion={completion}, hooks={missing}")
