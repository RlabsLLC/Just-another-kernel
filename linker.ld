ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R | PF_X */
    data PT_LOAD FLAGS(6); /* PF_R | PF_W */
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

    . = ALIGN(4096); /* Separate RX and RW PT_LOAD segments on a page boundary. */

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
