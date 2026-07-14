;--------------------------------------------------------------------------
;  _memcmp.s
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

	.globl _memcmp

	.area CODE

_memcmp:
	push	de
	movw	hl,ax
	movw	ax,sp
	addw	ax,#0x0006
	movw	de,ax
	mov	a,[de]
	mov	x,a
	incw	de
	mov	a,[de]
	movw	bc,ax
	or	a,c
	bz	00003$
	mov	a,#0x00
	cmp	a,c
	addc	b,a
	movw	ax,sp
	addw	ax,#0x0004
	movw	de,ax
	mov	a,[de]
	mov	x,a
	incw	de
	mov	a,[de]
	movw	de,ax

00001$:
	mov	a,[hl]
	mov	x,a
	mov	a,[de]
	xch	a,x
	cmp	a,x
	bnz	00002$
	incw	hl
	incw	de
	dbnz	c,00001$
	dbnz	b,00001$

00003$:
	movw	bc,#0x0000

00004$:
	pop	de
	pop	hl
	pop	ax
	pop	ax
	push	hl
	movw	ax,bc
	ret

00002$:
	sub	a,x
	mov	x,a
	mov	a,#0x00
	subc	a,#0x00
	movw	bc,ax
	br	00004$
