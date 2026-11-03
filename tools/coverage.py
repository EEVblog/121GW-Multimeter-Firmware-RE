"""
Cross-checks every function Ghidra found in an original image against the
reconstruction: `orig: 0x...` tags in the C sources, plus tables of library
code (FatFs, IAR runtime) that is replaced rather than transcribed.

    python -P tools/coverage.py bootloader
Writes docs/coverage_<name>.csv and lists anything unaccounted for.
"""
import csv
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

LIBRARY = {
    "bootloader": {
        # FatFs R0.08a (common/fatfs/ff.c, unmodified)
        0x08001E18: "FatFs mem_cpy", 0x08001E34: "FatFs mem_set", 0x08001E4A: "FatFs mem_cmp",
        0x08001E6E: "FatFs chk_chr", 0x08001E82: "FatFs move_window", 0x08001EFA: "FatFs sync",
        0x08001FD2: "FatFs clust2sect", 0x08001FEC: "FatFs get_fat", 0x080020EA: "FatFs put_fat",
        0x0800224E: "FatFs remove_chain", 0x080022C0: "FatFs create_chain",
        0x0800239C: "FatFs dir_sdi", 0x08002452: "FatFs dir_next", 0x08002592: "FatFs dir_find",
        0x080025FA: "FatFs dir_register", 0x0800267C: "FatFs create_name",
        0x080027E2: "FatFs follow_path", 0x08002882: "FatFs check_fs",
        0x080028FC: "FatFs chk_mounted", 0x08002D0A: "FatFs validate", 0x08002D38: "FatFs f_mount",
        0x08002D84: "FatFs f_open", 0x08002F2A: "FatFs f_read", 0x08003136: "FatFs f_sync",
        0x080031FA: "FatFs f_close",
        # IAR C runtime start-up, replaced by bootloader/src/startup.c
        0x08001DEA: "IAR __iar_copy_init3 -> startup.c", 0x08003216: "IAR __iar_zero_init3 -> startup.c",
        0x08004E14: "IAR __iar_data_init3 -> startup.c", 0x08004E98: "IAR __cmain -> startup.c",
        0x08004EB2: "IAR __low_level_init (returns 1) -> startup.c",
        0x08004EB6: "IAR exit thunk -> startup.c", 0x08004EBC: "IAR exit -> startup.c",
        0x08004EC8: "IAR __exit (semihosting BKPT loop) -> startup.c",
    },
    "app": {
        # FatFs R0.08a (common/fatfs/ff.c, unmodified); API functions are checked
        # against the original in tools/emu/test_app_functions.py
        **{a: "FatFs " + n for a, n in (
            (0x08020B84, "mem_cpy"), (0x08020BA0, "mem_set"), (0x08020BB6, "mem_cmp"),
            (0x08020BDA, "chk_chr"), (0x08020BEE, "move_window"), (0x08020C66, "sync"),
            (0x08020D3E, "clust2sect"), (0x08020D58, "get_fat"), (0x08020E3A, "put_fat"),
            (0x08020F7C, "remove_chain"), (0x08020FEE, "create_chain"), (0x080210CA, "dir_sdi"),
            (0x08021180, "dir_next"), (0x080212B2, "dir_find"), (0x0802131A, "dir_read"),
            (0x0802137E, "dir_register"), (0x08021400, "create_name"), (0x0802156E, "get_fileinfo"),
            (0x08021612, "follow_path"), (0x080216B2, "check_fs"), (0x0802172C, "chk_mounted"),
            (0x08021B50, "validate"), (0x08021B80, "f_mount"), (0x08021BCC, "f_open"),
            (0x08021D72, "f_read"), (0x08021F60, "f_write"), (0x08022188, "f_sync"),
            (0x0802224C, "f_close"), (0x08022268, "f_lseek"), (0x0802242C, "f_opendir"),
            (0x080224B6, "f_readdir"), (0x08022526, "f_stat"), (0x0802256E, "f_utime"))},
        0x08023CA0: "IAR strlen -> firmware/src/libc_min.c strlen",
        0x08013EC4: "IAR __aeabi_i2d -> libgcc",
        0x08013EDC: "IAR __aeabi_ui2d -> libgcc",
        0x0801FB9C: "IAR __aeabi_dsub -> libgcc",
        0x08028D12: "IAR __iar_zero_init3 (loop) -> firmware/src/startup.c",
    },
}


