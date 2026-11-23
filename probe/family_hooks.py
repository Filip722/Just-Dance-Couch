"""Menu action sites in the verified Land Ho! executables."""

# Each entry records a function and an instruction within it. Keep the
# original scene updates running so they load and release their own assets.
HAN_SITES = {
    "SJ7E41": (
        "80034e0c:80034ec0 8002e9f8:8002eaa4 8002eaf8:8002edb4 "
        "80059028:800590d4 80059128:800594b0 "
        "80035770:80035844 80035770:80035954 80035770:800359b0 "
        "800473ac:80047468 80047b48:80047da8 800473ac:80047514 800473ac:800475a4"
    ),
    "SJ6E41": (
        "80034d94:80034e48 8002ed40:8002edec 8002ee40:8002f13c "
        "800589a0:80058a4c 80058aa0:80058e7c "
        "800356d0:800357a4 800356d0:800358b4 800356d0:80035910 "
        "800468f8:800469b4 80047094:800472f4 800468f8:80046a60 800468f8:80046af0"
    ),
    "S5DE41": (
        "800373b8:8003746c 80030af4:80030ba0 80030bf4:800310dc "
        "- - "
        "80037b1c:80037bf0 80037b1c:80037dec 80037b1c:80037e48 "
        "80046934:800469e4 80046f3c:800471b8 80046934:80046a90 80046934:80046b20"
    ),
}

# Strap, opening, title, dancer selection, and song movie.
HAN_FUNCTIONS = {
    "SJ7E41": ("8001cd78", "8001d450", "80034e0c", "80043098", "80025e24"),
    "SJ6E41": ("8001d2d0", "8001dab4", "80034d94", "80041f90", "80026578"),
    "S5DE41": ("8001d4d0", "8001dd68", None, None, "8002718c"),
}


def family_hooks(disc):
    if disc in EARLY_SITES:
        return earlier_hooks(disc)
    disney2 = disc == "S5DE41"
    strap, opening, title, tips, video = HAN_FUNCTIONS[disc]
    hooks = {
        strap: "extern void family_probe_strap(void); family_probe_strap();",
        opening: "extern void family_probe_opening(unsigned); family_probe_opening(52);",
        video: "extern void family_probe_video(void); family_probe_video();",
    }
    if title:
        hooks[title] = "extern void family_probe_title(void); family_probe_title();"
    if tips:
        # The original dancer selector is open at this state. Use its normal close path.
        hooks[tips] = ("extern int family_probe_active(void); if (family_probe_active()) { "
                       "uint32_t tips = rd32(cpu.r[30] + 124); wr32(tips + 20, 3); }")

    title_self, title_confirm = (28, 30) if disney2 else (31, 29)
    main_self, main_choice = (26, 5) if disney2 else (27, 4)
    lobby_confirm, lobby_self, player = (24, 29, 27) if disney2 else (23, 28, 26)
    actions = [
        f"if (rd32(cpu.r[{title_self}] + 32)) cpu.r[{title_confirm}] = 1;",  # Title confirm
        "if (rd32(cpu.r[28] + 32)) cpu.r[30] = 1;",  # Main menu confirm
        f"wr32(cpu.r[{main_self}] + 28, {main_choice});",  # Dance (or Songs directly)
        "if (rd32(cpu.r[28] + 32)) cpu.r[30] = 1;",  # Dance menu confirm
        "wr32(cpu.r[26] + 28, 5);",  # Songs
        "extern int family_probe_select(uint32_t); "
        "if (family_probe_select(rd32(cpu.r[30] + 36))) cpu.r[29] = 1;",
        "wr32(cpu.r[30] + 28, 9);",  # Queue one song and show player entry
        "extern uint32_t family_probe_song(uint32_t); "
        "wr32(cpu.r[1] + 16, family_probe_song(rd32(cpu.r[1] + 16)));",
        f"cpu.r[{lobby_confirm}] = 1;",  # Confirm the hidden player entry
        f"wr32(cpu.r[{lobby_self}] + 28, 10);",  # Start the selected routine
        f"extern int family_probe_player(unsigned); "
        f"wr32(cpu.r[1] + 40, family_probe_player(cpu.r[{player}]));",
        f"wr32(cpu.r[1] + 40, cpu.r[{player}]);",  # Default icon by remote slot
    ]
    sites = [(address, f"L_{label}:;", action)
             for site, action in zip(HAN_SITES[disc].split(), actions, strict=True) if site != "-"
             for address, label in (site.split(":"),)]
    return hooks, guarded_sites(sites)


def guarded_sites(sites):
    return [(address, (before, f"{before}\n  extern int family_probe_active(void); "
                               f"if (family_probe_active()) {{ {action} }}"))
            for address, before, action in sites]


EARLY_SITES = {
    "SDZE41": (
        # Park at the grip reminder. The Wii title can finish recognizing
        # Player 1 after the launcher hands over the selection and controllers.
        ("8000fd24", "L_8000fec0:;",
         "extern int family_probe_select(uint32_t); "
         "if (family_probe_select(1) && rd32(0x804335f8u) != UINT32_MAX) goto L_8000fef8;"),
        # Let the logo and title states run their original exit actions.
        ("8001d7cc", " if (1) { cpu.lr = 0x8001d804u; dispatch(cpu.ctr); }", "cpu.r[3] = 1;"),
        ("8001da64", " if (1) { cpu.lr = 0x8001dad8u; dispatch(cpu.ctr); }", "cpu.r[3] = 1;"),
        ("80044f64", "cpu.r[0] = rotl(cpu.r[0], 0) & 0x800u;cpu.cr[0] = cr_compare((int32_t)cpu.r[0], 0);",
         "cr_set(2, 0);"),
        ("80037ff0", "L_80038100:;", "cpu.r[3] = cpu.r[31]; f_80038290(); goto L_80038240;"),
        ("80038290", " if (1) { cpu.lr = 0x800382acu; f_8004b280(); }", "cpu.r[3] = 0;"),
        ("80039260", "L_800392a8:;",
         "extern int family_probe_select(uint32_t); "
         "if (family_probe_select(rd32(cpu.r[31] + 60))) { "
         "cpu.r[3] = cpu.r[31]; f_80039670(); goto L_80039654; }"),
        ("80039670", "L_800396a8:;", "cpu.r[3] = 6;"),
        ("80039670", "wr32s((cpu.r[1] + 72), cpu.r[0]);",
         "extern uint32_t family_probe_song(uint32_t); "
         "wr32(cpu.r[1] + 72, family_probe_song(cpu.r[0]));"),
        ("80039670", "wr32((cpu.r[3] + 12), cpu.r[0]);",
         "extern uint32_t family_probe_song(uint32_t); "
         "wr32(cpu.r[3] + 12, family_probe_song(cpu.r[0]));"),
        ("80049660", " if (1) { cpu.lr = 0x800496c0u; f_80049d2c(); }",
         "cpu.r[3] = cpu.r[31]; f_800497c0(); goto L_800497a0;"),
        ("800497c0", "L_80049800:;", "goto L_80049848;"),
        ("80053354", " if (1) { cpu.lr = 0x800533b4u; f_80049d2c(); }", "goto L_80053434;"),
        ("8003df18", " if (1) { cpu.lr = 0x8003e140u; f_8003e740(); }",
         "if (cpu.r[29] == 0) cpu.r[3] = 1;"),  # Use player one's Start action.
        ("8001a3b8", "cpu.r[0] = rd32((cpu.r[3] + 12));",
         "cpu.r[0] |= 0x800u;"),  # Confirm the dance's own grip instructions.
    ),
    "SJZE41": (
        ("80032708", "L_8003275c:;", "if (rd32(cpu.r[30] + 32)) wr32(cpu.r[1] + 8, 1);"),
        ("80032708", "L_80032784:;", "wr32(cpu.r[30] + 28, 2);"),  # Open the song catalog.
        ("8002d690", "L_8002d6e4:;", "if (rd32(cpu.r[30] + 32)) wr32(cpu.r[1] + 8, 1);"),
        ("8002d690", "L_8002d70c:;", "wr32(cpu.r[30] + 28, 2);"),
        ("80032b54", "L_80032da0:;",
         "extern int family_probe_select(uint32_t); "
         "if (family_probe_select(rd32(cpu.r[29] + 36))) wr32(cpu.r[1] + 28, 1);"),
        ("80032b54", "L_80032ddc:;", "wr32(cpu.r[29] + 28, 14);"),  # Add a single song.
        ("80032b54", " if (1) { cpu.lr = 0x80032e30u; f_80027598(); }",
         "extern uint32_t family_probe_song(uint32_t); "
         "wr32(cpu.r[1] + 20, family_probe_song(rd32(cpu.r[1] + 20)));"),
        ("80040728", "L_800407b0:;", "if (rd32(cpu.r[31] + 48)) wr32(cpu.r[1] + 8, 1);"),
        ("80040728", "L_80040810:;", "wr32(cpu.r[1] + 12, cpu.r[30]);"),
        ("80040728", "L_80040884:;",
         "extern int family_probe_player(unsigned); wr32(cpu.r[1] + 16, family_probe_player(cpu.r[30]) >= 0);"),
        ("80040728", "L_800408c0:;", "wr32(cpu.r[31] + 28, 6);"),  # Exit the menu into the dance module.
        ("80041c50", "L_80041ca4:;", "if (rd32(cpu.r[30] + 36)) wr32(cpu.r[1] + 8, 1);"),
        ("80041c50", "L_80041d3c:;", "wr32(cpu.r[30] + 28, 6);"),
    ),
}


def earlier_hooks(disc):
    sites = list(EARLY_SITES[disc])
    if disc == "SDZE41":
        hooks = {
            "8001caf4": ("extern int family_probe_active(void); if (family_probe_active()) "
                         "{ wr32(cpu.r[3] + 44, 0); }"),
            "8001a058": ("extern uint32_t family_probe_song(uint32_t); "
                         "if (rd32(cpu.r[3] + 36) == 1) "
                         "wr32(cpu.r[3] + 44, family_probe_song(rd32(cpu.r[3] + 44)));"),
            "80035bbc": "extern void family_probe_video(void); family_probe_video();",
        }
    else:
        hooks = {
            "80019174": "extern void family_probe_strap(void); family_probe_strap();",
            "800198a8": "extern void family_probe_opening(unsigned); family_probe_opening(44);",
            "80021cf8": "extern void family_probe_video(void); family_probe_video();",
        }
    return hooks, guarded_sites(sites)
