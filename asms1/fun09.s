	.file	"fun09.cc"
	.text
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"r"
	.text
	.globl	_Z3funPKc
	.type	_Z3funPKc, @function
_Z3funPKc:
.LFB19:
	endbr64
	pushq	%rcx
	leaq	.LC0(%rip), %rsi
	call	fopen@PLT
	movq	%rax, %rdi
	orl	$-1, %eax
	testq	%rdi, %rdi
	je	.L1
	call	fclose@PLT
	xorl	%eax, %eax
.L1:
	popq	%rdx
	ret
.LFE19:
	.size	_Z3funPKc, .-_Z3funPKc
	.ident	"GCC: (Ubuntu 9.3.0-17ubuntu1~20.04) 9.3.0"
	.section	.note.GNU-stack,"",@progbits
