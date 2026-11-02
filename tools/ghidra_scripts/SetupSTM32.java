// Pre-analysis setup for STM32L15xxD images loaded as raw binaries.
// Creates RAM/peripheral/system memory blocks, labels every peripheral register
// from a CMSIS SVD file, and turns the vector table into named handler functions.
//
// Script args: <path to .svd>
// @category 121GW
import java.io.File;
import java.util.*;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

public class SetupSTM32 extends GhidraScript {

    private static final String[] CORE_VECTORS = {
        "__initial_sp", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
        "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler", null,
        null, null, null, "SVC_Handler", "DebugMon_Handler", null,
        "PendSV_Handler", "SysTick_Handler"
    };

    private static final Object[][] CORE_REGS = {
        {"SysTick_CTRL", 0xE000E010L}, {"SysTick_LOAD", 0xE000E014L},
        {"SysTick_VAL", 0xE000E018L}, {"SysTick_CALIB", 0xE000E01CL},
        {"NVIC_ISER0", 0xE000E100L}, {"NVIC_ISER1", 0xE000E104L},
        {"NVIC_ICER0", 0xE000E180L}, {"NVIC_ICER1", 0xE000E184L},
        {"NVIC_ISPR0", 0xE000E200L}, {"NVIC_ISPR1", 0xE000E204L},
        {"NVIC_ICPR0", 0xE000E280L}, {"NVIC_ICPR1", 0xE000E284L},
        {"NVIC_IPR", 0xE000E400L},
        {"SCB_CPUID", 0xE000ED00L}, {"SCB_ICSR", 0xE000ED04L},
        {"SCB_VTOR", 0xE000ED08L}, {"SCB_AIRCR", 0xE000ED0CL},
        {"SCB_SCR", 0xE000ED10L}, {"SCB_CCR", 0xE000ED14L},
        {"SCB_SHPR1", 0xE000ED18L}, {"SCB_SHPR2", 0xE000ED1CL},
        {"SCB_SHPR3", 0xE000ED20L}, {"SCB_SHCSR", 0xE000ED24L},
        {"SCB_CFSR", 0xE000ED28L}, {"SCB_HFSR", 0xE000ED2CL},
        {"DBGMCU_IDCODE", 0xE0042000L}, {"DBGMCU_CR", 0xE0042004L},
        {"U_ID0", 0x1FF800D0L}, {"U_ID1", 0x1FF800D4L}, {"U_ID2", 0x1FF800E4L},
        {"F_SIZE", 0x1FF800CCL},
    };

    private Memory mem;
    private SymbolTable st;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new IllegalArgumentException("usage: SetupSTM32.java <svd>");
        }
        mem = currentProgram.getMemory();
        st = currentProgram.getSymbolTable();

        // The raw-binary loader makes the image writable, which stops the decompiler
        // from treating literal pools as constants (registers then show as *DAT_xxx).
        MemoryBlock image = mem.getBlock(currentProgram.getMinAddress());
        image.setName("FLASH");
        image.setWrite(false);
        image.setExecute(true);

        block("RAM", 0x20000000L, 0xC000, false);
        block("PERIPH", 0x40000000L, 0x30000, true);
        block("PERIPH_BITBAND", 0x42000000L, 0x600000, true);
        block("FSMC", 0xA0000000L, 0x1000, true);
        block("SYSMEM", 0x1FF00000L, 0x2000, false);
        block("OPTBYTES", 0x1FF80000L, 0x100, true);
        block("DATA_EEPROM", 0x08080000L, 0x3000, true);
        block("CORE", 0xE000E000L, 0x1000, true);
        block("DBGMCU", 0xE0042000L, 0x10, true);

        Map<Integer, String> irqNames = loadSvd(new File(args[0]));

        for (Object[] r : CORE_REGS) {
            reg((String) r[0], (Long) r[1], 32);
        }

        int maxIrq = irqNames.keySet().stream().max(Integer::compare).orElse(56);
        applyVectors(currentProgram.getMinAddress(), 16 + maxIrq + 1, irqNames);
    }

    private void block(String name, long start, int size, boolean volatileFlag) throws Exception {
        Address a = toAddr(start);
        if (mem.getBlock(a) != null) {
            return;
        }
        MemoryBlock b = mem.createUninitializedBlock(name, a, size, false);
        b.setRead(true);
        b.setWrite(true);
        b.setExecute(false);
        b.setVolatile(volatileFlag);
    }

    private static String text(Element e, String tag) {
        NodeList nl = e.getElementsByTagName(tag);
        for (int i = 0; i < nl.getLength(); i++) {
            if (nl.item(i).getParentNode() == e) {
                return nl.item(i).getTextContent().trim();
            }
        }
        return null;
    }

    private static long num(String s) {
        s = s.trim().toLowerCase();
        return s.startsWith("0x") ? Long.parseLong(s.substring(2), 16) : Long.parseLong(s);
    }

    private Map<Integer, String> loadSvd(File f) throws Exception {
        Document doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(f);
        NodeList periphs = doc.getElementsByTagName("peripheral");
        Map<String, Element> byName = new HashMap<>();
        for (int i = 0; i < periphs.getLength(); i++) {
            Element p = (Element) periphs.item(i);
            byName.put(text(p, "name"), p);
        }
        Map<Integer, String> irqs = new TreeMap<>();
        int regCount = 0;
        for (int i = 0; i < periphs.getLength(); i++) {
            Element p = (Element) periphs.item(i);
            String pname = text(p, "name");
            long base = num(text(p, "baseAddress"));
            Element regSrc = p;
            if (p.hasAttribute("derivedFrom") && text(p, "registers") == null) {
                regSrc = byName.get(p.getAttribute("derivedFrom"));
            }
            NodeList regs = regSrc.getElementsByTagName("register");
            for (int j = 0; j < regs.getLength(); j++) {
                Element r = (Element) regs.item(j);
                String sz = text(r, "size");
                reg(pname + "_" + text(r, "name"), base + num(text(r, "addressOffset")),
                    sz == null ? 32 : (int) num(sz));
                regCount++;
            }
            NodeList ints = p.getElementsByTagName("interrupt");
            for (int j = 0; j < ints.getLength(); j++) {
                Element in = (Element) ints.item(j);
                irqs.put((int) num(text(in, "value")), text(in, "name"));
            }
        }
        println("SVD: " + periphs.getLength() + " peripherals, " + regCount
            + " registers, " + irqs.size() + " interrupts");
        return irqs;
    }

    private void reg(String name, long addr, int bits) throws Exception {
        Address a = toAddr(addr);
        if (mem.getBlock(a) == null) {
            return;
        }
        // Peripheral bit-band alias: one word per bit at 0x42000000 + off*32 + bit*4.
        if (addr >= 0x40000000L && addr < 0x40030000L) {
            long alias = 0x42000000L + (addr - 0x40000000L) * 32;
            for (int bit = 0; bit < bits; bit++) {
                Address ba = toAddr(alias + bit * 4L);
                st.createLabel(ba, name + "_bit" + bit, SourceType.IMPORTED);
                try {
                    createData(ba, UnsignedIntegerDataType.dataType);
                } catch (Exception ignored) {
                    // overlapping register definitions in the SVD
                }
            }
        }
        st.createLabel(a, name, SourceType.IMPORTED);
        DataType dt = bits == 16 ? UnsignedShortDataType.dataType
            : bits == 8 ? ByteDataType.dataType : UnsignedIntegerDataType.dataType;
        try {
            createData(a, dt);
        } catch (Exception ignored) {
            // overlapping register definitions in the SVD
        }
    }

    private void applyVectors(Address base, int count, Map<Integer, String> irqNames)
            throws Exception {
        long imgEnd = currentProgram.getMaxAddress().getOffset();
        st.createLabel(base, "g_pfnVectors", SourceType.USER_DEFINED);
        int made = 0;
        for (int i = 0; i < count; i++) {
            Address slot = base.add(i * 4L);
            String name = i < 16 ? CORE_VECTORS[i] : irqNames.get(i - 16);
            if (name != null && i >= 16) {
                name = name + "_IRQHandler";
            }
            try {
                createData(slot, PointerDataType.dataType);
            } catch (Exception ignored) {
                // already defined
            }
            if (i == 0) {
                continue;
            }
            long v = mem.getInt(slot) & 0xFFFFFFFFL;
            if ((v & 1) == 0 || v < base.getOffset() || v > imgEnd) {
                continue;
            }
            Address target = toAddr(v & ~1L);
            Function fn = getFunctionAt(target);
            if (fn == null) {
                disassemble(target);
                fn = createFunction(target, null);
            }
            if (name == null) {
                name = "Vector_" + i;
            }
            if (fn != null && fn.getName().startsWith("FUN_")) {
                fn.setName(name, SourceType.USER_DEFINED);
            } else {
                st.createLabel(target, name, SourceType.USER_DEFINED);
            }
            made++;
        }
        println("Vectors: " + count + " slots, " + made + " handlers");
    }
}
