/* PVH ELF note - required by QEMU 5+ to boot an uncompressed ELF kernel
 * directly via the -kernel flag (XEN_ELFNOTE_PHYS32_ENTRY = 18).
 */
.section .note.Xen, "a"
.align 4
.long 4         /* namesz: length of "Xen\0" */
.long 4         /* descsz: 32-bit physical entry address */
.long 18        /* type: XEN_ELFNOTE_PHYS32_ENTRY */
.ascii "Xen\0"  /* name */
.long _start    /* desc: protected-mode 32-bit entry point */

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.set MB2_MAGIC,   0xE85250D6
.set MB2_ARCH,    0
.set MB2_LEN,     mb2_header_end - mb2_header_start
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

.section .multiboot,"a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0           /* mode_type: linear graphics */
.long 1024        /* width */
.long 768         /* height */
.long 32          /* depth */

.section .multiboot2,"a"
.align 8
mb2_header_start:
.long MB2_MAGIC
.long MB2_ARCH
.long MB2_LEN
.long MB2_CHECKSUM

/* Framebuffer request tag */
.short 5
.short 0
.long 20
.long 1024
.long 768
.long 32

/* Multiboot2 tags must be 8-byte aligned. */
.align 8

/* End tag */
.short 0
.short 0
.long 8
mb2_header_end:

.section .text
.global _start
.type _start, %function
_start:
    cli
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kernel_main

.hang:
    hlt
    jmp .hang

.size _start, . - _start

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:
