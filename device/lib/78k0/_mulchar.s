;--------------------------------------------------------------------------
;  _mulchar.s
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

	.globl __muluchar
	.globl __mulschar
	.globl __mulsuchar
	.globl __muluschar

	.area CODE

	; Selector bit 0 marks the first operand signed; bit 1 marks the second.
__muluchar:
	mov	c,a
	push	de
	mov	a,#0x00
	br	__mulchar_common

__mulschar:
	mov	c,a
	push	de
	mov	a,#0x03
	br	__mulchar_common

__mulsuchar:
	mov	c,a
	push	de
	mov	a,#0x01
	br	__mulchar_common

__muluschar:
	mov	c,a
	push	de
	mov	a,#0x02

__mulchar_common:
	mov	e,a
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl+0x04]
	mov	b,a
	mov	x,a
	mov	a,c
	mulu	x
	mov	d,a

	; Correct the high product byte for each sign-extended operand.
	mov	a,e
	and	a,#0x01
	bz	00001$
	mov	a,c
	cmp	a,#0x80
	bc	00001$
	mov	a,d
	sub	a,b
	mov	d,a

00001$:
	mov	a,e
	and	a,#0x02
	bz	00002$
	mov	a,b
	cmp	a,#0x80
	bc	00002$
	mov	a,d
	sub	a,c
	mov	d,a

00002$:
	mov	a,d
	pop	de

__mulchar_cleanup:
	movw	bc,ax
	pop	hl
	movw	ax,sp
	addw	ax,#0x0001
	movw	sp,ax
	push	hl
	movw	ax,bc
	ret
