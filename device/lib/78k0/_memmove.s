;--------------------------------------------------------------------------
;  _memmove.s
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
;  MA 02110-1301, USA.
;
;  As a special exception, if you link this library with other files,
;  some of which are compiled with SDCC, to produce an executable,
;  this library does not by itself cause the resulting executable to
;  be covered by the GNU General Public License. This exception does
;  not however invalidate any other reasons why the executable file
;  might be covered by the GNU General Public License.
;--------------------------------------------------------------------------

	.globl _memmove

	.area CODE

_memmove:
	push	de
	push	ax
	movw	hl,ax

	; Load the 16-bit length into BC.
	movw	ax,sp
	addw	ax,#0x0008
	movw	de,ax
	mov	a,[de]
	mov	x,a
	incw	de
	mov	a,[de]
	movw	bc,ax
	or	a,c
	bz	00006$

	; Load the source pointer into DE.
	movw	ax,sp
	addw	ax,#0x0006
	movw	de,ax
	mov	a,[de]
	mov	x,a
	incw	de
	mov	a,[de]
	movw	de,ax

	; Copy backwards exactly when src < dst; either direction is safe
	; for non-overlapping ranges.
	mov	a,d
	cmp	a,h
	bc	00003$
	bnz	00001$
	mov	a,e
	cmp	a,l
	bc	00003$
	bz	00006$

00001$:
	; Convert BC into nested DBNZ counters: B is the number of 256-byte
	; blocks, rounded up when the low-byte remainder is nonzero.
	mov	a,c
	cmp	a,#0x00
	bz	00002$
	inc	b
00002$:
	mov	a,[de]
	mov	[hl],a
	incw	de
	incw	hl
	dbnz	c,00002$
	dbnz	b,00002$
	br	!00006$

00003$:
	; Point HL and DE one byte past their ranges using the original count.
	mov	a,l
	add	a,c
	mov	l,a
	mov	a,h
	addc	a,b
	mov	h,a
	mov	a,e
	add	a,c
	mov	e,a
	mov	a,d
	addc	a,b
	mov	d,a

	mov	a,c
	cmp	a,#0x00
	bz	00004$
	inc	b
00004$:
	decw	de
	decw	hl
	mov	a,[de]
	mov	[hl],a
	dbnz	c,00004$
	dbnz	b,00004$

00006$:
	pop	bc
	pop	de
	pop	hl
	pop	ax
	pop	ax
	push	hl
	movw	ax,bc
	ret
