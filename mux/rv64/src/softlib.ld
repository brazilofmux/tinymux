/* Linker script for softlib: bare-metal RV64, no libc.
 * Produces a minimal ELF with .text, .rodata, writable .data, and .bss.
 * rv64strip reads the ELF and extracts code + data + symbols.
 *
 * KEEP all .text sections: every function is a potential Tier 2
 * entry point (the DBT looks them up by symbol name).
 * --gc-sections removes only unreachable data (e.g. stub OTT tables).
 *
 * Layout: read-only sections first, then a single contiguous writable
 * span (.data, initialized) followed by .bss (NOBITS, zero-init).  The
 * loader restores .data and zero-fills .bss before every evaluation,
 * so this grouping keeps the per-eval reset to two contiguous ranges.
 * The __data_start/__bss_start symbols are informational; rv64strip
 * derives the writable/bss ranges from section headers, not symbols.
 */

ENTRY(rv64_cat)

SECTIONS {
    . = 0x10000;
    .text : { KEEP(*(.text*)) }
    .rodata : { *(.rodata*) *(.srodata*) }

    . = ALIGN(8);
    __data_start = .;
    .data : { *(.sdata*) *(.data*) }
    __data_end = .;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.sbss*) *(.bss*) *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
}
