;--------------------------------------------------------------------------
;  _divslong.s
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

	.globl __divslong
	.globl __modslong

	.area CODE

__divslong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000e
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x00
	br	!__divmodslong

__modslong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000e
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x01

__divmodslong:
	mov	[hl+0x0c],a
	mov	a,#0x00
	mov	[hl+0x0d],a

	mov	a,[hl+0x10]
	mov	x,a
	mov	a,[hl+0x11]
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a
	mov	a,c
	mov	[hl+0x02],a
	mov	a,b
	mov	[hl+0x03],a

	mov	a,[hl+0x14]
	mov	[hl+0x08],a
	mov	a,[hl+0x15]
	mov	[hl+0x09],a
	mov	a,[hl+0x16]
	mov	[hl+0x0a],a
	mov	a,[hl+0x17]
	mov	[hl+0x0b],a

	mov	a,[hl+0x03]
	and	a,#0x80
	bz	00001$
	mov	a,#0x01
	mov	[hl+0x0d],a
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

00001$:
	mov	a,[hl+0x0b]
	and	a,#0x80
	bz	00003$
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
	cmp	a,#0x00
	bnz	00003$
	mov	a,[hl+0x0d]
	xor	a,#0x01
	mov	[hl+0x0d],a

00003$:
	mov	a,#0x00
	mov	[hl+0x04],a
	mov	[hl+0x05],a
	mov	[hl+0x06],a
	mov	[hl+0x07],a
	mov	b,#0x20

00004$:
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

	mov	a,[hl+0x07]
	cmp	a,[hl+0x0b]
	bc	00005$
	bnz	00006$
	mov	a,[hl+0x06]
	cmp	a,[hl+0x0a]
	bc	00005$
	bnz	00006$
	mov	a,[hl+0x05]
	cmp	a,[hl+0x09]
	bc	00005$
	bnz	00006$
	mov	a,[hl+0x04]
	cmp	a,[hl+0x08]
	bc	00005$

00006$:
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

00005$:
	dbnz	b,00004$
	mov	a,[hl+0x0c]
	cmp	a,#0x00
	bnz	00008$
	mov	a,[hl+0x0d]
	cmp	a,#0x00
	bz	00007$
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
00007$:
	br	!00010$

00008$:
	mov	a,[hl+0x0d]
	cmp	a,#0x00
	bz	00009$
	mov	a,[hl+0x04]
	xor	a,#0xff
	add	a,#0x01
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
00009$:
	mov	a,[hl+0x04]
	mov	[hl],a
	mov	a,[hl+0x05]
	mov	[hl+0x01],a
	mov	a,[hl+0x06]
	mov	[hl+0x02],a
	mov	a,[hl+0x07]
	mov	[hl+0x03],a

00010$:
	mov	a,[hl+0x0e]
	mov	x,a
	mov	a,[hl+0x0f]
	movw	de,ax

	mov	a,[hl+0x13]
	mov	[hl+0x17],a
	mov	a,[hl+0x12]
	mov	[hl+0x16],a

	movw	ax,sp
	addw	ax,#0x0016
	movw	sp,ax
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	ret
