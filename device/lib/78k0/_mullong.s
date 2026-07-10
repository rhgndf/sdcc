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
	.globl ___SDCC_k78k0_ret2
	.globl ___SDCC_k78k0_ret3

	.area CODE

__mullong:
	push	ax
	movw	ax,de
	push	ax
	movw	ax,sp
	subw	ax,#0x000c
	movw	sp,ax
	movw	hl,ax

	mov	a,[hl+0x0e]
	mov	x,a
	mov	a,[hl+0x0f]
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl+0x00],a
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

	mov	a,#0x20
	mov	b,a

00001$:
	mov	a,[hl+0x04]
	and	a,#0x01
	bz	00002$

	mov	a,[hl+0x08]
	add	a,[hl+0x00]
	mov	[hl+0x08],a
	mov	a,[hl+0x09]
	addc	a,[hl+0x01]
	mov	[hl+0x09],a
	mov	a,[hl+0x0a]
	addc	a,[hl+0x02]
	mov	[hl+0x0a],a
	mov	a,[hl+0x0b]
	addc	a,[hl+0x03]
	mov	[hl+0x0b],a

00002$:
	clr1	cy
	mov	a,[hl+0x07]
	rorc	a,1
	mov	[hl+0x07],a
	mov	a,[hl+0x06]
	rorc	a,1
	mov	[hl+0x06],a
	mov	a,[hl+0x05]
	rorc	a,1
	mov	[hl+0x05],a
	mov	a,[hl+0x04]
	rorc	a,1
	mov	[hl+0x04],a

	clr1	cy
	mov	a,[hl+0x00]
	rolc	a,1
	mov	[hl+0x00],a
	mov	a,[hl+0x01]
	rolc	a,1
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	rolc	a,1
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	rolc	a,1
	mov	[hl+0x03],a

	dbnz	b,00001$

	mov	a,[hl+0x0a]
	mov	!___SDCC_k78k0_ret2,a
	mov	c,a
	mov	a,[hl+0x0b]
	mov	!___SDCC_k78k0_ret3,a
	mov	b,a

	mov	a,[hl+0x0c]
	mov	x,a
	mov	a,[hl+0x0d]
	movw	de,ax

	mov	a,[hl+0x11]
	mov	c,a
	mov	a,c
	mov	[hl+0x15],a
	mov	a,[hl+0x10]
	mov	c,a
	mov	a,c
	mov	[hl+0x14],a

	movw	ax,sp
	addw	ax,#0x0014
	movw	sp,ax
	mov	a,!___SDCC_k78k0_ret2
	mov	c,a
	mov	a,!___SDCC_k78k0_ret3
	mov	b,a
	mov	a,[hl+0x08]
	mov	x,a
	mov	a,[hl+0x09]
	ret
