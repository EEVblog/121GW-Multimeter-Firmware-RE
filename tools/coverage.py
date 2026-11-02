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
}


def orig_tags(src_dir):
    tags = {}
    for dirpath, _, files in os.walk(src_dir):
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            in_tag = False   # inside a comment that started with orig:/inlines:
            for lineno, line in enumerate(open(path, encoding="utf-8"), 1):
                if "orig:" in line or "inlines:" in line:
                    in_tag = True
                if in_tag:
                    for m in re.finditer(r"0x([0-9A-Fa-f]{8})", line):
                        tags.setdefault(int(m.group(1), 16), "%s:%d" % (rel, lineno))
                    if "*/" in line:
                        in_tag = False
    return tags


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "bootloader"
    tsv = os.path.join(ROOT, "ghidra", "export", name, "functions.tsv")
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
    for r in (missing if name == "bootloader" else []):
        print("  UNACCOUNTED %s %-24s %s bytes  callers: %s" % (r["addr"], r["name"], r["size"],
                                                             r["callers"][:60]))
    print("wrote", os.path.relpath(out, ROOT))
    if name == "bootloader":
        sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
