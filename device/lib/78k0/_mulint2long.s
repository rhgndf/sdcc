; 16-bit by 16-bit multiplication with a 32-bit result.

	.globl ___muluint2ulong
	.globl ___mulsint2slong

	.area CODE

___muluint2ulong:
	push	ax
	mov	a,#0x00
	br	__mulint2long_common

___mulsint2slong:
	push	ax
	mov	a,#0x01

__mulint2long_common:
	mov	c,a
	push	de
	movw	ax,sp
	subw	ax,#0x0004
	movw	sp,ax
	movw	hl,ax

	; a0*b0 supplies the low word.
	mov	a,[hl+0x06]
	mov	x,a
	mov	a,[hl+0x0a]
	mulu	x
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a
	mov	a,#0x00
	mov	[hl+0x02],a
	mov	[hl+0x03],a

	; Add a0*b1 and a1*b0, each shifted by one byte.
	mov	a,[hl+0x06]
	mov	x,a
	mov	a,[hl+0x0b]
	mulu	x
	mov	e,a
	mov	a,x
	add	a,[hl+0x01]
	mov	[hl+0x01],a
	mov	a,e
	addc	a,[hl+0x02]
	mov	[hl+0x02],a
	mov	a,#0x00
	addc	a,[hl+0x03]
	mov	[hl+0x03],a

	mov	a,[hl+0x07]
	mov	x,a
	mov	a,[hl+0x0a]
	mulu	x
	mov	e,a
	mov	a,x
	add	a,[hl+0x01]
	mov	[hl+0x01],a
	mov	a,e
	addc	a,[hl+0x02]
	mov	[hl+0x02],a
	mov	a,#0x00
	addc	a,[hl+0x03]
	mov	[hl+0x03],a

	; The high-byte product is shifted by two bytes.
	mov	a,[hl+0x07]
	mov	x,a
	mov	a,[hl+0x0b]
	mulu	x
	mov	e,a
	mov	a,x
	add	a,[hl+0x02]
	mov	[hl+0x02],a
	mov	a,e
	addc	a,[hl+0x03]
	mov	[hl+0x03],a

	; Signed 16-bit operands are their unsigned bit patterns minus 2^16.
	mov	a,c
	cmp	a,#0x00
	bz	00003$
	mov	a,[hl+0x07]
	and	a,#0x80
	bz	00001$
	mov	a,[hl+0x02]
	sub	a,[hl+0x0a]
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	subc	a,[hl+0x0b]
	mov	[hl+0x03],a
00001$:
	mov	a,[hl+0x0b]
	and	a,#0x80
	bz	00003$
	mov	a,[hl+0x02]
	sub	a,[hl+0x06]
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	subc	a,[hl+0x07]
	mov	[hl+0x03],a
00003$:

	; Restore DE and move the return address over the stack argument.
	mov	a,[hl+0x04]
	mov	x,a
	mov	a,[hl+0x05]
	movw	de,ax
	mov	a,[hl+0x08]
	mov	[hl+0x0a],a
	mov	a,[hl+0x09]
	mov	[hl+0x0b],a
	movw	ax,sp
	addw	ax,#0x000a
	movw	sp,ax

	; Return the product in BC:AX.
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	ret
