	.title	78K0 Assembler Rejection Test

	.area	CODE
	.globl	external_address

	; Low-byte aliases and values outside the direct-address classes.
	mov	a,0x00		; ERROR: direct address is neither an saddr nor an SFR
	mov	a,0xffd0		; ERROR: direct address is neither an saddr nor an SFR
	mov	a,0xffdf		; ERROR: direct address is neither an saddr nor an SFR
	mov	a,0x10000	; ERROR: direct address is neither an saddr nor an SFR
	mov	a,!0x10000	; ERROR: out of range signed / unsigned value
	call	!0x10000	; ERROR: out of range signed / unsigned value
	mov	a,0x1fe20	; ERROR: out of range signed / unsigned value
	movw	ax,0x1fe20	; ERROR: out of range signed / unsigned value

	mov	a,<external_address ; ERROR: Byte selection is not valid for a 78K0 direct address
	mov	a,external_address ; ERROR: Relocatable direct addresses require explicit addr16 syntax
	movw	ax,!external_address ; ERROR: Relocatable word addresses cannot be checked for even alignment

	; A forced addr16 prefix is not part of any 78K0 bit-address grammar.
	set1	!0xff80.0	; ERROR: Forced addr16 syntax is not valid for a 78K0 bit operand
	set1	0x1fe20.0	; ERROR: out of range signed / unsigned value
	set1	0xfe20.0x10000	; ERROR: out of range signed / unsigned value

	; MOVW direct operands require an even resolved address.
	movw	ax,0xfe21	; ERROR: 78K0 word address must be even
	movw	ax,0xffe1	; ERROR: 78K0 word address must be even

	; Byte immediates and [HL+disp] are unsigned bytes.
	mov	a,#0x100	; ERROR: 78K0 byte operand is outside 0x00..0xff
	mov	a,#-1		; ERROR: 78K0 byte operand is outside 0x00..0xff
	mov	a,#0x10000	; ERROR: out of range signed / unsigned value
	mov	a,[hl+0x100]	; ERROR: 78K0 byte operand is outside 0x00..0xff
	mov	a,[hl+-1]	; ERROR: 78K0 byte operand is outside 0x00..0xff
	mov	a,[hl+0x10000]	; ERROR: out of range signed / unsigned value

	; Word immediates and CALLT addresses must not wrap to valid low bits.
	movw	ax,#0x10000	; ERROR: out of range signed / unsigned value
	addw	ax,#0x10000	; ERROR: out of range signed / unsigned value
	callt	[0x10040]	; ERROR: out of range signed / unsigned value
