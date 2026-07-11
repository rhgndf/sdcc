;--------------------------------------------------------------------------
;  _divlonglong.s
;
;  Copyright (C) 2026
;
;  This library is free software; you can redistribute it and/or modify it
;  under the terms of the GNU General Public License as published by the
;  Free Software Foundation; either version 2, or (at your option) any
;  later version.
;
;  This library is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this library; see the file COPYING. If not, write to the
;  Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;   MA 02110-1301, USA.
;
;  As a special exception, if you link this library with other files,
;  some of which are compiled with SDCC, to produce an executable,
;  this library does not by itself cause the resulting executable to
;  be covered by the GNU General Public License. This exception does
;  not however invalidate any other reasons why the executable file
;   might be covered by the GNU General Public License.
;--------------------------------------------------------------------------

	.globl __divulonglong
	.globl __modulonglong
	.globl __divslonglong
	.globl __modslonglong

	.area CODE

__divulonglong:
	mov	a,#0x00
	br	!__divmodulonglong

__modulonglong:
	mov	a,#0x01

__divmodulonglong:
	mov	c,a
	movw	ax,sp
	subw	ax,#0x0019
	movw	sp,ax
	movw	hl,ax
	push	de
	mov	a,c
	mov	[hl+0x18],a

	mov	a,[hl+0x1d]
	mov	[hl],a
	mov	a,[hl+0x1e]
	mov	[hl+0x01],a
	mov	a,[hl+0x1f]
	mov	[hl+0x02],a
	mov	a,[hl+0x20]
	mov	[hl+0x03],a
	mov	a,[hl+0x21]
	mov	[hl+0x04],a
	mov	a,[hl+0x22]
	mov	[hl+0x05],a
	mov	a,[hl+0x23]
	mov	[hl+0x06],a
	mov	a,[hl+0x24]
	mov	[hl+0x07],a

	mov	a,#0x00
	mov	[hl+0x08],a
	mov	[hl+0x09],a
	mov	[hl+0x0a],a
	mov	[hl+0x0b],a
	mov	[hl+0x0c],a
	mov	[hl+0x0d],a
	mov	[hl+0x0e],a
	mov	[hl+0x0f],a

	mov	a,[hl+0x25]
	mov	[hl+0x10],a
	mov	a,[hl+0x26]
	mov	[hl+0x11],a
	mov	a,[hl+0x27]
	mov	[hl+0x12],a
	mov	a,[hl+0x28]
	mov	[hl+0x13],a
	mov	a,[hl+0x29]
	mov	[hl+0x14],a
	mov	a,[hl+0x2a]
	mov	[hl+0x15],a
	mov	a,[hl+0x2b]
	mov	[hl+0x16],a
	mov	a,[hl+0x2c]
	mov	[hl+0x17],a

	call	!__divmodlonglong_magnitude
	mov	a,[hl+0x18]
	cmp	a,#0x00
	bnz	00104$
	movw	ax,hl
	movw	de,ax
	br	!00105$

00104$:
	movw	ax,hl
	addw	ax,#0x0008
	movw	de,ax

00105$:
	; Copy the selected result to the caller-provided return destination.
	mov	a,[hl+0x1b]
	mov	x,a
	mov	a,[hl+0x1c]
	movw	bc,ax
	movw	ax,bc
	movw	hl,ax
	mov	b,#0x08
00108$:
	mov	a,[de]
	mov	[hl],a
	incw	de
	incw	hl
	dbnz	b,00108$

	; Restore DE and move the return address over the hidden pointer and arguments.
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	movw	de,ax
	mov	a,[hl+0x1c]
	mov	[hl+0x2e],a
	mov	a,[hl+0x1b]
	mov	[hl+0x2d],a
	movw	ax,sp
	addw	ax,#0x002d
	movw	sp,ax
	ret

__divslonglong:
	mov	a,#0x00
	br	!__divmodslonglong

__modslonglong:
	mov	a,#0x01

