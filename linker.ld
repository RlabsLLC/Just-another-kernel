ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    .multiboot :
    {
        KEEP(*(.multiboot))
    }

    . = 1M;

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        *(COMMON)
        *(.bss*)
    }
}
