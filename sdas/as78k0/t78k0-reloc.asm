	.title	78K0 Relocation Test
	.module	reloc_use

	.area	CODE
	.globl	addr16_byte
	.globl	byte_value
	.globl	word_value

	call	!addr16_byte
	mov	a,byte_value
	mov	a,#byte_value
	mov	a,#<word_value
	mov	a,#>word_value
	mov	a,[hl+byte_value]
	movw	ax,!word_value
