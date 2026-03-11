/* Minimal linker script for RV64IMD test binaries */
ENTRY(_start)

MEMORY
{
    CODE (rx)  : ORIGIN = 0x00010000, LENGTH = 1M
    DATA (rw)  : ORIGIN = 0x00080000, LENGTH = 16M
}

SECTIONS
{
    .text : {
        *(.text.init)
        *(.text .text.*)
    } > CODE

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > CODE

    .data : {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        __data_end = .;
    } > DATA

    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > DATA

    _end = .;
}
