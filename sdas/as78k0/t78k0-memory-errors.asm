	.title	78K0 RAM Boundary Rejection Test
	.module	memory_boundary_errors

	; LINK-ERROR: overlaps register space at 0xFEE0..0xFFFF

	; One byte beyond the uPD78F0034 high-speed RAM enters register bank 3.
	.area	DATA
	.ds	0x3e1
