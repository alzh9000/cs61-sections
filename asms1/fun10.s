	.file	"fun10.cc"
	.text
	.globl	_Z3funPKc
	.type	_Z3funPKc, @function
_Z3funPKc:
.LFB20:
	endbr64
	pushq	%rcx
	xorl	%edx, %edx
	xorl	%esi, %esi
	call	strtol@PLT
	movb	$1, %dl
	movl	%eax, %ecx
	shrl	$8, %ecx
	je	.L2
	leal	-1(%rax), %edx
	testl	%eax, %edx
	setne	%dl
.L2:
	movzbl	%dl, %eax
	popq	%rdx
	ret
.LFE20:
	.size	_Z3funPKc, .-_Z3funPKc
	.ident	"GCC: (Ubuntu 9.3.0-17ubuntu1~20.04) 9.3.0"
	.section	.note.GNU-stack,"",@progbits
