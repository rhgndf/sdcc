;--------------------------------------------------------------------------
;  _mullong.s
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

	.globl __mullong

	.area CODE

__mullong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000c
	movw	sp,ax
	movw	hl,ax

	mov	a,[hl+0x0e]
	mov	x,a
	mov	a,[hl+0x0f]
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a
	mov	a,c
	mov	[hl+0x02],a
	mov	a,b
	mov	[hl+0x03],a

	mov	a,[hl+0x12]
	mov	[hl+0x04],a
	mov	a,[hl+0x13]
	mov	[hl+0x05],a
	mov	a,[hl+0x14]
	mov	[hl+0x06],a
	mov	a,[hl+0x15]
	mov	[hl+0x07],a

	mov	a,#0x00
	mov	[hl+0x08],a
	mov	[hl+0x09],a
	mov	[hl+0x0a],a
	mov	[hl+0x0b],a

	; Truncated base-256 schoolbook multiplication. For each source byte,
	; only products below byte four are accumulated.
	mov	b,#0x00
00001$:
	mov	a,[hl+b]
	mov	e,a
	mov	d,#0x00
	mov	c,#0x04
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

	; C addresses the multiplier; temporarily retarget it at result[B+C+4].
	mov	a,c
	add	a,b
	add	a,#0x04
	mov	c,a
	mov	a,x
	add	a,[hl+c]
	mov	[hl+c],a
	mov	a,d
	addc	a,#0x00
	mov	d,a
	mov	a,c
	sub	a,b
	sub	a,#0x04
	mov	c,a
	inc	c
	mov	a,c
	add	a,b
	cmp	a,#0x08
	bc	00002$

	inc	b
	mov	a,b
	cmp	a,#0x04
	bc	00001$

	mov	a,[hl+0x0c]
	mov	x,a
	mov	a,[hl+0x0d]
	movw	de,ax

	mov	a,[hl+0x11]
	mov	[hl+0x15],a
	mov	a,[hl+0x10]
	mov	[hl+0x14],a

	movw	ax,sp
	addw	ax,#0x0014
	movw	sp,ax
	mov	a,[hl+0x0a]
	mov	c,a
	mov	a,[hl+0x0b]
	mov	b,a
	mov	a,[hl+0x08]
	mov	x,a
	mov	a,[hl+0x09]
	ret
