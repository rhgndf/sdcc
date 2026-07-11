;--------------------------------------------------------------------------
;  _mullonglong.s
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

	.globl __mullonglong

	.area CODE

__mullonglong:
	movw	ax,sp
	subw	ax,#0x0018
	movw	sp,ax
	movw	hl,ax
	push	de

	mov	a,[hl+0x1c]
	mov	[hl],a
	mov	a,[hl+0x1d]
	mov	[hl+0x01],a
	mov	a,[hl+0x1e]
	mov	[hl+0x02],a
	mov	a,[hl+0x1f]
	mov	[hl+0x03],a
	mov	a,[hl+0x20]
	mov	[hl+0x04],a
	mov	a,[hl+0x21]
	mov	[hl+0x05],a
	mov	a,[hl+0x22]
	mov	[hl+0x06],a
	mov	a,[hl+0x23]
	mov	[hl+0x07],a

	mov	a,[hl+0x24]
	mov	[hl+0x08],a
	mov	a,[hl+0x25]
	mov	[hl+0x09],a
	mov	a,[hl+0x26]
	mov	[hl+0x0a],a
	mov	a,[hl+0x27]
	mov	[hl+0x0b],a
	mov	a,[hl+0x28]
	mov	[hl+0x0c],a
	mov	a,[hl+0x29]
	mov	[hl+0x0d],a
	mov	a,[hl+0x2a]
	mov	[hl+0x0e],a
	mov	a,[hl+0x2b]
	mov	[hl+0x0f],a

	mov	a,#0x00
	mov	[hl+0x10],a
	mov	[hl+0x11],a
	mov	[hl+0x12],a
	mov	[hl+0x13],a
	mov	[hl+0x14],a
	mov	[hl+0x15],a
	mov	[hl+0x16],a
	mov	[hl+0x17],a

	; Truncated base-256 schoolbook multiplication. For each source byte,
	; only products below byte eight are accumulated.
	mov	b,#0x00
00001$:
	mov	a,[hl+b]
	mov	e,a
	mov	d,#0x00
	mov	c,#0x08
00002$:
	mov	a,[hl+c]
	mov	x,a
	mov	a,e
	mulu	x
	xch	a,d
	add	a,x
	mov	x,a
	mov	a,d
	addc	a,#0x00
	mov	d,a

	; C addresses the multiplier; temporarily retarget it at result[B+C+8].
	mov	a,c
	add	a,b
	add	a,#0x08
	mov	c,a
	mov	a,x
	add	a,[hl+c]
	mov	[hl+c],a
	mov	a,d
	addc	a,#0x00
	mov	d,a
	mov	a,c
	sub	a,b
	sub	a,#0x08
	mov	c,a
	inc	c
	mov	a,c
	add	a,b
	cmp	a,#0x10
	bc	00002$

	inc	b
	mov	a,b
	cmp	a,#0x08
	bc	00001$

00004$:
	; Copy the product to the caller-provided return destination.
	mov	a,[hl+0x1a]
	mov	x,a
	mov	a,[hl+0x1b]
	movw	bc,ax
	movw	ax,hl
	addw	ax,#0x0010
	movw	de,ax
	movw	ax,bc
	movw	hl,ax
	mov	b,#0x08
00005$:
	mov	a,[de]
	mov	[hl],a
	incw	de
	incw	hl
	dbnz	b,00005$

	; Restore DE and move the return address over the hidden pointer and arguments.
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	movw	de,ax
	mov	a,[hl+0x1b]
	mov	[hl+0x2d],a
	mov	a,[hl+0x1a]
	mov	[hl+0x2c],a
	movw	ax,sp
	addw	ax,#0x002c
	movw	sp,ax
	ret
