	.file	"fun07.cc"
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
	movl	$2, %edx
	movl	%eax, %esi
.L6:
	cmpl	%esi, %edx
	jge	.L2
	movl	%edx, %ecx
	imull	%edx, %ecx
.L4:
	cmpl	%esi, %ecx
	jge	.L3
	addl	%edx, %ecx
	jmp	.L4
.L3:
	je	.L7
	incl	%edx
	jmp	.L6
.L2:
	decl	%eax
	setle	%al
	movzbl	%al, %eax
	jmp	.L1
.L7:
	movl	$1, %eax
.L1:
	popq	%rdx
	ret
.LFE20:
	.size	_Z3funPKc, .-_Z3funPKc
	.ident	"GCC: (Ubuntu 9.3.0-17ubuntu1~20.04) 9.3.0"
	.section	.note.GNU-stack,"",@progbits
