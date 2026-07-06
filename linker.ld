ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    .note.Xen :
    {
        *(.note.Xen)
    }

    . = 1M;

    .multiboot :
    {
        KEEP(*(.multiboot))
    }

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
