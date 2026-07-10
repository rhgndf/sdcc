; 16-bit by 16-bit multiplication with a 32-bit result.

	.globl ___muluint2ulong
	.globl ___mulsint2slong

	.area CODE

___muluint2ulong:
	push	ax
	mov	a,#0x00
	br	!__mulint2long_common

___mulsint2slong:
	push	ax
	mov	a,#0x01

__mulint2long_common:
	mov	c,a
	push	de
	movw	ax,sp
	subw	ax,#0x000b
	movw	sp,ax
	movw	hl,ax

	; Multiplicand, multiplier, result, and sign flag.
	mov	a,[hl+0x0d]
	mov	[hl+0x00],a
	mov	a,[hl+0x0e]
	mov	[hl+0x01],a
	mov	a,#0x00
	mov	[hl+0x02],a
	mov	[hl+0x03],a
	mov	a,[hl+0x11]
	mov	[hl+0x04],a
	mov	a,[hl+0x12]
	mov	[hl+0x05],a
	mov	a,#0x00
	mov	[hl+0x06],a
	mov	[hl+0x07],a
	mov	[hl+0x08],a
	mov	[hl+0x09],a
	mov	[hl+0x0a],a

	; Convert signed operands to magnitudes and remember the result sign.
	mov	a,c
	cmp	a,#0x00
	bz	00004$
	mov	a,[hl+0x01]
	and	a,#0x80
	bz	00002$
	mov	a,#0x00
	sub	a,[hl+0x00]
	mov	[hl+0x00],a
	mov	a,#0x00
	subc	a,[hl+0x01]
	mov	[hl+0x01],a
	mov	a,#0x01
	mov	[hl+0x0a],a
00002$:
	mov	a,[hl+0x05]
	and	a,#0x80
	bz	00004$
	mov	a,#0x00
	sub	a,[hl+0x04]
	mov	[hl+0x04],a
	mov	a,#0x00
	subc	a,[hl+0x05]
	mov	[hl+0x05],a
	mov	a,[hl+0x0a]
	xor	a,#0x01
	mov	[hl+0x0a],a

00004$:
	mov	a,#0x10
	mov	b,a
00005$:
	mov	a,[hl+0x04]
	and	a,#0x01
	bz	00006$
	mov	a,[hl+0x06]
	add	a,[hl+0x00]
	mov	[hl+0x06],a
	mov	a,[hl+0x07]
	addc	a,[hl+0x01]
	mov	[hl+0x07],a
	mov	a,[hl+0x08]
	addc	a,[hl+0x02]
	mov	[hl+0x08],a
	mov	a,[hl+0x09]
	addc	a,[hl+0x03]
	mov	[hl+0x09],a
00006$:
	clr1	cy
	mov	a,[hl+0x05]
	rorc	a,1
	mov	[hl+0x05],a
	mov	a,[hl+0x04]
	rorc	a,1
	mov	[hl+0x04],a
	clr1	cy
	mov	a,[hl+0x00]
	rolc	a,1
	mov	[hl+0x00],a
	mov	a,[hl+0x01]
	rolc	a,1
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	rolc	a,1
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	rolc	a,1
	mov	[hl+0x03],a
	dbnz	b,00005$

	mov	a,[hl+0x0a]
	cmp	a,#0x00
	bz	00007$
	mov	a,#0x00
	sub	a,[hl+0x06]
	mov	[hl+0x06],a
	mov	a,#0x00
	subc	a,[hl+0x07]
	mov	[hl+0x07],a
	mov	a,#0x00
	subc	a,[hl+0x08]
	mov	[hl+0x08],a
	mov	a,#0x00
	subc	a,[hl+0x09]
	mov	[hl+0x09],a
00007$:

	; Restore DE and move the return address over the stack argument.
	mov	a,[hl+0x0b]
	mov	x,a
	mov	a,[hl+0x0c]
	movw	de,ax
	mov	a,[hl+0x0f]
	mov	[hl+0x11],a
	mov	a,[hl+0x10]
	mov	[hl+0x12],a
	movw	ax,sp
	addw	ax,#0x0011
	movw	sp,ax

	; Return the product in BC:AX.
	mov	a,[hl+0x08]
	mov	c,a
	mov	a,[hl+0x09]
	mov	b,a
	mov	a,[hl+0x06]
	mov	x,a
	mov	a,[hl+0x07]
	ret
