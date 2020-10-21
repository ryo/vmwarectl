/* $NetBSD: vmt.c,v 1.11 2015/04/23 23:23:00 pgoyette Exp $ */
/* $OpenBSD: vmt.c,v 1.11 2011/01/27 21:29:25 dtucker Exp $ */

/*
 * Copyright (c) 2007 David Crawshaw <david@zentus.com>
 * Copyright (c) 2008 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <string.h>
#ifdef __linux__
#include <stdint.h>
#endif
#include "vmport.h"

#define BACKDOOR_OP_I386(op, frame)		\
	__asm__ __volatile__ (			\
		"pushal;"			\
		"pushl %%eax;"			\
		"movl 0x18(%%eax), %%ebp;"	\
		"movl 0x14(%%eax), %%edi;"	\
		"movl 0x10(%%eax), %%esi;"	\
		"movl 0x0c(%%eax), %%edx;"	\
		"movl 0x08(%%eax), %%ecx;"	\
		"movl 0x04(%%eax), %%ebx;"	\
		"movl 0x00(%%eax), %%eax;"	\
		op				\
		"xchgl %%eax, 0x00(%%esp);"	\
		"movl %%ebp, 0x18(%%eax);"	\
		"movl %%edi, 0x14(%%eax);"	\
		"movl %%esi, 0x10(%%eax);"	\
		"movl %%edx, 0x0c(%%eax);"	\
		"movl %%ecx, 0x08(%%eax);"	\
		"movl %%ebx, 0x04(%%eax);"	\
		"popl 0x00(%%eax);"		\
		"popal;"			\
		:				\
		:"a"(frame)			\
	)

#define BACKDOOR_OP_AMD64(op, frame)		\
	__asm__ __volatile__ (			\
		"pushq %%rbp;			\n\t" \
		"pushq %%rax;			\n\t" \
		"movq 0x30(%%rax), %%rbp;	\n\t" \
		"movq 0x28(%%rax), %%rdi;	\n\t" \
		"movq 0x20(%%rax), %%rsi;	\n\t" \
		"movq 0x18(%%rax), %%rdx;	\n\t" \
		"movq 0x10(%%rax), %%rcx;	\n\t" \
		"movq 0x08(%%rax), %%rbx;	\n\t" \
		"movq 0x00(%%rax), %%rax;	\n\t" \
		op				"\n\t" \
		"xchgq %%rax, 0x00(%%rsp);	\n\t" \
		"movq %%rbp, 0x30(%%rax);	\n\t" \
		"movq %%rdi, 0x28(%%rax);	\n\t" \
		"movq %%rsi, 0x20(%%rax);	\n\t" \
		"movq %%rdx, 0x18(%%rax);	\n\t" \
		"movq %%rcx, 0x10(%%rax);	\n\t" \
		"movq %%rbx, 0x08(%%rax);	\n\t" \
		"popq 0x00(%%rax);		\n\t" \
		"popq %%rbp;			\n\t" \
		: /* No outputs. */ : "a" (frame) \
		  /* No pushal on amd64 so warn gcc about the clobbered registers. */ \
		: "rbx", "rcx", "rdx", "rdi", "rsi", "cc", "memory" \
	)

#define X86_IO_MAGIC		0x86	/* magic for upper 32bit of x7 */
#define X86_IO_W7_SIZE_MASK	__BITS(2, 0)
#define X86_IO_W7_SIZE(n)	__SHIFTIN((n), X86_IO_W7_SIZE_MASK)
#define X86_IO_W7_DIR		__BIT(2)
#define X86_IO_W7_WITH		__BIT(3)
#define X86_IO_W7_STR		__BIT(4)
#define X86_IO_W7_DF		__BIT(5)
#define X86_IO_W7_IMM_MASK	__BITS(12, 5)
#define X86_IO_W7_IMM(imm)	__SHIFTIN((imm), X86_IO_W7_IMM_MASK)

#define BACKDOOR_OP_AARCH64(op, frame)		\
	__asm__ __volatile__ (			\
		"ldp x0, x1, [%0, 8 * 0];	\n\t" \
		"ldp x2, x3, [%0, 8 * 2];	\n\t" \
		"ldp x4, x5, [%0, 8 * 4];	\n\t" \
		"ldr x6,     [%0, 8 * 6];	\n\t" \
		"mov x7, %1			\n\t" \
		"movk x7, %2, lsl #32;		\n\t" \
		"mrs xzr, mdccsr_el0;		\n\t" \
		"stp x0, x1, [%0, 8 * 0];	\n\t" \
		"stp x2, x3, [%0, 8 * 2];	\n\t" \
		"stp x4, x5, [%0, 8 * 4];	\n\t" \
		"str x6,     [%0, 8 * 6];	\n\t" \
		: \
		: "r" (frame), \
		  "r" (op), \
		  "i" (X86_IO_MAGIC) \
		: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "memory" \
	)

#if defined(__i386__)
#define BACKDOOR_OP(frame) BACKDOOR_OP_I386("inl %%dx, %%eax;", frame)
#elif defined(__amd64__)
#define BACKDOOR_OP(frame) BACKDOOR_OP_AMD64("inl %%dx, %%eax;", frame)
#elif defined(__aarch64__)
#define OP_AARCH64_IO	(X86_IO_W7_WITH | X86_IO_W7_DIR | X86_IO_W7_SIZE(2))
#define OP_AARCH64_IN	(X86_IO_W7_WITH | X86_IO_W7_STR | X86_IO_W7_DIR)
#define OP_AARCH64_OUT	(X86_IO_W7_WITH | X86_IO_W7_STR)
#define BACKDOOR_OP(frame) BACKDOOR_OP_AARCH64(OP_AARCH64_IO, frame)
#else
#error no BACKDOOR opcode in this arch
#endif

void
vm_cmd(struct vm_backdoor *frame)
{
	BACKDOOR_OP(frame);
}

void
vm_portcmd(struct vm_backdoor *frame)
{
	frame->eax.word = VM_MAGIC;
	frame->edx.word = VM_PORT_CMD;
	vm_cmd(frame);
}

int
vm_probe(void)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = ~VM_MAGIC;
	frame.ecx.part.low = VM_CMD_GET_VERSION;
	frame.ecx.part.high = 0xffff;
	vm_portcmd(&frame);

	if (frame.ebx.word == VM_MAGIC &&
	    frame.eax.word != 0xffffffff)
		return 0;

	return -1;
}