ADDR = r"0x([0-9A-Fa-f]{8})"


def _tag_text(tags, mode, text, where):
    if mode == "orig-list":
        # "orig: 0xA", "orig: 0xA..0xB", "orig: 0xA, 0xB, 0xC - prose"
        # (optionally "Label 0xA", "0xA / 0xB")
        item = r"(?:[A-Za-z_]+\s+)?" + ADDR + r"(?:\s*\([^)]*\))?"
        m = re.match(r"\s*" + item + r"(?:\s*(?:,|\.\.|/)\s*" + item + r")*", text)
        if m:
            for a in re.finditer(ADDR, m.group(0)):
                tags.setdefault(int(a.group(1), 16), where)
    elif mode == "inlines":
        for a in re.finditer(ADDR + r"(?=\s*[(A-Za-z_/])", text):   # "0xADDR (name)" / "0xADDR Name"
            tags.setdefault(int(a.group(1), 16), where)


def orig_tags(src_dir):
    tags = {}
    for dirpath, _, files in os.walk(src_dir):
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            mode = None   # "orig" / "inlines" while inside a tagged comment
            cont = False
            for lineno, line in enumerate(open(path, encoding="utf-8"), 1):
                where = "%s:%d" % (rel, lineno)
                # credit only addresses actually listed after a marker,
                # not any address mentioned in surrounding prose
                pos = 0
                if cont:   # previous orig: line ended with "," -> list continues
                    _tag_text(tags, "orig-list", line.lstrip().lstrip("*"), where)
                    cont = False
                for m in re.finditer(r"\b(orig|inlines):", line):
                    _tag_text(tags, mode, line[pos:m.start()], where)
                    mode, pos = m.group(1), m.end()
                    if mode == "orig":
                        _tag_text(tags, "orig-list", line[pos:], where)
                        cont = line.rstrip().endswith(",")
                        pos = len(line)
                _tag_text(tags, mode, line[pos:], where)
                if "*/" in line:
                    mode = None
    return tags


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "bootloader"
    tsv = os.path.join(ROOT, "ghidra", "export", name, "functions.tsv")
    if not os.path.isfile(tsv):
        sys.exit("error: %s not found. ghidra/export/ is git-ignored; generate it with the "
                 "Ghidra export scripts (see docs/tooling.md) first." % os.path.relpath(tsv, ROOT))
    src_dir = {"app": "firmware"}.get(name, name)
    tags = orig_tags(os.path.join(ROOT, src_dir))
    lib = LIBRARY.get(name, {})
    rows, missing = [], []
    for r in csv.DictReader(open(tsv), delimiter="\t"):
        addr = int(r["addr"], 16)
        if addr in tags:
            status, where = "reconstructed", tags[addr]
        elif addr in lib:
            status, where = "library", lib[addr]
        elif name == "app" and (r["name"].startswith("iar_") or r["name"] == "__errno"):
            # IAR soft-float/integer runtime: GCC's libgcc, with numeric.h where
            # IAR's results differ (saturation, NaN sign, divide by zero)
            status, where = "library", "IAR runtime -> libgcc / firmware/include/numeric.h"
        elif r["name"].endswith("IRQHandler") and int(r["size"]) <= 4:
            status, where = "default handler", "%s/src/startup.c Default_Handler" % src_dir
        else:
            status, where = "UNACCOUNTED", ""
            missing.append(r)
        rows.append((r["addr"], r["name"], r["size"], status, where))

    out = os.path.join(ROOT, "docs", "coverage_%s.csv" % name)
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["address", "ghidra_name", "size", "status", "where"])
        w.writerows(rows)
    counts = {}
    for row in rows:
        counts[row[3]] = counts.get(row[3], 0) + 1
    print("%d functions: %s" % (len(rows), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    for r in missing:
        print("  UNACCOUNTED %s %-24s %s bytes  callers: %s" % (r["addr"], r["name"], r["size"],
                                                             r["callers"][:60]))
    print("wrote", os.path.relpath(out, ROOT))
    if missing:
        print("%d UNACCOUNTED function(s)" % len(missing))
        sys.exit(1)


if __name__ == "__main__":
    main()
