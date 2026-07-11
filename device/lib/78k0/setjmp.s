; 78K0 setjmp/longjmp support.
;
; jmp_buf layout:
;   0..1  return address
;   2..3  stack pointer after the setjmp return address is removed
;   4..5  callee-preserved DE

	.area CODE

	.globl ___setjmp

___setjmp:
	movw	hl,ax

	; Preserve DE in the saved calling context.
	movw	ax,de
	mov	[hl+0x05],a
	mov	a,x
	mov	[hl+0x04],a

	; Save the return address and the caller's stack pointer.
	pop	ax
	movw	bc,ax
	mov	[hl+0x01],a
	mov	a,x
	mov	[hl],a
	movw	ax,sp
	mov	[hl+0x03],a
	mov	a,x
	mov	[hl+0x02],a

	; Recreate the call frame and return zero in AX.
	movw	ax,bc
	push	ax
	movw	ax,#0x0000
	ret

	.globl _longjmp

_longjmp:
	; The jump buffer is in AX; the requested result is above the return
	; address on the current stack.
	movw	bc,ax
	movw	ax,sp
	movw	hl,ax
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]
	movw	de,ax

	; Restore SP and arrange for RET to branch to setjmp's caller.
	movw	ax,bc
	movw	hl,ax
	mov	a,[hl+0x02]
	mov	x,a
	mov	a,[hl+0x03]
	movw	sp,ax
	mov	a,[hl]
	mov	x,a
	mov	a,[hl+0x01]
	push	ax

	; Keep the requested result in BC while restoring preserved DE.
	movw	ax,de
	movw	bc,ax
	mov	a,[hl+0x04]
	mov	x,a
	mov	a,[hl+0x05]
	movw	de,ax

	movw	ax,bc
	cmpw	ax,#0x0000
	bnz	001$
	movw	ax,#0x0001
001$:
	ret
