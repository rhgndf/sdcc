	.title	78K0 Assembler Encoding Test

	.area	CODE

	; 8-bit data transfer
	mov	b,#0x12
	mov	0xfe20,#0x23
	mov	0xff80,#0x34
	mov	a,b
	mov	b,a
	mov	a,0xfe20
	mov	0xfe20,a
	mov	a,0xff80
	mov	0xff80,a
	mov	a,!0x1234
	mov	!0x1234,a
	mov	psw,#0x80	; EXPECT: 11 1E 80
	mov	a,psw		; EXPECT: F0 1E
	mov	psw,a		; EXPECT: F2 1E
	mov	a,[de]
	mov	[de],a
	mov	a,[hl]
	mov	[hl],a
	mov	a,[hl+0x12]
	mov	[hl+0x12],a
	mov	a,[hl+b]
	mov	[hl+b],a
	mov	a,[hl+c]
	mov	[hl+c],a

	xch	a,b
	xch	a,0xfe20
	xch	a,0xff80
	xch	a,!0x1234
	xch	a,[de]
	xch	a,[hl]
	xch	a,[hl+0x12]
	xch	a,[hl+b]
	xch	a,[hl+c]

	; 16-bit data transfer
	movw	bc,#0x2345
	movw	0xfe20,#0x3456
	movw	0xff80,#0x4567
	movw	ax,0xfe20
	movw	0xfe20,ax
	movw	ax,0xff80
	movw	0xff80,ax
	movw	ax,bc
	movw	bc,ax
	movw	ax,!0x1234
	movw	!0x1234,ax
	not1	cy		; EXPECT: 01
	set1	cy		; EXPECT: 20
	clr1	cy		; EXPECT: 21
	movw	ax,#0x1234	; EXPECT: 10 34 12
	movw	sp,#0xabcd	; EXPECT: EE 1C CD AB
	movw	sp,ax
	movw	ax,sp
	xchw	ax,bc

	; 8-bit arithmetic and logical operations
	mov	a,#0x56	; EXPECT: A1 56
	add	a,#0x12	; EXPECT: 0D 12
	add	0xfe20,#0x12
	add	a,b
	add	b,a
	add	a,0xfe20
	add	a,!0x1234
	add	a,[hl]
	add	a,[hl+0x12]
	add	a,[hl+b]
	add	a,[hl+c]

	addc	a,#0x12
	addc	0xfe20,#0x12
	addc	a,b
	addc	b,a
	addc	a,0xfe20
	addc	a,!0x1234
	addc	a,[hl]
	addc	a,[hl+0x12]
	addc	a,[hl+b]
	addc	a,[hl+c]

	sub	a,#0x12
	sub	0xfe20,#0x12
	sub	a,b
	sub	b,a
	sub	a,0xfe20
	sub	a,!0x1234
	sub	a,[hl]
	sub	a,[hl+0x12]
	sub	a,[hl+b]
	sub	a,[hl+c]

	subc	a,#0x12
	subc	0xfe20,#0x12
	subc	a,b
	subc	b,a
	subc	a,0xfe20
	subc	a,!0x1234
	subc	a,[hl]
	subc	a,[hl+0x12]
	subc	a,[hl+b]
	subc	a,[hl+c]

	and	a,#0x12
	and	0xfe20,#0x12
	and	a,b
	and	b,a
	and	a,0xfe20
	and	a,!0x1234
	and	a,[hl]
	and	a,[hl+0x12]
	and	a,[hl+b]
	and	a,[hl+c]

	or	a,#0x12
	or	0xfe20,#0x12
	or	a,b
	or	b,a
	or	a,0xfe20
	or	a,!0x1234
	or	a,[hl]
	or	a,[hl+0x12]
	or	a,[hl+b]
	or	a,[hl+c]

	xor	a,#0x12
	xor	0xfe20,#0x12
	xor	a,b
	xor	b,a
	xor	a,0xfe20
	xor	a,!0x1234
	xor	a,[hl]
	xor	a,[hl+0x12]
	xor	a,[hl+b]
	xor	a,[hl+c]

	cmp	a,#0x12
	cmp	0xfe20,#0x12
	cmp	a,b
	cmp	b,a
	cmp	a,0xfe20
	cmp	a,!0x1234
	cmp	a,[hl]
	cmp	a,[hl+0x12]
	cmp	a,[hl+b]
	cmp	a,[hl+c]

	; 16-bit arithmetic, multiply, divide, increment, and rotate
	addw	ax,#0x1234
	subw	ax,#0x1234
	cmpw	ax,#0x1234
	mulu	x
	divuw	c
	inc	a
	inc	0xfe20
	dec	a
	dec	0xfe20
	incw	bc
	decw	bc
	ror	a,1
	rol	a,1
	rorc	a,1
	rolc	a,1
	ror4	[hl]
	rol4	[hl]
	adjba
	adjbs

	; Bit operations: saddr, sfr, A, PSW, and [HL] forms
	mov1	cy,0xfe20.1
	mov1	cy,0xff80.2
	mov1	cy,a.3
	mov1	cy,psw.4
	mov1	cy,[hl].5
	mov1	0xfe20.1,cy
	mov1	0xff80.2,cy
	mov1	a.3,cy
	mov1	psw.4,cy
	mov1	[hl].5,cy
	and1	cy,0xfe20.1
	and1	cy,0xff80.2
	and1	cy,a.3
	and1	cy,psw.4
	and1	cy,[hl].5
	or1	cy,0xfe20.1
	or1	cy,0xff80.2
	or1	cy,a.3
	or1	cy,psw.4
	or1	cy,[hl].5
	xor1	cy,0xfe20.1
	xor1	cy,0xff80.2
	xor1	cy,a.3
	xor1	cy,psw.4
	xor1	cy,[hl].5
	set1	0xfe20.1
	set1	0xff80.2
	set1	a.3
	set1	psw.4
	set1	[hl].5
	clr1	0xfe20.1
	clr1	0xff80.2
	clr1	a.3
	clr1	psw.4
	clr1	[hl].5

	; Calls, returns, stack operations, and branches
	call	!0x3456		; EXPECT: 9A 56 34
	callf	!0x0800
	callt	[0x40]
	br	ax		; EXPECT: 31 98
	br	!0x3456
	br	pdf_branch_target
	bc	pdf_branch_target
	bnc	pdf_branch_target
	bz	pdf_branch_target
	bnz	pdf_branch_target
	bt	0xfe20.1,pdf_branch_target
	bt	0xff80.2,pdf_branch_target
	bt	a.3,pdf_branch_target
	bt	psw.4,pdf_branch_target
	bt	[hl].5,pdf_branch_target
	bf	0xfe20.1,pdf_branch_target
	bf	0xff80.2,pdf_branch_target
	bf	a.3,pdf_branch_target
	bf	psw.4,pdf_branch_target
	bf	[hl].5,pdf_branch_target
	btclr	0xfe20.1,pdf_branch_target
	btclr	0xff80.2,pdf_branch_target
	btclr	a.3,pdf_branch_target
	btclr	psw.4,pdf_branch_target
	btclr	[hl].5,pdf_branch_target
	dbnz	b,pdf_branch_target
	dbnz	c,pdf_branch_target
	dbnz	0xfe20,pdf_branch_target
pdf_branch_target:
	push	psw
	push	bc
	pop	psw
	pop	bc
	sel	rb2
	brk
	ret			; EXPECT: AF
	reti
	retb
	nop
	ei
	di
	halt
	stop

	; Stable PC-relative byte checks for the A.bit branch opcode class.
	.area	FIXED (ABS)
	.org	0x1000
	bt	a.3,pdf_bt_a_done		; EXPECT: 31 3E 00
pdf_bt_a_done:
	bf	a.3,pdf_bf_a_done		; EXPECT: 31 3F 00
pdf_bf_a_done:
	btclr	a.3,pdf_btclr_a_done	; EXPECT: 31 3D 00
pdf_btclr_a_done:
