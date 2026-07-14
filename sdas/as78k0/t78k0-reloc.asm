	.title	78K0 Relocation Test
	.module	reloc_use

	.area	CODE
	.globl	saddr_byte
	.globl	saddr_word
	.globl	addr16_byte
	.globl	addr16_word
	.globl	byte_value
	.globl	word_value

	mov	a,saddr_byte
	movw	ax,saddr_word
	call	!addr16_byte
	movw	ax,!addr16_word
	mov	a,#byte_value
	mov	a,#<word_value
	mov	a,#>word_value
	mov	a,[hl+byte_value]
