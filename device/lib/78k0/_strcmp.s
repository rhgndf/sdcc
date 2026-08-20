;--------------------------------------------------------------------------
;  _strcmp.s
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

	.globl _strcmp

	.area CODE

; AX = left, stack = right. Return the unsigned-byte difference in AX.
; DE is preserved and the stacked argument is removed before returning.
_strcmp:
	movw	hl,ax
	pop	bc
	pop	ax
	push	bc
	push	de
	movw	de,ax

00001$:
	mov	a,[hl]
	mov	x,a
	mov	a,[de]
	cmp	a,x
	bnz	00003$
	cmp	a,#0x00
	bz	00004$
	incw	hl
	incw	de
	br	00001$

00003$:
	xch	a,x
	sub	a,x
	mov	x,a
	mov	a,#0x00
	subc	a,#0x00

00004$:
	pop	de
	ret
