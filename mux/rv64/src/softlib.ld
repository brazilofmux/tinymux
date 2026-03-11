/* Linker script for softlib: bare-metal RV64, no libc.
 * Produces a minimal ELF with .text and .rodata at address 0.
 * rv64strip reads the ELF and extracts code + data + symbols.
 */

ENTRY(rv64_cat)

SECTIONS {
    . = 0;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
}
