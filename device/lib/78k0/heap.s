; Default 78K0 heap used by the portable allocation routines.

	.globl ___sdcc_heap_init
	.globl ___sdcc_heap
	.globl ___sdcc_heap_end

	.area GSINIT
	call	!___sdcc_heap_init

	.area DATA
___sdcc_heap::
	.ds 511
___sdcc_heap_end::
	.ds 1
