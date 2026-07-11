;--------------------------------------------------------------------------
;  _mulint.s
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

	.globl __mulint

	.area CODE

__mulint:
	movw	bc,ax
	pop	hl
	pop	ax
	push	hl
	push	de
	movw	de,ax

	; Only the low product and two cross-product bytes affect a 16-bit result.
	mov	a,e
	mov	x,a
	mov	a,c
	mulu	x
	mov	h,a
	mov	a,x
	mov	l,a

	mov	a,d
	mov	x,a
	mov	a,c
	mulu	x
	mov	a,x
	add	a,h
	mov	h,a

	mov	a,e
	mov	x,a
	mov	a,b
	mulu	x
	mov	a,x
	add	a,h
	mov	h,a

	movw	ax,hl
	pop	de
	ret
