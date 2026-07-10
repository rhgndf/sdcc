;--------------------------------------------------------------------------
;  _strlen.s
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

	.globl _strlen

	.area CODE

_strlen:
	mov	b,a
	mov	a,x
	mov	c,a
	movw	ax,sp
	subw	ax,#0x0004
	movw	sp,ax
	movw	hl,ax

	mov	a,c
	mov	[hl+0x00],a
	mov	a,b
	mov	[hl+0x01],a
	mov	a,#0x00
	mov	[hl+0x02],a
	mov	[hl+0x03],a

00001$:
	mov	a,[hl+0x00]
	mov	x,a
	mov	a,[hl+0x01]
	movw	hl,ax
	mov	a,[hl+0x00]
	mov	b,a

	movw	ax,sp
	movw	hl,ax
	mov	a,b
	cmp	a,#0x00
	bz	00002$

	mov	a,[hl+0x00]
	add	a,#0x01
	mov	[hl+0x00],a
	mov	a,[hl+0x01]
	addc	a,#0x00
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	add	a,#0x01
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	addc	a,#0x00
	mov	[hl+0x03],a
	br	!00001$

00002$:
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	movw	ax,sp
	addw	ax,#0x0004
	movw	sp,ax
	mov	a,c
	mov	x,a
	mov	a,b
	ret
