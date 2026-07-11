;--------------------------------------------------------------------------
;  _divsint.s
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

	.globl __divsint
	.globl __modsint
	.globl __divuint
	.globl __moduint

	.area CODE

__divsint:
	mov	b,a
	mov	a,x
	mov	c,a
	movw	ax,de
	push	ax
	movw	ax,sp
	subw	ax,#0x0006
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x00
	br	!__divmodsint

__modsint:
	mov	b,a
	mov	a,x
	mov	c,a
	movw	ax,de
	push	ax
	movw	ax,sp
	subw	ax,#0x0006
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x01

__divmodsint:
	mov	[hl+0x00],a

	cmp	a,#0x00
	bnz	00001$
	mov	a,b
	xor	a,[hl+0x0b]
	and	a,#0x80
	br	!00002$

00001$:
	mov	a,b
	and	a,#0x80

00002$:
	mov	[hl+0x01],a

	mov	a,c
	mov	[hl+0x02],a
	mov	a,b
	mov	[hl+0x03],a
	cmp	a,#0x80
	bc	00003$
	mov	a,[hl+0x02]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x03],a

00003$:
	mov	a,[hl+0x0a]
	mov	[hl+0x04],a
	mov	a,[hl+0x0b]
	mov	[hl+0x05],a
	cmp	a,#0x80
	bc	00004$
	mov	a,[hl+0x04]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl+0x04],a
	mov	a,[hl+0x05]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x05],a

00004$:
	mov	a,[hl+0x05]
	cmp	a,#0x00
	bnz	00009$
	mov	a,[hl+0x04]
	cmp	a,#0x00
	bz	00009$
	mov	c,a
	mov	a,[hl+0x00]
	cmp	a,#0x00
	bnz	00010$
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]
	divuw	c
	br	!00006$

00010$:
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]
	divuw	c
	mov	a,c
	mov	x,a
	mov	a,#0x00
	br	!00006$

00009$:
	mov	a,[hl+0x04]
	mov	x,a
	mov	a,[hl+0x05]
	push	ax

	mov	a,[hl+0x00]
	cmp	a,#0x00
	bnz	00005$
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]
	call	!__divuint
	br	!00006$

00005$:
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]
	call	!__moduint

00006$:
	mov	b,a
	mov	a,x
	mov	c,a
	movw	ax,sp
	movw	hl,ax

	mov	a,[hl+0x01]
	cmp	a,#0x00
	bnz	00007$
	mov	a,c
	mov	x,a
	mov	a,b
	br	!00008$

00007$:
	mov	a,c
	xor	a,#0xff
	add	a,#0x01
	mov	x,a
	mov	a,b
	xor	a,#0xff
	addc	a,#0x00

00008$:
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl+0x00],a

	mov	a,[hl+0x06]
	mov	x,a
	mov	a,[hl+0x07]
	movw	de,ax

	mov	a,[hl+0x09]
	mov	c,a
	mov	a,c
	mov	[hl+0x0b],a
	mov	a,[hl+0x08]
	mov	c,a
	mov	a,c
	mov	[hl+0x0a],a

	movw	ax,sp
	addw	ax,#0x000a
	movw	sp,ax
	mov	a,[hl+0x00]
	mov	x,a
	mov	a,[hl+0x01]
	ret
