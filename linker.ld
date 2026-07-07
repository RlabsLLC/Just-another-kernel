ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    .note.Xen :
    {
        *(.note.Xen)
    }

    .multiboot :
    {
        KEEP(*(.multiboot))
    }

    .multiboot2 :
    {
        KEEP(*(.multiboot2))
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
