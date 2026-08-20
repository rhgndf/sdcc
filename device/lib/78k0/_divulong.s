;--------------------------------------------------------------------------
;  _divulong.s
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

	.globl __divulong
	.globl __modulong

	.area CODE

__divulong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000d
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x00
	br	__divmodulong

__modulong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000d
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x01

__divmodulong:
	mov	[hl+0x0c],a

	mov	a,[hl+0x0f]
	mov	x,a
	mov	a,[hl+0x10]
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a
	mov	a,c
	mov	[hl+0x02],a
	mov	a,b
	mov	[hl+0x03],a

	mov	a,#0x00
	mov	[hl+0x04],a
	mov	[hl+0x05],a
	mov	[hl+0x06],a
	mov	[hl+0x07],a

	mov	a,[hl+0x13]
	mov	[hl+0x08],a
	mov	a,[hl+0x14]
	mov	[hl+0x09],a
	mov	a,[hl+0x15]
	mov	[hl+0x0a],a
	mov	a,[hl+0x16]
	mov	[hl+0x0b],a

	; Divide one byte at a time when the divisor fits DIVUW's C operand.
	mov	a,[hl+0x0b]
	or	a,[hl+0x0a]
	or	a,[hl+0x09]
	bnz	00006$
	mov	a,[hl+0x08]
	cmp	a,#0x00
	bz	00006$
	mov	e,a
	mov	d,#0x00
	mov	b,#0x04
00007$:
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
	bnz	00007$

	mov	a,[hl+0x0c]
	cmp	a,#0x00
	bnz	00008$
	br	!00005$
00008$:
	mov	a,d
	mov	[hl],a
	mov	a,#0x00
	mov	[hl+0x01],a
	mov	[hl+0x02],a
	mov	[hl+0x03],a
	br	!00005$

00006$:
	mov	b,#0x20

	; Skip leading zero dividend bits. They leave both quotient and remainder
	; unchanged, so enter the full loop only when the first one reaches CY.
00009$:
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
	bc	00010$
	dbnz	b,00009$
	br	00005$

00001$:
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

00010$:
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

	mov	a,[hl+0x07]
	cmp	a,[hl+0x0b]
	bc	00002$
	bnz	00003$
	mov	a,[hl+0x06]
	cmp	a,[hl+0x0a]
	bc	00002$
	bnz	00003$
	mov	a,[hl+0x05]
	cmp	a,[hl+0x09]
	bc	00002$
	bnz	00003$
	mov	a,[hl+0x04]
	cmp	a,[hl+0x08]
	bc	00002$

00003$:
	mov	a,[hl+0x04]
	sub	a,[hl+0x08]
	mov	[hl+0x04],a
	mov	a,[hl+0x05]
	subc	a,[hl+0x09]
	mov	[hl+0x05],a
	mov	a,[hl+0x06]
	subc	a,[hl+0x0a]
	mov	[hl+0x06],a
	mov	a,[hl+0x07]
	subc	a,[hl+0x0b]
	mov	[hl+0x07],a
	mov	a,[hl]
	or	a,#0x01
	mov	[hl],a

00002$:
	dbnz	b,00001$

	mov	a,[hl+0x0c]
	cmp	a,#0x00
	bnz	00004$

	br	00005$

00004$:
	mov	a,[hl+0x04]
	mov	[hl],a
	mov	a,[hl+0x05]
	mov	[hl+0x01],a
	mov	a,[hl+0x06]
	mov	[hl+0x02],a
	mov	a,[hl+0x07]
	mov	[hl+0x03],a

00005$:
	mov	a,[hl+0x0d]
	mov	x,a
	mov	a,[hl+0x0e]
	movw	de,ax

	mov	a,[hl+0x12]
	mov	[hl+0x16],a
	mov	a,[hl+0x11]
	mov	[hl+0x15],a

	movw	ax,sp
	addw	ax,#0x0015
	movw	sp,ax
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	ret
