	.title	78K0 Assembler Encoding Test

	.area	CODE

	; 8-bit data transfer
	mov	b,#0xff	; EXPECT: A3 FF
	mov	0xff1f,#0x23	; EXPECT: 11 1F 23
	mov	0xff00,#0x34	; EXPECT: 11 00 34
	mov	0xff20,#0x34	; EXPECT: 13 20 34
	mov	a,b	; EXPECT: 63
	mov	b,a	; EXPECT: 73
	mov	a,0xfe20	; EXPECT: F0 20
	mov	0xff1f,a	; EXPECT: F2 1F
	mov	a,0xffcf	; EXPECT: F4 CF
	mov	0xffe0,a	; EXPECT: F6 E0
	mov	a,!0xffff	; EXPECT: 8E FF FF
	mov	!0x0000,a	; EXPECT: 9E 00 00
	mov	psw,#0x80	; EXPECT: 11 1E 80
	mov	a,psw	; EXPECT: F0 1E
	mov	psw,a	; EXPECT: F2 1E
	mov	a,[de]	; EXPECT: 85
	mov	[de],a	; EXPECT: 95
	mov	a,[hl]	; EXPECT: 87
	mov	[hl],a	; EXPECT: 97
	mov	a,[hl+0xff]	; EXPECT: AE FF
	mov	[hl+0xff],a	; EXPECT: BE FF
	mov	a,[hl+b]	; EXPECT: AB
	mov	[hl+b],a	; EXPECT: BB
	mov	a,[hl+c]	; EXPECT: AA
	mov	[hl+c],a	; EXPECT: BA

	; Direct-address class boundaries (saddr takes the overlap).
	mov	a,0xfe20	; EXPECT: F0 20
	mov	a,0xff1f	; EXPECT: F0 1F
	mov	a,0xff20	; EXPECT: F4 20
	mov	a,0xffcf	; EXPECT: F4 CF
	mov	a,0xffe0	; EXPECT: F4 E0
	mov	a,0xffff	; EXPECT: F4 FF
	mov	a,@0xff00	; EXPECT: F4 00

	xch	a,b	; EXPECT: 33
	xch	a,0xfe20	; EXPECT: 83 20
	xch	a,0xff80	; EXPECT: 93 80
	xch	a,!0x1234	; EXPECT: CE 34 12
	xch	a,[de]	; EXPECT: 05
	xch	a,[hl]	; EXPECT: 07
	xch	a,[hl+0x12]	; EXPECT: DE 12
	xch	a,[hl+b]	; EXPECT: 31 8B
	xch	a,[hl+c]	; EXPECT: 31 8A

	; 16-bit data transfer
	movw	bc,#0x2345	; EXPECT: 12 45 23
	movw	0xfe20,#0x3456	; EXPECT: EE 20 56 34
	movw	0xffe0,#0x4567	; EXPECT: FE E0 67 45
	movw	ax,0xfe20	; EXPECT: 89 20
	movw	0xff1e,ax	; EXPECT: 99 1E
	movw	ax,0xff00	; EXPECT: 89 00
	movw	0xffe0,ax	; EXPECT: B9 E0
	movw	ax,bc	; EXPECT: C2
	movw	bc,ax	; EXPECT: D2
	movw	ax,!0xfffe	; EXPECT: 02 FE FF
	movw	!0x0000,ax	; EXPECT: 03 00 00
	not1	cy	; EXPECT: 01
	set1	cy	; EXPECT: 20
	clr1	cy	; EXPECT: 21
	movw	ax,#0x1234	; EXPECT: 10 34 12
	movw	sp,#0xabcd	; EXPECT: EE 1C CD AB
	movw	sp,ax	; EXPECT: B9 1C
	movw	ax,sp	; EXPECT: A9 1C
	xchw	ax,bc	; EXPECT: E2

	; Even word-address boundaries.
	movw	ax,0xfe20	; EXPECT: 89 20
	movw	ax,0xff1e	; EXPECT: 89 1E
	movw	ax,0xff20	; EXPECT: A9 20
	movw	ax,0xffce	; EXPECT: A9 CE
	movw	ax,0xfffe	; EXPECT: A9 FE
	movw	ax,@0xff00	; EXPECT: A9 00

	; 8-bit arithmetic and logical operations
	mov	a,#0x56	; EXPECT: A1 56
	add	a,#0x12	; EXPECT: 0D 12
	add	0xfe20,#0x12	; EXPECT: 88 20 12
	add	a,b	; EXPECT: 61 0B
	add	b,a	; EXPECT: 61 03
	add	a,0xfe20	; EXPECT: 0E 20
	add	a,!0x1234	; EXPECT: 08 34 12
	add	a,[hl]	; EXPECT: 0F
	add	a,[hl+0x12]	; EXPECT: 09 12
	add	a,[hl+b]	; EXPECT: 31 0B
	add	a,[hl+c]	; EXPECT: 31 0A

	addc	a,#0x12	; EXPECT: 2D 12
	addc	0xfe20,#0x12	; EXPECT: A8 20 12
	addc	a,b	; EXPECT: 61 2B
	addc	b,a	; EXPECT: 61 23
	addc	a,0xfe20	; EXPECT: 2E 20
	addc	a,!0x1234	; EXPECT: 28 34 12
	addc	a,[hl]	; EXPECT: 2F
	addc	a,[hl+0x12]	; EXPECT: 29 12
	addc	a,[hl+b]	; EXPECT: 31 2B
	addc	a,[hl+c]	; EXPECT: 31 2A

	sub	a,#0x12	; EXPECT: 1D 12
	sub	0xfe20,#0x12	; EXPECT: 98 20 12
	sub	a,b	; EXPECT: 61 1B
	sub	b,a	; EXPECT: 61 13
	sub	a,0xfe20	; EXPECT: 1E 20
	sub	a,!0x1234	; EXPECT: 18 34 12
	sub	a,[hl]	; EXPECT: 1F
	sub	a,[hl+0x12]	; EXPECT: 19 12
	sub	a,[hl+b]	; EXPECT: 31 1B
	sub	a,[hl+c]	; EXPECT: 31 1A

	subc	a,#0x12	; EXPECT: 3D 12
	subc	0xfe20,#0x12	; EXPECT: B8 20 12
	subc	a,b	; EXPECT: 61 3B
	subc	b,a	; EXPECT: 61 33
	subc	a,0xfe20	; EXPECT: 3E 20
	subc	a,!0x1234	; EXPECT: 38 34 12
	subc	a,[hl]	; EXPECT: 3F
	subc	a,[hl+0x12]	; EXPECT: 39 12
	subc	a,[hl+b]	; EXPECT: 31 3B
	subc	a,[hl+c]	; EXPECT: 31 3A

	and	a,#0x12	; EXPECT: 5D 12
	and	0xfe20,#0x12	; EXPECT: D8 20 12
	and	a,b	; EXPECT: 61 5B
	and	b,a	; EXPECT: 61 53
	and	a,0xfe20	; EXPECT: 5E 20
	and	a,!0x1234	; EXPECT: 58 34 12
	and	a,[hl]	; EXPECT: 5F
	and	a,[hl+0x12]	; EXPECT: 59 12
	and	a,[hl+b]	; EXPECT: 31 5B
	and	a,[hl+c]	; EXPECT: 31 5A

	or	a,#0x12	; EXPECT: 6D 12
	or	0xfe20,#0x12	; EXPECT: E8 20 12
	or	a,b	; EXPECT: 61 6B
	or	b,a	; EXPECT: 61 63
	or	a,0xfe20	; EXPECT: 6E 20
	or	a,!0x1234	; EXPECT: 68 34 12
	or	a,[hl]	; EXPECT: 6F
	or	a,[hl+0x12]	; EXPECT: 69 12
	or	a,[hl+b]	; EXPECT: 31 6B
	or	a,[hl+c]	; EXPECT: 31 6A

	xor	a,#0x12	; EXPECT: 7D 12
	xor	0xfe20,#0x12	; EXPECT: F8 20 12
	xor	a,b	; EXPECT: 61 7B
	xor	b,a	; EXPECT: 61 73
	xor	a,0xfe20	; EXPECT: 7E 20
	xor	a,!0x1234	; EXPECT: 78 34 12
	xor	a,[hl]	; EXPECT: 7F
	xor	a,[hl+0x12]	; EXPECT: 79 12
	xor	a,[hl+b]	; EXPECT: 31 7B
	xor	a,[hl+c]	; EXPECT: 31 7A

	cmp	a,#0x12	; EXPECT: 4D 12
	cmp	0xfe20,#0x12	; EXPECT: C8 20 12
	cmp	a,b	; EXPECT: 61 4B
	cmp	b,a	; EXPECT: 61 43
	cmp	a,0xfe20	; EXPECT: 4E 20
	cmp	a,!0x1234	; EXPECT: 48 34 12
	cmp	a,[hl]	; EXPECT: 4F
	cmp	a,[hl+0x12]	; EXPECT: 49 12
	cmp	a,[hl+b]	; EXPECT: 31 4B
	cmp	a,[hl+c]	; EXPECT: 31 4A

	; 16-bit arithmetic, multiply, divide, increment, and rotate
	addw	ax,#0x1234	; EXPECT: CA 34 12
	subw	ax,#0x1234	; EXPECT: DA 34 12
	cmpw	ax,#0x1234	; EXPECT: EA 34 12
	mulu	x	; EXPECT: 31 88
	divuw	c	; EXPECT: 31 82
	inc	a	; EXPECT: 41
	inc	0xfe20	; EXPECT: 81 20
	dec	a	; EXPECT: 51
	dec	0xfe20	; EXPECT: 91 20
	incw	bc	; EXPECT: 82
	decw	bc	; EXPECT: 92
	ror	a,1	; EXPECT: 24
	rol	a,1	; EXPECT: 26
	rorc	a,1	; EXPECT: 25
	rolc	a,1	; EXPECT: 27
	ror4	[hl]	; EXPECT: 31 90
	rol4	[hl]	; EXPECT: 31 80
	adjba	; EXPECT: 61 80
	adjbs	; EXPECT: 61 90

	; Bit operations: saddr, sfr, A, PSW, and [HL] forms
	mov1	cy,0xfe20.1	; EXPECT: 71 14 20
	mov1	cy,0xff80.2	; EXPECT: 71 2C 80
	mov1	cy,a.3	; EXPECT: 61 BC
	mov1	cy,psw.4	; EXPECT: 71 44 1E
	mov1	cy,[hl].5	; EXPECT: 71 D4
	mov1	0xfe20.1,cy	; EXPECT: 71 11 20
	mov1	0xff80.2,cy	; EXPECT: 71 29 80
	mov1	a.3,cy	; EXPECT: 61 B9
	mov1	psw.4,cy	; EXPECT: 71 41 1E
	mov1	[hl].5,cy	; EXPECT: 71 D1
	and1	cy,0xfe20.1	; EXPECT: 71 15 20
	and1	cy,0xff80.2	; EXPECT: 71 2D 80
	and1	cy,a.3	; EXPECT: 61 BD
	and1	cy,psw.4	; EXPECT: 71 45 1E
	and1	cy,[hl].5	; EXPECT: 71 D5
	or1	cy,0xfe20.1	; EXPECT: 71 16 20
	or1	cy,0xff80.2	; EXPECT: 71 2E 80
	or1	cy,a.3	; EXPECT: 61 BE
	or1	cy,psw.4	; EXPECT: 71 46 1E
	or1	cy,[hl].5	; EXPECT: 71 D6
	xor1	cy,0xfe20.1	; EXPECT: 71 17 20
	xor1	cy,0xff80.2	; EXPECT: 71 2F 80
	xor1	cy,a.3	; EXPECT: 61 BF
	xor1	cy,psw.4	; EXPECT: 71 47 1E
	xor1	cy,[hl].5	; EXPECT: 71 D7
	set1	0xfe20.1	; EXPECT: 1A 20
	set1	0xff80.2	; EXPECT: 71 2A 80
	set1	@0xff00.0	; EXPECT: 71 0A 00
	set1	a.3	; EXPECT: 61 BA
	set1	psw.4	; EXPECT: 4A 1E
	set1	[hl].5	; EXPECT: 71 D2
	clr1	0xfe20.1	; EXPECT: 1B 20
	clr1	0xff80.2	; EXPECT: 71 2B 80
	clr1	a.3	; EXPECT: 61 BB
	clr1	psw.4	; EXPECT: 4B 1E
	clr1	[hl].5	; EXPECT: 71 D3

	; Calls, returns, stack operations, and branches
	call	!0x3456	; EXPECT: 9A 56 34
	callf	!0x0800	; EXPECT: 0C 00
	callt	[0x40]	; EXPECT: C1
	br	ax	; EXPECT: 31 98
	br	!0x3456	; EXPECT: 9B 56 34
	br	pdf_branch_target	; EXPECT: FA 43
	bc	pdf_branch_target	; EXPECT: 8D 41
	bnc	pdf_branch_target	; EXPECT: 9D 3F
	bz	pdf_branch_target	; EXPECT: AD 3D
	bnz	pdf_branch_target	; EXPECT: BD 3B
	bt	0xfe20.1,pdf_branch_target	; EXPECT: 9C 20 38
	bt	0xff80.2,pdf_branch_target	; EXPECT: 31 26 80 34
	bt	a.3,pdf_branch_target	; EXPECT: 31 3E 31
	bt	psw.4,pdf_branch_target	; EXPECT: CC 1E 2E
	bt	[hl].5,pdf_branch_target	; EXPECT: 31 D6 2B
	bf	0xfe20.1,pdf_branch_target	; EXPECT: 31 13 20 27
	bf	0xff80.2,pdf_branch_target	; EXPECT: 31 27 80 23
	bf	a.3,pdf_branch_target	; EXPECT: 31 3F 20
	bf	psw.4,pdf_branch_target	; EXPECT: 31 43 1E 1C
	bf	[hl].5,pdf_branch_target	; EXPECT: 31 D7 19
	btclr	0xfe20.1,pdf_branch_target	; EXPECT: 31 11 20 15
	btclr	0xff80.2,pdf_branch_target	; EXPECT: 31 25 80 11
	btclr	a.3,pdf_branch_target	; EXPECT: 31 3D 0E
	btclr	psw.4,pdf_branch_target	; EXPECT: 31 41 1E 0A
	btclr	[hl].5,pdf_branch_target	; EXPECT: 31 D5 07
	dbnz	b,pdf_branch_target	; EXPECT: 8B 05
	dbnz	c,pdf_branch_target	; EXPECT: 8A 03
	dbnz	0xfe20,pdf_branch_target	; EXPECT: 04 20 00
pdf_branch_target:
	push	psw	; EXPECT: 22
	push	bc	; EXPECT: B3
	pop	psw	; EXPECT: 23
	pop	bc	; EXPECT: B2
	sel	rb2	; EXPECT: 61 F0
	brk	; EXPECT: BF
	ret	; EXPECT: AF
	reti	; EXPECT: 8F
	retb	; EXPECT: 9F
	nop	; EXPECT: 00
	ei	; EXPECT: 7A 1E
	di	; EXPECT: 7B 1E
	halt	; EXPECT: 71 10
	stop	; EXPECT: 71 00

	; Stable PC-relative byte checks for the A.bit branch opcode class.
	.area	FIXED (ABS)
	.org	0x1000
	bt	a.3,pdf_bt_a_done	; EXPECT: 31 3E 00
pdf_bt_a_done:
	bf	a.3,pdf_bf_a_done	; EXPECT: 31 3F 00
pdf_bf_a_done:
	btclr	a.3,pdf_btclr_a_done	; EXPECT: 31 3D 00
pdf_btclr_a_done:
