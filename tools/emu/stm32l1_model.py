"""
Minimal STM32L15xxD model on Unicorn, for comparing the original and rebuilt
121GW firmware images.

Peripherals are modelled at register level only as far as the bootloader
needs: RCC, PWR, FLASH (+option bytes), LCD, GPIO, TIM2, SDIO with an SD card,
DMA2 channel 4, and the NVIC/SCB. Every peripheral write is recorded as
(context, register, resulting value), so two images that drive the hardware
identically produce identical traces.
"""
import struct
from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_BLOCK,
                     UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_UNMAPPED, UC_PROT_ALL)
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M3, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR,
                               UC_ARM_REG_XPSR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
                               UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10,
                               UC_ARM_REG_R11, UC_ARM_REG_R12)

FLASH_BASE, FLASH_SIZE = 0x08000000, 0x60000
RAM_BASE, RAM_SIZE = 0x20000000, 0xC000
PERIPH_BASE, PERIPH_SIZE = 0x40000000, 0x30000
BITBAND_BASE, BITBAND_SIZE = 0x42000000, 0x600000
OB_BASE = 0x1FF80000
SCS_BASE = 0xE000E000
RET_MAGIC = 0x0F000000          # exception-return trampoline
APP_BASE = 0x08006000

RCC, PWR, FLASHIF, LCD = 0x40023800, 0x40007000, 0x40023C00, 0x40002400
ADC1, DBGMCU_BASE = 0x40012400, 0xE0042000
EEPROM_BASE = 0x08080000
TIM2, SDIO, DMA1, DMA2 = 0x40000000, 0x40012C00, 0x40026000, 0x40026400
GPIO = {"A": 0x40020000, "B": 0x40020400, "C": 0x40020800, "D": 0x40020C00,
        "E": 0x40021000, "H": 0x40021400, "F": 0x40021800, "G": 0x40021C00}

IRQ_TIM2, IRQ_SDIO, IRQ_DMA2_CH4 = 28, 45, 53
CTX_REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
            UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9,
            UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_LR, UC_ARM_REG_SP,
            UC_ARM_REG_XPSR]


def reg_name(addr):
    names = {RCC: "RCC", PWR: "PWR", FLASHIF: "FLASH", LCD: "LCD", TIM2: "TIM2", SDIO: "SDIO",
             DMA1: "DMA1", DMA2: "DMA2", OB_BASE: "OB", SCS_BASE: "SCS"}
    names.update({v: "GPIO" + k for k, v in GPIO.items()})
    for base in sorted(names, reverse=True):
        if base <= addr < base + 0x1000:
            return "%s+0x%03X" % (names[base], addr - base)
    return "0x%08X" % addr


class SdCard:
    """SD card answering SDIO commands, backed by a disk image."""

    def __init__(self, image, high_capacity, v2=True, absent=False):
        self.image = image
        self.hc = high_capacity
        self.v2 = v2 or high_capacity
        self.absent = absent
        self.rca = 0x1234
        self.state = 0
        self.acmd41_tries = 0
        self.app_cmd = False
        self.pending_read = None
        self.pending_write = None
        self.scr_words = []

    def status(self):
        return (self.state << 9) | 0x100 | (0x20 if self.app_cmd else 0)

    def command(self, index, arg):
        """Returns (kind, response words, respcmd) where kind is 'none', 'short',
        'long', 'r3' or 'timeout'."""
        app = self.app_cmd
        self.app_cmd = False
        if self.absent:
            return ("none", [], 0) if index == 0 else ("timeout", [], 0)
        if index == 0:
            self.state = 0
            return "none", [], 0
        if index == 8:
            return ("short", [arg & 0xFFF], 8) if self.v2 else ("timeout", [], 0)
        if index == 55:
            self.app_cmd = True
            return "short", [self.status()], 55
        if app and index == 41:
            self.acmd41_tries += 1
            ocr = 0x00FF8000
            if self.acmd41_tries > 2:
                ocr |= 0x80000000 | (0x40000000 if self.hc else 0)
                self.state = 1
            return "r3", [ocr], 0x3F
        if index == 2:
            self.state = 2
            return "long", [0x1D414453, 0x44202020, 0x10A0B0C0, 0xD0012345], 0x3F
        if index == 3:
            self.state = 3
            return "short", [(self.rca << 16) | 0x0500], 3
        if index == 9:
            if self.hc:   # CSD v2.0, C_SIZE = 0x3B37 (~7.5 GB)
                csd = [0x400E0032, 0x5B590000, 0x3B377F80, 0x0A404000]
            else:         # CSD v1.0, 1 GB
                csd = [0x002E0032, 0x5F5983D5, 0xEDB707FF, 0x96400000]
            return "long", csd, 0x3F
        if index == 7:
            st = self.status()
            self.state = 4
            return "short", [st], 7
        if index in (16, 13):
            return "short", [self.status()], index
        if app and index == 51:
            self.scr_words = [0x00003502, 0x00000000]
            return "short", [self.status()], 51
        if app and index == 6:
            return "short", [self.status()], 6
        if index == 17:
            addr = arg * 512 if self.hc else arg
            self.pending_read = bytes(self.image[addr:addr + 512])
            return "short", [self.status()], 17
        if app and index == 23:
            return "short", [self.status()], 23
        if index == 25:
            self.pending_write = arg * 512 if self.hc else arg
            self.state = 6       # receive-data
            return "short", [self.status()], 25
        if index == 12:
            st = self.status()
            self.state = 4
            return "short", [st], 12
        raise RuntimeError("unmodelled SD command %d (app=%s)" % (index, app))


