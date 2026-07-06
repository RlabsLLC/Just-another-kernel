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
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .text
.global _start
.type _start, @function
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
