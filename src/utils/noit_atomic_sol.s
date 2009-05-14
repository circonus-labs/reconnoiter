/*
 * Copyright (c) 2005-2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *      of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
