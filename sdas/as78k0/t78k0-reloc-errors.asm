	.title	78K0 Relocation Rejection Test
	.module	reloc_errors

	; LINK-ERROR: 78K0 short-address relocation error
	; LINK-ERROR: 78K0 SFR relocation error
	; LINK-ERROR: 78K0 word-address alignment error

	.area	CODE
	.globl	not_saddr
	.globl	not_sfr
	.globl	odd_saddr
	.globl	odd_sfr

	mov	a,not_saddr
	mov	a,@not_sfr
	movw	ax,odd_saddr
	movw	ax,@odd_sfr