__divmodslonglong:
	mov	c,a
	movw	ax,sp
	subw	ax,#0x001a
	movw	sp,ax
	movw	hl,ax
	push	de
	mov	a,c
	mov	[hl+0x18],a
	mov	a,#0x00
	mov	[hl+0x19],a

	mov	a,[hl+0x1e]
	mov	[hl],a
	mov	a,[hl+0x1f]
	mov	[hl+0x01],a
	mov	a,[hl+0x20]
	mov	[hl+0x02],a
	mov	a,[hl+0x21]
	mov	[hl+0x03],a
	mov	a,[hl+0x22]
	mov	[hl+0x04],a
	mov	a,[hl+0x23]
	mov	[hl+0x05],a
	mov	a,[hl+0x24]
	mov	[hl+0x06],a
	mov	a,[hl+0x25]
	mov	[hl+0x07],a

	mov	a,[hl+0x26]
	mov	[hl+0x10],a
	mov	a,[hl+0x27]
	mov	[hl+0x11],a
	mov	a,[hl+0x28]
	mov	[hl+0x12],a
	mov	a,[hl+0x29]
	mov	[hl+0x13],a
	mov	a,[hl+0x2a]
	mov	[hl+0x14],a
	mov	a,[hl+0x2b]
	mov	[hl+0x15],a
	mov	a,[hl+0x2c]
	mov	[hl+0x16],a
	mov	a,[hl+0x2d]
	mov	[hl+0x17],a

	mov	a,[hl+0x07]
	and	a,#0x80
	bz	00201$
	mov	a,#0x01
	mov	[hl+0x19],a
	mov	a,[hl]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl],a
	mov	a,[hl+0x01]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x03],a
	mov	a,[hl+0x04]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x04],a
	mov	a,[hl+0x05]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x05],a
	mov	a,[hl+0x06]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x06],a
	mov	a,[hl+0x07]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x07],a

00201$:
	mov	a,[hl+0x17]
	and	a,#0x80
	bz	00203$
	mov	a,[hl+0x10]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl+0x10],a
	mov	a,[hl+0x11]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x11],a
	mov	a,[hl+0x12]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x12],a
	mov	a,[hl+0x13]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x13],a
	mov	a,[hl+0x14]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x14],a
	mov	a,[hl+0x15]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x15],a
	mov	a,[hl+0x16]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x16],a
	mov	a,[hl+0x17]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x17],a

	mov	a,[hl+0x18]
	cmp	a,#0x00
	bnz	00203$
	mov	a,[hl+0x19]
	xor	a,#0x01
	mov	[hl+0x19],a

00203$:
	call	!__divmodlonglong_magnitude
	mov	a,[hl+0x18]
	cmp	a,#0x00
	bnz	00208$

	mov	a,[hl+0x19]
	cmp	a,#0x00
	bz	00207$
	mov	a,[hl]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl],a
	mov	a,[hl+0x01]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x03],a
	mov	a,[hl+0x04]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x04],a
	mov	a,[hl+0x05]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x05],a
	mov	a,[hl+0x06]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x06],a
	mov	a,[hl+0x07]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x07],a

00207$:
	movw	ax,hl
	movw	de,ax
	br	!00210$

00208$:
	mov	a,[hl+0x19]
	cmp	a,#0x00
	bz	00209$
	mov	a,[hl+0x08]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl+0x08],a
	mov	a,[hl+0x09]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x09],a
	mov	a,[hl+0x0a]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x0a],a
	mov	a,[hl+0x0b]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x0b],a
	mov	a,[hl+0x0c]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x0c],a
	mov	a,[hl+0x0d]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x0d],a
	mov	a,[hl+0x0e]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x0e],a
	mov	a,[hl+0x0f]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x0f],a

00209$:
	movw	ax,hl
	addw	ax,#0x0008
	movw	de,ax

00210$:
	; Copy the selected result to the caller-provided return destination.
	mov	a,[hl+0x1c]
	mov	x,a
	mov	a,[hl+0x1d]
	movw	bc,ax
	movw	ax,bc
	movw	hl,ax
	mov	b,#0x08
00213$:
	mov	a,[de]
	mov	[hl],a
	incw	de
	incw	hl
	dbnz	b,00213$

	; Restore DE and move the return address over the hidden pointer and arguments.
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	movw	de,ax
	mov	a,[hl+0x1d]
	mov	[hl+0x2f],a
	mov	a,[hl+0x1c]
	mov	[hl+0x2e],a
	movw	ax,sp
	addw	ax,#0x002e
	movw	sp,ax
	ret

