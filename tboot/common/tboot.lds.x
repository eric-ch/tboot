/* ld script to make i386 Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 * Modified for i386 Xen by Keir Fraser
 * Modified for tboot by Joseph Cihula
 */

#include <config.h>

#undef ENTRY
#undef ALIGN

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(start)
PHDRS
{
  text PT_LOAD ;
}
SECTIONS
{
  . = TBOOT_BASE_ADDR;		/* 0x70000 */

  _stext = .;	                /* text */
  _mle_start = .;               /* beginning of MLE pages */

  .text : {
	*(.text)
	*(.fixup)
	*(.gnu.warning)
	} :text =0x9090

  _etext = .;			/* end of text section */

  .rodata : { *(.rodata) *(.rodata.*) }
  . = ALIGN(4096);

  _mle_end = .;                 /* end of MLE pages */

  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(4096);

  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss.stack_aligned)
	*(.bss.page_aligned)
	*(.bss)
	*(.tboot_shared)
	}

  _end = . ;
}
