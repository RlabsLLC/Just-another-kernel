ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    .note.Xen :
    {
        *(.note.Xen)
    } :text

    . = 1M;

    .multiboot :
    {
        KEEP(*(.multiboot))
    } :text

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    . = ALIGN(4096);

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(COMMON)
        *(.bss*)
    } :data

    /DISCARD/ :
    {
        *(.note.GNU-stack)
    }
}
