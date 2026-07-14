;--------------------------------------------------------------------------
;  _divchar.s
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

	.globl __divschar
	.globl __divsuchar
	.globl __divuschar
	.globl __modschar
	.globl __modsuchar
	.globl __moduschar

	.area CODE

	; Selector bits 0/1 mark signed operands; bit 2 requests the remainder.
__divschar:
	mov	c,a
	push	de
	mov	a,#0x03
	br	!__divchar_common

__divsuchar:
	mov	c,a
	push	de
	mov	a,#0x01
	br	!__divchar_common

__divuschar:
	mov	c,a
	push	de
	mov	a,#0x02
	br	!__divchar_common

__modschar:
	mov	c,a
	push	de
	mov	a,#0x07
	br	!__divchar_common

__modsuchar:
	mov	c,a
	push	de
	mov	a,#0x05
	br	!__divchar_common

__moduschar:
	mov	c,a
	push	de
	mov	a,#0x06

__divchar_common:
	mov	e,a
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl+0x04]
	mov	b,a
	mov	d,#0x00

	mov	a,e
	and	a,#0x01
	bz	00001$
	mov	a,c
	cmp	a,#0x80
	bc	00001$
	mov	a,#0x00
	sub	a,c
	mov	c,a
	mov	d,#0x01

00001$:
	mov	a,e
	and	a,#0x02
	bz	00003$
	mov	a,b
	cmp	a,#0x80
	bc	00003$
	mov	a,#0x00
	sub	a,b
	mov	b,a
	mov	a,e
	and	a,#0x04
	bnz	00003$
	mov	a,d
	xor	a,#0x01
	mov	d,a

00003$:
	mov	a,c
	mov	x,a
	mov	a,b
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,e
	and	a,#0x04
	bz	00004$
	mov	a,c
	mov	x,a

00004$:
	mov	a,d
	cmp	a,#0x00
	bz	00005$
	mov	a,#0x00
	sub	a,x
	mov	x,a
	mov	a,#0x00
	subc	a,#0x00
	br	00006$

00005$:
	mov	a,#0x00

00006$:
	pop	de

__divchar_cleanup:
	movw	bc,ax
	pop	hl
	movw	ax,sp
	addw	ax,#0x0001
	movw	sp,ax
	push	hl
	movw	ax,bc
	ret