class Stm32l1:
    def __init__(self, flash_image, *, keys_idr=0xFFFF, obr=0x00F000AA, hse_ok=True,
                 sd_image=None, sd_high_capacity=False, sd_v2=True, sd_absent=False,
                 tick_blocks=0, corrupt_flash_at=None, extra_code=None,
                 stop_on_app_entry=True, eeprom_image=None, pwr_csr=0x8):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        uc = self.uc
        uc.mem_map(FLASH_BASE, FLASH_SIZE, UC_PROT_ALL)
        uc.mem_write(FLASH_BASE, bytes(flash_image).ljust(FLASH_SIZE, b"\x00"))
        uc.mem_map(RAM_BASE, RAM_SIZE, UC_PROT_ALL)
        # Rebuilt code under test lives outside real flash (emulator only).
        if extra_code is not None:
            base, code = extra_code
            uc.mem_map(base, (len(code) + 0xFFFF) & ~0xFFFF, UC_PROT_ALL)
            uc.mem_write(base, bytes(code))
        self.stop_on_app_entry = stop_on_app_entry
        self.pwr_csr = pwr_csr          # PWR_CSR as read (WUF/SBF/VREFINTRDYF)
        # Data EEPROM (calibration/settings): plain memory, writes traced.
        uc.mem_map(EEPROM_BASE, 0x3000 + 0x1000, UC_PROT_ALL)
        if eeprom_image is not None:
            uc.mem_write(EEPROM_BASE, bytes(eeprom_image)[:0x3000])
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_eeprom_write, begin=EEPROM_BASE,
                    end=EEPROM_BASE + 0x2FFF)
        uc.mem_map(RET_MAGIC, 0x1000, UC_PROT_ALL)
        uc.mem_write(RET_MAGIC, b"\xFE\xE7")   # b .
        uc.mmio_map(PERIPH_BASE, PERIPH_SIZE, self._mmio_read, PERIPH_BASE,
                    self._mmio_write, PERIPH_BASE)
        uc.mmio_map(BITBAND_BASE, BITBAND_SIZE, self._bb_read, None, self._bb_write, None)
        uc.mmio_map(OB_BASE, 0x1000, self._mmio_read, OB_BASE, self._mmio_write, OB_BASE)
        uc.mmio_map(SCS_BASE, 0x1000, self._mmio_read, SCS_BASE, self._mmio_write, SCS_BASE)
        uc.mmio_map(DBGMCU_BASE, 0x1000, self._mmio_read, DBGMCU_BASE, self._mmio_write,
                    DBGMCU_BASE)

        self.regs = {}
        # keys_idr: a constant IDR value, or a list of (reads_before, value) steps
        self.keys_schedule = keys_idr if isinstance(keys_idr, list) else [(0, keys_idr)]
        self.keys_reads = 0
        # Per-port IDR source: constant, or callable(model) -> value (port letter keys).
        self.idr = {}
        self.idr_reads = {}
        self.hse_ok = hse_ok
        self.trace = []
        self.lcd_frames = []
        self.ob_writes = []
        self.events = []
        self.ctx = "main"
        self.irq_stack = []
        self.pending_irqs = set()
        self.stop_reason = None
        self.blocks = 0
        self.tick_blocks = tick_blocks
        self.flash_unlock_seq = {"pe": 0, "prg": 0, "opt": 0}
        self.erase_pending = []
        self.sd = (SdCard(sd_image, sd_high_capacity, sd_v2, sd_absent)
                   if sd_image is not None else None)
        self.corrupt_flash_at = corrupt_flash_at   # a programmed word that reads back wrong
        self.corrupt_pending = False

        r = self.regs
        r[RCC + 0x00] = 0x00000300
        r[RCC + 0x04] = 0x0000B000
        r[RCC + 0x34] = 0x0C000000
        r[PWR + 0x00] = 0x00001000
        r[FLASHIF + 0x04] = 0x00000007
        r[FLASHIF + 0x18] = 0x0000000C
        r[FLASHIF + 0x1C] = obr
        r[SCS_BASE + 0xD00] = 0x412FC231
        r[SCS_BASE + 0xD0C] = 0xFA050000

        uc.hook_add(UC_HOOK_BLOCK, self._on_block)
        uc.hook_add(UC_HOOK_CODE, self._on_return, begin=RET_MAGIC, end=RET_MAGIC + 1)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_flash_write, begin=FLASH_BASE,
                    end=FLASH_BASE + FLASH_SIZE - 1)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._on_unmapped)

    # ---- tracing --------------------------------------------------------
    def _record(self, addr, value):
        self.trace.append((self.ctx, addr, value & 0xFFFFFFFF))

    # ---- generic register file -------------------------------------------
    def _get(self, addr):
        return self.regs.get(addr, 0)

    def _mmio_read(self, uc, offset, size, base):
        addr = base + offset
        word = addr & ~3
        value = self._read_reg(word)
        shift = (addr & 3) * 8
        return (value >> shift) & ((1 << (size * 8)) - 1)

    def _mmio_write(self, uc, offset, size, value, base):
        addr = base + offset
        word = addr & ~3
        shift = (addr & 3) * 8
        mask = ((1 << (size * 8)) - 1) << shift
        self._write_reg(word, (value << shift) & mask, mask)

    def _bb_read(self, uc, offset, size, _):
        byte = PERIPH_BASE + (offset >> 5)
        bit = ((byte & 3) * 8) + ((offset >> 2) & 7)
        return (self._read_reg(byte & ~3) >> bit) & 1

    def _bb_write(self, uc, offset, size, value, _):
        byte = PERIPH_BASE + (offset >> 5)
        word = byte & ~3
        bit = ((byte & 3) * 8) + ((offset >> 2) & 7)
        self._write_reg(word, (value & 1) << bit, 1 << bit)

    def _read_reg(self, word):
        r = self._get(word)
        if word == RCC + 0x00:
            on = r
            r = (on & ~0x02020202) | ((on & 1) << 1) | ((on & 0x100) << 1) \
                | (((on & 0x10000) << 1) if self.hse_ok else 0) | ((on & 0x1000000) << 1)
        elif word == RCC + 0x08:
            r = (r & ~0xC) | ((r & 3) << 2)
        elif word == RCC + 0x34:
            r = (r & ~0x202) | ((r & 1) << 1) | ((r & 0x100) << 1)
        elif word == PWR + 0x04:
            r = self.pwr_csr
        elif word == ADC1:                     # SR: ADONS follows CR2.ADON
            r = (r & ~0x40) | ((self._get(ADC1 + 0x08) & 1) << 6)
        elif word == LCD + 0x08:
            en = self._get(LCD) & 1
            r = (r & 0x4) | 0x20 | (en * 0x11)
        elif word == GPIO["G"] + 0x10 and "G" not in self.idr:
            r = [v for n, v in self.keys_schedule if self.keys_reads >= n][-1]
            self.keys_reads += 1
        elif (word & 0x3FF) == 0x10 and (word & ~0x3FF) in GPIO.values():
            port = next(k for k, v in GPIO.items() if v == word - 0x10)
            src = self.idr.get(port)
            if callable(src):
                r = src(self) & 0xFFFF
            elif src is not None:
                r = src & 0xFFFF
            self.idr_reads[port] = self.idr_reads.get(port, 0) + 1
        elif word == SDIO + 0x34:
            r = self._sdio_status()
        elif word == SDIO + 0x80 or (SDIO + 0x80 <= word < SDIO + 0x100):
            r = self.sd.scr_words.pop(0) if self.sd and self.sd.scr_words else 0
        return r

    # Read-only bits per register: writes leave them unchanged (as on silicon).
    READ_ONLY = {RCC + 0x00: 0x02020202,      # HSIRDY, MSIRDY, HSERDY, PLLRDY
                 RCC + 0x08: 0x0000000C,      # SWS
                 RCC + 0x34: 0xFE000202}      # LSIRDY, LSERDY, reset flags

    def _write_reg(self, word, value, mask):
        old = self._get(word)
        new = (old & ~mask) | value
        ro = self.READ_ONLY.get(word, 0)
        new = (new & ~ro) | (old & ro)
        if word == FLASHIF + 0x04:            # lock bits can be set but not cleared by writes
            new = (new & ~0x7) | ((old | new) & 0x7)

        # write-only / special registers
        if (word & ~0x3FF) in GPIO.values() and (word & 0x3FF) == 0x18:
            odr = word - 0x18 + 0x14
            o = self._get(odr)
            o = (o | (value & 0xFFFF)) & ~((value >> 16) & 0xFFFF)
            self.regs[odr] = o
            self._record(odr, o)
            return
        if word == TIM2 + 0x10:          # SR: write 0 to clear (16 significant bits)
            self.regs[word] = old & (value | ~mask)
            self._record(word, (value | ~mask) & 0xFFFF)
            self._update_irqs()
            return
        if word in (SDIO + 0x38, DMA1 + 0x04, DMA2 + 0x04, FLASHIF + 0x18):
            self._record(word, value)
            if word == SDIO + 0x38:
                self.regs["sdio_sta"] = self.regs.get("sdio_sta", 0) & ~value
            elif word == DMA2 + 0x04:
                self.regs[DMA2] = self._get(DMA2) & ~value
            self._update_irqs()
            return
        if (word & ~0xFF) == SCS_BASE + 0x100 and (word & 0xFF) < 0x20:   # NVIC ISER
            self.regs[word] = old | value
            self._record(word, value)
            self._update_irqs()
            return
        if word == SCS_BASE + 0xD0C:
            new = (new & 0x0000FFFF) | 0xFA050000
        if word == FLASHIF + 0x0C:
            self._flash_key("pe", value, (0x89ABCDEF, 0x02030405), 0x1, None)
            self._record(word, value)
            return
        if word == FLASHIF + 0x10:
            self._flash_key("prg", value, (0x8C9DAEBF, 0x13141516), 0x2, 0x1)
            self._record(word, value)
            return
        if word == FLASHIF + 0x14:
            self._flash_key("opt", value, (0xFBEAD9C8, 0x24252627), 0x4, 0x1)
            self._record(word, value)
            return
        if word == OB_BASE + 0x04:
            self.ob_writes.append(new)

        self.regs[word] = new
        self._record(word, new)

        if word == FLASHIF + 0x04 and (new & (1 << 18)):
            self._stop("option-byte launch (reset)")
        elif word == LCD + 0x08 and (value & 0x4):
            frame = tuple(self._get(LCD + 0x14 + 4 * i) for i in range(8))
            self.lcd_frames.append(frame)
            self.regs[word] = new & ~0x4
        elif word == SDIO + 0x0C and (new & 0x400):
            self._sdio_command(new & 0x3F, self._get(SDIO + 0x08))
        elif word == DMA2 + 0x44 and (new & 1) and not (old & 1):
            self._dma_start()
        self._update_irqs()

    def _flash_key(self, which, value, keys, lock_bit, needs_clear):
        pecr = FLASHIF + 0x04
        seq = self.flash_unlock_seq
        if needs_clear is not None and self._get(pecr) & needs_clear:
            seq[which] = 0
            return
        if seq[which] == 0 and value == keys[0]:
            seq[which] = 1
        elif seq[which] == 1 and value == keys[1]:
            seq[which] = 0
            self.regs[pecr] = self._get(pecr) & ~lock_bit
        else:
            seq[which] = 0

    # ---- flash programming ---------------------------------------------
    def _on_flash_write(self, uc, access, address, size, value, _):
        pecr = self._get(FLASHIF + 0x04)
        if pecr & 0x2:
            raise RuntimeError("flash write while PRGLOCK set at 0x%08X" % address)
        if (pecr & 0x208) == 0x208:
            self.erase_pending.append(address & ~0xFF)
        elif address == self.corrupt_flash_at:
            self.corrupt_pending = True
        self.trace.append((self.ctx, "FLASH@%08X" % address, value & 0xFFFFFFFF))

    def _on_eeprom_write(self, uc, access, address, size, value, _):
        if self._get(FLASHIF + 0x04) & 0x1:
            raise RuntimeError("data EEPROM write while PELOCK set at 0x%08X" % address)
        self.trace.append((self.ctx, "EEPROM@%08X/%d" % (address, size),
                           value & ((1 << (8 * size)) - 1)))

    def _apply_erases(self):
        for page in self.erase_pending:
            self.uc.mem_write(page, b"\x00" * 0x100)
        self.erase_pending.clear()

    # ---- SDIO / DMA -----------------------------------------------------
    def _sdio_status(self):
        sta = self.regs.get("sdio_sta", 0)
        if self.sd and self.sd.scr_words:
            sta |= 1 << 21
        elif self.regs.get("scr_active"):
            sta |= 0x500           # DATAEND | DBCKEND
            self.regs["scr_active"] = False
            self.regs["sdio_sta"] = sta
        return sta

    def _sdio_command(self, index, arg):
        sta = self.regs.get("sdio_sta", 0)
        kind, resp, respcmd = self.sd.command(index, arg)
        if kind == "none":
            sta |= 0x80
        elif kind == "timeout":
            sta |= 0x04
        else:
            sta |= 0x01 if kind == "r3" else 0x40
            self.regs[SDIO + 0x10] = respcmd
            for i, w in enumerate((resp + [0, 0, 0, 0])[:4]):
                self.regs[SDIO + 0x14 + 4 * i] = w
        if index == 51:
            self.regs["scr_active"] = True
        self.regs["sdio_sta"] = sta

    def _dma_start(self):
        ccr = self._get(DMA2 + 0x44)
        cpar = self._get(DMA2 + 0x4C)
        cmar = self._get(DMA2 + 0x50)
        cndtr = self._get(DMA2 + 0x48)
        if cpar != SDIO + 0x80:
            return
        if ccr & 0x10:
            addr = self.sd.pending_write
            if addr is None:
                raise RuntimeError("DMA to card with no pending SD write")
            data = bytes(self.uc.mem_read(cmar, cndtr * 4))
            self.sd.image[addr:addr + len(data)] = data
            self.sd.pending_write = None
            self.regs[DMA2 + 0x48] = 0
            self.regs[DMA2] = self._get(DMA2) | 0x3000          # TCIF4 | GIF4
            self.regs["sdio_sta"] = self.regs.get("sdio_sta", 0) | 0x500
            self.events.append(("sd_write", addr, len(data)))
            return
        data = self.sd.pending_read
        if data is None:
            raise RuntimeError("DMA started with no pending SD read")
        self.uc.mem_write(cmar, data[:cndtr * 4])
        self.sd.pending_read = None
        self.regs[DMA2 + 0x48] = 0
        self.regs[DMA2] = self._get(DMA2) | 0x3000          # TCIF4 | GIF4
        self.regs["sdio_sta"] = self.regs.get("sdio_sta", 0) | 0x500
        self.events.append(("sd_read", cmar))

    # ---- interrupts -----------------------------------------------------
    def _irq_enabled(self, irq):
        return (self._get(SCS_BASE + 0x100 + 4 * (irq >> 5)) >> (irq & 31)) & 1

    def _update_irqs(self):
        if (self._get(DMA2) & 0x2000) and (self._get(DMA2 + 0x44) & 0x2):
            self.pending_irqs.add(IRQ_DMA2_CH4)
        if self.regs.get("sdio_sta", 0) & self._get(SDIO + 0x3C):
            self.pending_irqs.add(IRQ_SDIO)
        if (self._get(TIM2 + 0x10) & 1) and (self._get(TIM2 + 0x0C) & 1):
            self.pending_irqs.add(IRQ_TIM2)
        else:
            self.pending_irqs.discard(IRQ_TIM2)

    def _on_block(self, uc, address, size, _):
        self.blocks += 1
        if self.erase_pending:
            self._apply_erases()
        if self.corrupt_pending:
            self.corrupt_pending = False
            word = struct.unpack("<I", uc.mem_read(self.corrupt_flash_at, 4))[0]
            uc.mem_write(self.corrupt_flash_at, struct.pack("<I", word ^ 0x00000100))
        if self.tick_blocks and self.blocks % self.tick_blocks == 0:
            if (self._get(TIM2) & 1):
                self.regs[TIM2 + 0x10] = self._get(TIM2 + 0x10) | 1
                self._update_irqs()
        if self.stop_on_app_entry and APP_BASE <= address < FLASH_BASE + FLASH_SIZE \
                and not self.irq_stack:
            self._stop("jump to application")
            return
        if not self.irq_stack and any(self._irq_enabled(i) for i in self.pending_irqs):
            uc.emu_stop()

    def _enter_irq(self):
        irq = min(i for i in self.pending_irqs if self._irq_enabled(i))
        self.pending_irqs.discard(irq)
        uc = self.uc
        ctx = [uc.reg_read(r) for r in CTX_REGS]
        pc = uc.reg_read(UC_ARM_REG_PC)
        self.irq_stack.append((ctx, pc))
        vtor = self._get(SCS_BASE + 0xD08)
        handler = struct.unpack("<I", uc.mem_read(vtor + 4 * (16 + irq), 4))[0]
        uc.reg_write(UC_ARM_REG_SP, (ctx[14] - 32) & ~7)
        uc.reg_write(UC_ARM_REG_LR, RET_MAGIC | 1)
        self.ctx = "irq%d" % irq
        return handler | 1

    def _on_return(self, uc, address, size, _):
        uc.emu_stop()

    def _leave_irq(self):
        ctx, pc = self.irq_stack.pop()
        for r, v in zip(CTX_REGS, ctx):
            self.uc.reg_write(r, v)
        self.ctx = "main"
        self._update_irqs()
        return pc | 1

    def _on_unmapped(self, uc, access, address, size, value, _):
        self._stop("unmapped access at 0x%08X (pc 0x%08X)" % (address, uc.reg_read(UC_ARM_REG_PC)))
        return False

    def _stop(self, reason):
        if self.stop_reason is None:
            self.stop_reason = reason
        self.uc.emu_stop()

    # ---- run ------------------------------------------------------------
    def run(self, max_blocks=40_000_000, chunk=5_000_000):
        uc = self.uc
        sp, reset = struct.unpack("<II", uc.mem_read(FLASH_BASE, 8))
        uc.reg_write(UC_ARM_REG_SP, sp)
        pc = reset | 1
        while self.stop_reason is None and self.blocks < max_blocks:
            try:
                uc.emu_start(pc, 0xFFFFFFFF, count=chunk)
            except UcError as e:
                if self.stop_reason is None:
                    self.stop_reason = "emulator error %s at 0x%08X" % (e, uc.reg_read(UC_ARM_REG_PC))
                break
            pc = uc.reg_read(UC_ARM_REG_PC)
            if self.stop_reason:
                break
            if self.irq_stack and (pc & ~1) == RET_MAGIC:
                pc = self._leave_irq()
            elif not self.irq_stack and any(self._irq_enabled(i) for i in self.pending_irqs):
                pc = self._enter_irq()
            else:
                pc |= 1
        if self.stop_reason is None:
            self.stop_reason = "instruction budget exhausted"
        self.final_pc = uc.reg_read(UC_ARM_REG_PC)
        self.final_sp = uc.reg_read(UC_ARM_REG_SP)
        return self

    def call(self, func, args=(), sp=0x20001240, max_blocks=2_000_000):
        """Calls one function (Thumb address) and returns r0, stopping when it
        returns to RET_MAGIC. Interrupts are delivered as in run()."""
        uc = self.uc
        regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
        for r, v in zip(regs, args):
            uc.reg_write(r, v & 0xFFFFFFFF)
        if len(args) > 4:            # AAPCS: further arguments on the stack
            sp = (sp - 4 * (len(args) - 4)) & ~7
            for i, v in enumerate(args[4:]):
                uc.mem_write(sp + 4 * i, struct.pack("<I", v & 0xFFFFFFFF))
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, RET_MAGIC | 1)
        pc = func | 1
        while self.stop_reason is None and self.blocks < max_blocks:
            try:
                uc.emu_start(pc, 0xFFFFFFFF, count=1_000_000)
            except UcError as e:
                self.stop_reason = "emulator error %s at 0x%08X" % (e, uc.reg_read(UC_ARM_REG_PC))
                break
            pc = uc.reg_read(UC_ARM_REG_PC)
            if self.stop_reason:
                break
            if (pc & ~1) == RET_MAGIC:
                if self.irq_stack:
                    pc = self._leave_irq()
                    continue
                self.stop_reason = "returned"
                break
            if not self.irq_stack and any(self._irq_enabled(i) for i in self.pending_irqs):
                pc = self._enter_irq()
            else:
                pc |= 1
        if self.stop_reason is None:
            self.stop_reason = "budget exhausted"
        return uc.reg_read(UC_ARM_REG_R0)

    def flash(self, start=FLASH_BASE, length=FLASH_SIZE):
        return bytes(self.uc.mem_read(start, length))