; Divide the magnitude in bytes 0..7 by bytes 16..23. The quotient replaces
; the dividend and the remainder is left in bytes 8..15.
__divmodlonglong_magnitude:
	mov	a,#0x00
	mov	[hl+0x08],a
	mov	[hl+0x09],a
	mov	[hl+0x0a],a
	mov	[hl+0x0b],a
	mov	[hl+0x0c],a
	mov	[hl+0x0d],a
	mov	[hl+0x0e],a
	mov	[hl+0x0f],a

	; DIVUW can consume a wide dividend a byte at a time when the divisor
	; fits in C. D carries the remainder between bytes.
	mov	a,[hl+0x17]
	or	a,[hl+0x16]
	or	a,[hl+0x15]
	or	a,[hl+0x14]
	or	a,[hl+0x13]
	or	a,[hl+0x12]
	or	a,[hl+0x11]
	bnz	00305$
	mov	a,[hl+0x10]
	cmp	a,#0x00
	bz	00305$
	mov	e,a
	mov	d,#0x00
	mov	b,#0x08
00306$:
	dec	b
	mov	a,[hl+b]
	mov	x,a
	mov	a,e
	mov	c,a
	mov	a,d
	divuw	c
	mov	a,c
	mov	d,a
	mov	a,x
	mov	[hl+b],a
	mov	a,b
	cmp	a,#0x00
	bnz	00306$
	mov	a,d
	mov	[hl+0x08],a
	ret

00305$:
	mov	b,#0x40

00301$:
	clr1	cy
	mov	a,[hl]
	rolc	a,1
	mov	[hl],a
	mov	a,[hl+0x01]
	rolc	a,1
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	rolc	a,1
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	rolc	a,1
	mov	[hl+0x03],a
	mov	a,[hl+0x04]
	rolc	a,1
	mov	[hl+0x04],a
	mov	a,[hl+0x05]
	rolc	a,1
	mov	[hl+0x05],a
	mov	a,[hl+0x06]
	rolc	a,1
	mov	[hl+0x06],a
	mov	a,[hl+0x07]
	rolc	a,1
	mov	[hl+0x07],a
	mov	a,[hl+0x08]
	rolc	a,1
	mov	[hl+0x08],a
	mov	a,[hl+0x09]
	rolc	a,1
	mov	[hl+0x09],a
	mov	a,[hl+0x0a]
	rolc	a,1
	mov	[hl+0x0a],a
	mov	a,[hl+0x0b]
	rolc	a,1
	mov	[hl+0x0b],a
	mov	a,[hl+0x0c]
	rolc	a,1
	mov	[hl+0x0c],a
	mov	a,[hl+0x0d]
	rolc	a,1
	mov	[hl+0x0d],a
	mov	a,[hl+0x0e]
	rolc	a,1
	mov	[hl+0x0e],a
	mov	a,[hl+0x0f]
	rolc	a,1
	mov	[hl+0x0f],a

	mov	a,[hl+0x0f]
	cmp	a,[hl+0x17]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x0e]
	cmp	a,[hl+0x16]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x0d]
	cmp	a,[hl+0x15]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x0c]
	cmp	a,[hl+0x14]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x0b]
	cmp	a,[hl+0x13]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x0a]
	cmp	a,[hl+0x12]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x09]
	cmp	a,[hl+0x11]
	bc	00302$
	bnz	00303$
	mov	a,[hl+0x08]
	cmp	a,[hl+0x10]
	bc	00302$

00303$:
	mov	a,[hl+0x08]
	sub	a,[hl+0x10]
	mov	[hl+0x08],a
	mov	a,[hl+0x09]
	subc	a,[hl+0x11]
	mov	[hl+0x09],a
	mov	a,[hl+0x0a]
	subc	a,[hl+0x12]
	mov	[hl+0x0a],a
	mov	a,[hl+0x0b]
	subc	a,[hl+0x13]
	mov	[hl+0x0b],a
	mov	a,[hl+0x0c]
	subc	a,[hl+0x14]
	mov	[hl+0x0c],a
	mov	a,[hl+0x0d]
	subc	a,[hl+0x15]
	mov	[hl+0x0d],a
	mov	a,[hl+0x0e]
	subc	a,[hl+0x16]
	mov	[hl+0x0e],a
	mov	a,[hl+0x0f]
	subc	a,[hl+0x17]
	mov	[hl+0x0f],a
	mov	a,[hl]
	or	a,#0x01
	mov	[hl],a

00302$:
	dbnz	b,00304$
	ret
00304$:
	br	!00301$
