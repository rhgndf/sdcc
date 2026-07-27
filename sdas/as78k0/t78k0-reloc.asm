	.title	78K0 Relocation Test
	.module	reloc_use

	.area	CODE
	.globl	saddr_byte
	.globl	sfr_byte
	.globl	saddr_word
	.globl	sfr_word
	.globl	addr16_target
	.globl	immediate_byte
	.globl	word_value
	.globl	displacement
	.globl	addr16_word
	.globl	dotted.saddr

	; Generic relocations retained from the original fixture.  The EXPECT
	; values use 0x1234, 0x56, 0x1234, 0x12, and 0x5678 respectively.
	call	!addr16_target		; EXPECT: 9A 34 12
	mov	a,#immediate_byte	; EXPECT: A1 56
	mov	a,#<word_value		; EXPECT: A1 34
	mov	a,#>word_value		; EXPECT: A1 12
	mov	a,[hl+displacement]	; EXPECT: AE 12
	movw	ax,!addr16_word		; EXPECT: 02 78 56

	; Bare unresolved direct operands are short addresses.  '@' explicitly
	; preserves the SFR class so the assembler can choose the SFR opcode.
	mov	a,saddr_byte		; EXPECT: F0 20
	mov	a,@sfr_byte		; EXPECT: F4 80
	mov	saddr_byte,a		; EXPECT: F2 20
	mov	@sfr_byte,a		; EXPECT: F6 80
	xch	a,saddr_byte		; EXPECT: 83 20
	xch	a,@sfr_byte		; EXPECT: 93 80

	; MOVW relocations also retain and validate their even-address rule.
	movw	ax,saddr_word		; EXPECT: 89 22
	movw	ax,@sfr_word		; EXPECT: A9 82
	movw	saddr_word,ax		; EXPECT: 99 22
	movw	@sfr_word,ax		; EXPECT: B9 82

	; Symbolic bit operands must keep the bit suffix separate from the
	; relocatable direct address.
	set1	saddr_byte.1		; EXPECT: 1A 20
	set1	@sfr_byte.2		; EXPECT: 71 2A 80
	mov1	cy,saddr_byte.3		; EXPECT: 71 34 20
	mov1	cy,@sfr_byte.4		; EXPECT: 71 4C 80
	set1	dotted.saddr.5		; EXPECT: 5A 24
