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

__divschar:
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
	mov	x,a

	mov	a,[hl+0x02]
	cmp	a,#0x80
	bc	00002$
	mov	c,a
	mov	a,b
	xor	a,#0x01
	mov	b,a
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
00002$:
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,b
	cmp	a,#0x00
	bz	00003$
	mov	a,x
	xor	a,#0xff
	add	a,#0x01
	br	!__divchar_cleanup

00003$:
	mov	a,x
	br	!__divchar_cleanup

__divsuchar:
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
	mov	x,a

	mov	a,[hl+0x02]
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,b
	cmp	a,#0x00
	bz	00012$
	mov	a,x
	xor	a,#0xff
	add	a,#0x01
	br	!__divchar_cleanup

00012$:
	mov	a,x
	br	!__divchar_cleanup

__divuschar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,#0x00
	mov	b,a

	mov	a,c
	mov	x,a

	mov	a,[hl+0x02]
	cmp	a,#0x80
	bc	00021$
	mov	a,#0x01
	mov	b,a
	mov	a,[hl+0x02]
	xor	a,#0xff
	add	a,#0x01
00021$:
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,b
	cmp	a,#0x00
	bz	00022$
	mov	a,x
	xor	a,#0xff
	add	a,#0x01
	br	!__divchar_cleanup

00022$:
	mov	a,x
	br	!__divchar_cleanup

__modschar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,#0x00
	mov	b,a

	mov	a,c
	cmp	a,#0x80
	bc	00031$
	mov	a,#0x01
	mov	b,a
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
00031$:
	mov	x,a

	mov	a,[hl+0x02]
	cmp	a,#0x80
	bc	00032$
	xor	a,#0xff
	add	a,#0x01
00032$:
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,c
	mov	a,b
	cmp	a,#0x00
	bz	00033$
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
	br	!__divchar_cleanup

00033$:
	mov	a,c
	br	!__divchar_cleanup

__modsuchar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,#0x00
	mov	b,a

	mov	a,c
	cmp	a,#0x80
	bc	00041$
	mov	a,#0x01
	mov	b,a
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
00041$:
	mov	x,a

	mov	a,[hl+0x02]
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,c
	mov	a,b
	cmp	a,#0x00
	bz	00042$
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
	br	!__divchar_cleanup

00042$:
	mov	a,c
	br	!__divchar_cleanup

__moduschar:
	mov	c,a
	movw	ax,sp
	movw	hl,ax

	mov	a,c
	mov	x,a

	mov	a,[hl+0x02]
	cmp	a,#0x80
	bc	00051$
	xor	a,#0xff
	add	a,#0x01
00051$:
	mov	c,a
	mov	a,#0x00
	divuw	c

	mov	a,c
	br	!__divchar_cleanup

__divchar_cleanup:
	mov	c,a
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl+0x01]
	mov	[hl+0x02],a
	mov	a,[hl+0x00]
	mov	[hl+0x01],a
	movw	ax,sp
	addw	ax,#0x0001
	movw	sp,ax
	mov	a,c
	ret
