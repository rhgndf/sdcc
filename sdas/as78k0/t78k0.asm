	.title	78K0 Assembler Encoding Test

	.area	CODE

	mov	psw,#0x80	; EXPECT: 11 1E 80
	mov	a,psw		; EXPECT: F0 1E
	mov	psw,a		; EXPECT: F2 1E
	not1	cy		; EXPECT: 01
	set1	cy		; EXPECT: 20
	clr1	cy		; EXPECT: 21
	movw	ax,#0x1234	; EXPECT: 10 34 12
	movw	sp,#0xabcd	; EXPECT: EE 1C CD AB
	mov	a,#0x56	; EXPECT: A1 56
	add	a,#0x12	; EXPECT: 0D 12
	call	!0x3456		; EXPECT: 9A 56 34
	br	ax		; EXPECT: 31 98
	ret			; EXPECT: AF
