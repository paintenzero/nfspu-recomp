"""Generate private lifted C from a local supported game installation.

Function catalogs contain addresses/call metadata, not executable bytes.
Generated output and original game files must remain outside Git.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "third_party/pcrecomp"))

INPUTS = (
    ("Porsche.exe", "analysis/functions.json", "gen", 400,
     "02437ddbf41af86dffe14251b28ed8889bd1c4c904fe77ff730d8912257ec90d"),
    ("setup/thrash/dx7z.dll", "analysis/dx7z-functions.json", "dx7z", 200,
     "0433cc93106603e9e0a81168e2fa44c40e3121a71b7ae9cd6f3b1f7551f3fc18"),
)


def replace_once(paths, anchor, replacement):
    matches = [(p, p.read_text(encoding="utf-8")) for p in paths
               if anchor in p.read_text(encoding="utf-8")]
    if len(matches) != 1 or matches[0][1].count(anchor) != 1:
        raise RuntimeError(f"Native adapter anchor changed: {anchor}")
    path, source = matches[0]
    path.write_text(source.replace(anchor, replacement), encoding="utf-8")


def adapt(paths):
    # Restore loop scheduling for this version of the global-register lifter.
    branch = re.compile(r"goto L_([0-9A-Fa-f]{8});.*?/\* 0x([0-9A-Fa-f]{8}): (?:j\w+|loop\w*) ")
    for path in paths:
        lines = []
        for line in path.read_text(encoding="utf-8").splitlines():
            match = branch.search(line)
            if match and int(match[1], 16) <= int(match[2], 16):
                if not lines or lines[-1].strip() != "RECOMP_BACKEDGE();":
                    lines.append("    RECOMP_BACKEDGE();")
            lines.append(line)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    anchor = "    RECOMP_ENTER(0x00467FC0u);"
    replace_once(paths, anchor, "    if (recomp_mode_hook) recomp_mode_hook(g_cur_func, esp); /* NFSPU race mode adapter */\n" + anchor)
    anchor = "    RECOMP_ENTER(0x005391B0u);"
    replace_once(paths, anchor, "    uint32_t _nfspu_camera = ecx; /* NFSPU camera capture */\n" + anchor)
    anchor = "    RECOMP_FLAGS_OUT(); esp += 20; return; /* 0x005392C3: ret 0x10 */"
    replace_once(paths, anchor, "    if (recomp_projection_hook) recomp_projection_hook(_nfspu_camera); /* NFSPU Hor+ adapter */\n" + anchor)
    anchor = "    RECOMP_FLAGS_OUT(); esp += 4; return; /* 0x0041FEC0: ret  */"
    replace_once(paths, anchor, "    { extern void nfspu_hud_layout_apply(void); nfspu_hud_layout_apply(); } /* NFSPU HUD arrangement */\n" + anchor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=ROOT / "game")
    parser.add_argument("--output-root", type=Path, default=ROOT / "src/recomp")
    args = parser.parse_args()
    # Check every input before creating output. Only this exact lift is supported.
    for relative, catalog, folder, split, expected in INPUTS:
        image = args.game_dir / relative
        if not image.is_file() or hashlib.sha256(image.read_bytes()).hexdigest() != expected:
            parser.error(f"Missing or unsupported game image: {image}; see README.md")
        output = args.output_root / folder
        if output.exists() and any(output.glob("recomp_*")):
            parser.error(f"Refusing to overwrite an existing private lift: {output}")
    try:
        from tools.lift.translator import load_pe
        from tools.disasm.disasm32 import Disassembler
        from tools.lift.lift32 import Lifter
    except ImportError as error:
        parser.error(f"Install requirements-lift.txt first: {error}")
    entries = []
    paths = []
    for relative, catalog, folder, split, expected in INPUTS:
        info, data, iat = load_pe(str(args.game_dir / relative))
        disassembler = Disassembler(data, info.image_base, info.sections)
        lifter = Lifter(iat_map=iat)
        records = sorted(json.loads((ROOT / catalog).read_text()), key=lambda row: row["address_int"])
        output = args.output_root / folder
        output.mkdir(parents=True, exist_ok=True)
        module_entries = []
        for offset in range(0, len(records), split):
            chunk = records[offset:offset + split]
            path = output / f"recomp_{offset // split:04d}.c"
            lines = ["/* Private binary-derived output. Do not redistribute. */",
                     "#define RECOMP_GENERATED_CODE", '#include "recomp_types.h"',
                     "#include <math.h>", "#include <string.h>", ""]
            for record in chunk:
                function = disassembler.disassemble_function(record["address_int"], iat)
                if function is None:
                    raise RuntimeError(f"Could not recover {record['address']}")
                function.name = record["name"]
                lines.append(lifter.lift_function(function))
                module_entries.append((function.address, function.name))
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            paths.append(path)
            print(f"{folder}: {offset + len(chunk)}/{len(records)} functions", flush=True)
        declarations = [f"void {name}(void);" for address, name in module_entries]
        (output / "recomp_funcs.h").write_text("#pragma once\n" + "\n".join(declarations) + "\n", encoding="utf-8")
        write_dispatch(output / "recomp_dispatch.c", module_entries)
        entries.extend(module_entries)
    entries = sorted(set(entries))
    write_dispatch(args.output_root / "recomp_dispatch.c", entries)
    (args.output_root / "recomp_decls.h").write_text(
        "#pragma once\n" + "\n".join(f"void {name}(void);" for address, name in entries) + "\n", encoding="utf-8")
    adapt(paths)
    print(f"Prepared {len(entries)} functions in {args.output_root}")


def write_dispatch(path, entries):
    lines = ['#include "recomp_types.h"', ""]
    lines += [f"void {name}(void);" for address, name in entries]
    lines += ["const recomp_dispatch_entry_t recomp_dispatch_table[] = {"]
    lines += [f"    {{ 0x{address:08X}u, {name} }}," for address, name in entries]
    lines += ["};", f"const uint32_t recomp_dispatch_count = {len(entries)};"]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
