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

__muluchar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,c
	mulu	x
	br	!__mulchar_cleanup

__mulschar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,#0x00
	mov	b,a

	mov	a,c
	cmp	a,#0x80
	bc	00001$
	mov	c,a
	mov	a,b
	xor	a,#0x01
	mov	b,a
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
00001$:
	mov	c,a

	mov	a,[hl+0x02]
	cmp	a,#0x80
	bc	00002$
	mov	x,a
	mov	a,b
	xor	a,#0x01
	mov	b,a
	mov	a,x
	xor	a,#0xff
	add	a,#0x01
00002$:
	mov	x,a
	mov	a,c
	mulu	x

	mov	c,a
	mov	a,b
	cmp	a,#0x00
	mov	a,c
	bz	00003$
	mov	c,a
	mov	a,#0x00
	sub	a,x
	mov	x,a
	mov	a,#0x00
	subc	a,c
	br	!__mulchar_cleanup

00003$:
	br	!__mulchar_cleanup

__mulsuchar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,#0x00
	mov	b,a

	mov	a,c
	cmp	a,#0x80
	bc	00011$
	mov	a,#0x01
	mov	b,a
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
00011$:
	mov	c,a

	mov	a,[hl+0x02]
	mov	x,a
	mov	a,c
	mulu	x

	mov	c,a
	mov	a,b
	cmp	a,#0x00
	mov	a,c
	bz	00012$
	mov	c,a
	mov	a,#0x00
	sub	a,x
	mov	x,a
	mov	a,#0x00
	subc	a,c
	br	!__mulchar_cleanup

00012$:
	br	!__mulchar_cleanup

__muluschar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,#0x00
	mov	b,a

	mov	a,[hl+0x02]
	cmp	a,#0x80
	bc	00021$
	mov	a,#0x01
	mov	b,a
	mov	a,[hl+0x02]
	xor	a,#0xff
	add	a,#0x01
00021$:
	mov	x,a

	mov	a,c
	mulu	x

	mov	c,a
	mov	a,b
	cmp	a,#0x00
	mov	a,c
	bz	00022$
	mov	c,a
	mov	a,#0x00
	sub	a,x
	mov	x,a
	mov	a,#0x00
	subc	a,c
	br	!__mulchar_cleanup

00022$:
	br	!__mulchar_cleanup

__mulchar_cleanup:
	movw	bc,ax
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl+0x01]
	mov	[hl+0x02],a
	mov	a,[hl+0x00]
	mov	[hl+0x01],a
	movw	ax,sp
	addw	ax,#0x0001
	movw	sp,ax
	movw	ax,bc
	ret
