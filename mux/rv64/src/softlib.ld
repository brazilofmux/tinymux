/* Linker script for softlib: bare-metal RV64, no libc.
 * Produces a minimal ELF with .text and .rodata at address 0.
 * rv64strip reads the ELF and extracts code + data + symbols.
 *
 * KEEP all .text sections: every function is a potential Tier 2
 * entry point (the DBT looks them up by symbol name).
 * --gc-sections removes only unreachable data (e.g. stub OTT tables).
 */

ENTRY(rv64_cat)

SECTIONS {
    . = 0x10000;
    .text : { KEEP(*(.text*)) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
}
