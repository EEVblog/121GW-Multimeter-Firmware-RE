"""
Per-function fingerprint of a Ghidra export, to help group functions into
modules: peripherals touched, data-EEPROM/RAM use, fan-in/fan-out, known name.

    python -P tools/fingerprint.py app [--from 0x08006000] [--to 0x0802A000]
"""
import argparse
import csv
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PERIPH = re.compile(r"\b(GPIO[A-H]|RCC|PWR|Flash|FLASH|LCD|SDIO|DMA[12]|TIM\d+|USART\d|UART\d|"
                    r"SPI\d|I2C\d|ADC1?|ADC_Common|DAC|COMP|RTC|EXTI|SYSCFG|IWDG|WWDG|SCB|NVIC|"
                    r"SysTick|OPAMP|RI|AES|USB|DBGMCU|CRC)_([A-Z0-9_]+?)(?:_bit\d+)?\b")
EEPROM = re.compile(r"DAT_0808[0-2][0-9a-f]{3}|0x808[0-2][0-9a-f]{3}")
RAMVAR = re.compile(r"\bDAT_200[0-9a-f]{5}\b")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--from", dest="lo", default="0")
    ap.add_argument("--to", dest="hi", default="0xFFFFFFFF")
    args = ap.parse_args()
    lo, hi = int(args.lo, 16), int(args.hi, 16)
    exp = os.path.join(ROOT, "ghidra", "export", args.name)

    bodies = {}
    cur = None
    for line in open(os.path.join(exp, "decomp.c"), encoding="utf-8"):
        m = re.match(r"// ==== (\S+) @ ([0-9a-f]+)", line)
        if m:
            cur = int(m.group(2), 16)
            bodies[cur] = []
        elif cur is not None:
            bodies[cur].append(line)

    known = {}
    mt = os.path.join(ROOT, "docs", "app_matches.tsv")
    if args.name == "app" and os.path.exists(mt):
        for r in csv.DictReader(open(mt), delimiter="\t"):
            known[int(r["app_addr"], 16)] = r["name"].split("/")[0]

    for r in csv.DictReader(open(os.path.join(exp, "functions.tsv")), delimiter="\t"):
        a = int(r["addr"], 16)
        if not lo <= a < hi:
            continue
        text = "".join(bodies.get(a, []))
        periph = sorted({p for p, _ in PERIPH.findall(text)})
        ee = "EE" if EEPROM.search(text) else ""
        ram = len(set(RAMVAR.findall(text)))
        nin = len([x for x in r["callers"].split(",") if x])
        nout = len([x for x in r["callees"].split(",") if x])
        print("%08x %5s in%-3d out%-3d ram%-3d %-2s %-30s %s" % (
            a, r["size"], nin, nout, ram, ee, ",".join(periph)[:30], known.get(a, r["name"])))


if __name__ == "__main__":
    main()
