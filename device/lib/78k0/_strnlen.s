;--------------------------------------------------------------------------
;  _strnlen.s
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

	.globl _strnlen

	.area CODE

_strnlen:
	movw	hl,ax
	pop	ax
	pop	bc
	push	ax
	push	de
	movw	de,#0x0000
	mov	a,c
	or	a,b
	bz	00002$
	mov	a,c
	cmp	a,#0x00
	bz	00001$
	inc	b

00001$:
	mov	a,[hl]
	cmp	a,#0x00
	bz	00002$
	incw	hl
	incw	de
	dbnz	c,00001$
	dbnz	b,00001$

00002$:
	movw	ax,de
	pop	de
	ret
