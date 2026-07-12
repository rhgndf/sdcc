;--------------------------------------------------------------------------
;  _divslong.s
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

	.globl __divslong
	.globl __modslong
	.globl __divulong
	.globl __modulong

	.area CODE

__divslong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000a
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x00
	br	!__divmodslong

__modslong:
	push	ax
	push	de
	movw	ax,sp
	subw	ax,#0x000a
	movw	sp,ax
	movw	hl,ax
	mov	a,#0x01

__divmodslong:
	; Locals: magnitude/result [0..3], divisor [4..7], mode [8], sign [9].
	mov	[hl+0x08],a
	mov	a,#0x00
	mov	[hl+0x09],a

	; Save the register argument.
	mov	a,[hl+0x0c]
	mov	x,a
	mov	a,[hl+0x0d]
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a
	mov	a,c
	mov	[hl+0x02],a
	mov	a,b
	mov	[hl+0x03],a

	; Save the stack argument.
	mov	a,[hl+0x10]
	mov	[hl+0x04],a
	mov	a,[hl+0x11]
	mov	[hl+0x05],a
	mov	a,[hl+0x12]
	mov	[hl+0x06],a
	mov	a,[hl+0x13]
	mov	[hl+0x07],a

	; Convert the dividend to its unsigned magnitude. The remainder sign is
	; the dividend sign; the quotient sign is toggled below for the divisor.
	mov	a,[hl+0x03]
	and	a,#0x80
	bz	00001$
	mov	a,#0x01
	mov	[hl+0x09],a
	mov	a,[hl]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl],a
	mov	a,[hl+0x01]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x03],a

00001$:
	mov	a,[hl+0x07]
	and	a,#0x80
	bz	00003$
	mov	a,[hl+0x04]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl+0x04],a
	mov	a,[hl+0x05]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x05],a
	mov	a,[hl+0x06]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x06],a
	mov	a,[hl+0x07]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x07],a

	mov	a,[hl+0x08]
	cmp	a,#0x00
	bnz	00003$
	mov	a,[hl+0x09]
	xor	a,#0x01
	mov	[hl+0x09],a

00003$:
	; Call the unsigned magnitude helper, which also provides the DIVUW path.
	mov	a,[hl+0x06]
	mov	x,a
	mov	a,[hl+0x07]
	push	ax
	mov	a,[hl+0x04]
	mov	x,a
	mov	a,[hl+0x05]
	push	ax
	mov	a,[hl+0x08]
	cmp	a,#0x00
	bnz	00004$
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	call	!__divulong
	br	!00005$
00004$:
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	call	!__modulong

00005$:
	; The unsigned helper cleans its stack argument and returns in BC:AX.
	movw	de,ax
	movw	ax,sp
	movw	hl,ax
	mov	a,e
	mov	[hl],a
	mov	a,d
	mov	[hl+0x01],a
	mov	a,c
	mov	[hl+0x02],a
	mov	a,b
	mov	[hl+0x03],a

	mov	a,[hl+0x09]
	cmp	a,#0x00
	bz	00006$
	mov	a,[hl]
	xor	a,#0xff
	add	a,#0x01
	mov	[hl],a
	mov	a,[hl+0x01]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x01],a
	mov	a,[hl+0x02]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x02],a
	mov	a,[hl+0x03]
	xor	a,#0xff
	addc	a,#0x00
	mov	[hl+0x03],a

00006$:
	; Restore DE and move the return address past the callee-cleaned argument.
	mov	a,[hl+0x0a]
	mov	x,a
	mov	a,[hl+0x0b]
	movw	de,ax
	mov	a,[hl+0x0f]
	mov	[hl+0x13],a
	mov	a,[hl+0x0e]
	mov	[hl+0x12],a
	movw	ax,sp
	addw	ax,#0x0012
	movw	sp,ax
	mov	a,[hl+0x02]
	mov	c,a
	mov	a,[hl+0x03]
	mov	b,a
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	ret
