	.file	"fun12.cc"
	.text
	.globl	_Z3funPKc
	.type	_Z3funPKc, @function
_Z3funPKc:
.LFB27:
	endbr64
	xorl	%eax, %eax
	orq	$-1, %rcx
	movq	%rdi, %rdx
	repnz scasb
	xorl	%eax, %eax
	notq	%rcx
	decq	%rcx
	movslq	%ecx, %rsi
	addq	%rdx, %rsi
.L3:
	movb	(%rdx,%rax), %dil
	movl	%eax, %r8d
	testb	%dil, %dil
	je	.L2
	leaq	1(%rax), %r9
	notq	%rax
	cmpb	(%rsi,%rax), %dil
	jne	.L2
	movq	%r9, %rax
	jmp	.L3
.L2:
	cmpl	$4, %ecx
	setle	%al
	cmpl	%ecx, %r8d
	setne	%dl
	orl	%edx, %eax
	movzbl	%al, %eax
	ret
.LFE27:
	.size	_Z3funPKc, .-_Z3funPKc
	.ident	"GCC: (Ubuntu 9.3.0-17ubuntu1~20.04) 9.3.0"
	.section	.note.GNU-stack,"",@progbits
