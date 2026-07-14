;--------------------------------------------------------------------------
;  _divuint.s
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

	.globl __divuint
	.globl __moduint

	.area CODE

__divuint:
	mov	b,a
	mov	a,x
	mov	c,a
	push	de
	movw	ax,sp
	subw	ax,#0x0007
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x00
	br	__divmoduint

__moduint:
	mov	b,a
	mov	a,x
	mov	c,a
	push	de
	movw	ax,sp
	subw	ax,#0x0007
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x01

__divmoduint:
	mov	[hl+0x06],a

	mov	a,c
	mov	[hl],a
	mov	a,b
	mov	[hl+0x01],a
	mov	a,#0x00
	mov	[hl+0x02],a
	mov	[hl+0x03],a
	mov	a,[hl+0x0b]
	mov	[hl+0x04],a
	mov	a,[hl+0x0c]
	mov	[hl+0x05],a

	; DIVUW handles a 16-bit dividend directly when the divisor fits in C.
	cmp	a,#0x00
	bnz	00006$
	mov	a,[hl+0x04]
	cmp	a,#0x00
	bz	00006$
	mov	c,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	divuw	c
	mov	b,a
	mov	a,[hl+0x06]
	cmp	a,#0x00
	bnz	00007$
	mov	a,b
	br	00005$

00007$:
	mov	a,c
	mov	x,a
	mov	a,#0x00
	br	00005$

00006$:
	; A divisor of at least 256 can produce only an 8-bit quotient. Seed the
	; remainder with the dividend's high byte and process only its low byte.
	mov	a,[hl+0x01]
	mov	[hl+0x02],a
	mov	a,[hl]
	mov	[hl+0x01],a
	mov	a,#0x00
	mov	[hl],a
	mov	[hl+0x03],a
	mov	b,#0x08

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

	mov	a,[hl+0x03]
	cmp	a,[hl+0x05]
	bc	00002$
	bnz	00003$
	mov	a,[hl+0x02]
	cmp	a,[hl+0x04]
	bc	00002$

00003$:
	mov	a,[hl+0x02]
	sub	a,[hl+0x04]
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	subc	a,[hl+0x05]
	mov	[hl+0x03],a
	mov	a,[hl]
	or	a,#0x01
	mov	[hl],a

00002$:
	dbnz	b,00001$

	mov	a,[hl+0x06]
	cmp	a,#0x00
	bnz	00004$

	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	br	00005$

00004$:
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]

00005$:
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a

	mov	a,[hl+0x07]
	mov	x,a
	mov	a,[hl+0x08]
	movw	de,ax

	mov	a,[hl+0x0a]
	mov	[hl+0x0c],a
	mov	a,[hl+0x09]
	mov	[hl+0x0b],a

	movw	ax,sp
	addw	ax,#0x000b
	movw	sp,ax
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	ret
