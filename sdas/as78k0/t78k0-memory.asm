	.title	78K0 RAM Boundary Test
	.module	memory_boundary

	; At the uPD78F0034 default DATA base, 0x3e0 bytes end exactly where
	; the general-purpose register banks begin.
	.area	DATA
	.ds	0x3e0
