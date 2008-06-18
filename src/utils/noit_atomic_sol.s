/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#if defined(__sparcv9) || defined(__sparc)

	.section	".text"
	.align	8
	.skip	24
	.align	4

	.global noit_atomic_cas32
noit_atomic_cas32:
	cas	[%o0],%o2,%o1
	mov	%o1,%o0
	retl
	nop
	.type	noit_atomic_cas32,2
	.size	noit_atomic_cas32,(.-noit_atomic_cas32)
#endif
#if defined(__sparcv9)
	.section	".text"
	.align	8
	.skip	24
	.align	4

	.global noit_atomic_cas64
noit_atomic_cas64:
	casx	[%o0],%o2,%o1
	mov	%o1,%o0
	retl
	nop
	.type	noit_atomic_cas64,2
	.size	noit_atomic_cas64,(.-noit_atomic_cas64)

	.section	".text"
	.align	8
	.skip	24
	.align	4

	.global noit_atomic_casptr
noit_atomic_casptr:
	casx	[%o0],%o2,%o1
	mov	%o1,%o0
	retl
	nop
	.type	noit_atomic_casptr,2
	.size	noit_atomic_casptr,(.-noit_atomic_casptr)

#elif defined(__sparc)
	.section	".text"
	.align	8
	.skip	24
	.align	4

	.global noit_atomic_cas64
noit_atomic_cas64:
	cas	[%o0],%o2,%o1
	mov	%o1,%o0
	retl
	nop
	.type	noit_atomic_cas64,2
	.size	noit_atomic_cas64,(.-noit_atomic_cas64)

	.section	".text"
	.align	8
	.skip	24
	.align	4

	.global noit_atomic_casptr
noit_atomic_casptr:
	cas	[%o0],%o2,%o1
	mov	%o1,%o0
	retl
	nop
	.type	noit_atomic_casptr,2
	.size	noit_atomic_casptr,(.-noit_atomic_casptr)

#elif defined(__amd64) || defined(__i386)

#if defined(__amd64)
	.code64
#endif
	.globl noit_atomic_cas32
	.type noit_atomic_cas32, @function
	.globl noit_atomic_casptr
	.type noit_atomic_casptr, @function
	.globl noit_atomic_cas64
	.type noit_atomic_cas64, @function

	.section .text,"ax"
	.align 16
noit_atomic_cas64:
#if defined(__amd64)
	movq       %rdx,%rax
	lock
	cmpxchgq   %rsi,(%rdi)
#else
	pushl   %ebx
	pushl   %esi
	movl    0xc(%esp),%esi
	movl    0x18(%esp),%eax
	movl    0x1c(%esp),%edx
	movl    0x10(%esp),%ebx
	movl    0x14(%esp),%ecx
	lock
	cmpxchg8b (%esi)
	popl    %esi
	popl    %ebx
#endif
	ret        
	.size noit_atomic_cas64, . - noit_atomic_cas64

	.align 16
noit_atomic_cas32:
#if defined(__amd64)
	movl       %edx,%eax
	lock
	cmpxchgl   %esi,(%rdi)
#else
        movl    4(%esp), %edx
        movl    8(%esp), %ecx
        movl    12(%esp), %eax
        lock
        cmpxchgl %ecx, (%edx)
#endif
	ret        
	.size noit_atomic_cas32, . - noit_atomic_cas32
	.align 16
noit_atomic_casptr:
#if defined(__amd64)
	movq       %rdx,%rax
	lock
	cmpxchgq   %rsi,(%rdi)
#else
        movl    4(%esp), %edx
        movl    8(%esp), %ecx
        movl    12(%esp), %eax
        lock
        cmpxchgl %ecx, (%edx)
#endif
	ret        
	.size noit_atomic_casptr, . - noit_atomic_casptr

#else
#error "No atomics for this architecture?!"
#endif
