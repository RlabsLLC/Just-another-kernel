ENTRY(_start)

SECTIONS
{
    . = 1M;

    .note.Xen :
    {
        *(.note.Xen)
    }

    .multiboot :
    {
        *(.multiboot)
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
