# Copyright (C) 2017 Michael R. Tirado <mtirado418@gmail.com> -- GPLv3+
#
# This program is libre software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details. You should have
# received a copy of the GNU General Public License version 3
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# TODO: "blit" variations for copying rectangles in one call
#
# NOTE: to be called from C code using cdecl calling convention

#.extern printf

.align 16
.data
#message:
#	.ascii "Hai World\n\0"

#	push $message
#	call printf

#void x86_sse2_xmmcpy_128(void *dest, void *src, unsigned int count)
.global x86_sse2_xmmcpy_128
x86_sse2_xmmcpy_128:

	pushl %ebp
	movl  %esp, %ebp

	movl  16(%ebp),    %edx  /* count   */
	movl  12(%ebp),    %eax  /* src     */
	movl   8(%ebp),    %ebx  /* dst     */
	xorl     %ecx,     %ecx  /* counter */

cont_loop_128:

	movdqa  (%eax),     %xmm0
	movntdq  %xmm0,    (%ebx)
	inc      %ecx
	add      $16,       %eax
	add      $16,       %ebx
	cmp      %edx,      %ecx
	jne      cont_loop_128

	popl  %ebp
	ret


#void x86_sse2_xmmcpy_512(void *dest, void *src, unsigned int count)
.global x86_sse2_xmmcpy_512
x86_sse2_xmmcpy_512:

	pushl %ebp
	movl  %esp, %ebp

	movl  16(%ebp),    %edx  /* count   */
	movl  12(%ebp),    %eax  /* src     */
	movl   8(%ebp),    %ebx  /* dst     */
	xorl     %ecx,     %ecx  /* counter */

cont_loop_512:

	movdqa    (%eax),     %xmm0
	movdqa  16(%eax),     %xmm1
	movdqa  32(%eax),     %xmm2
	movdqa  48(%eax),     %xmm3
	movntdq    %xmm0,    (%ebx)
	movntdq    %xmm1,  16(%ebx)
	movntdq    %xmm2,  32(%ebx)
	movntdq    %xmm3,  48(%ebx)
	inc        %ecx
	add        $64,       %eax
	add        $64,       %ebx
	cmp        %edx,      %ecx
	jne        cont_loop_512

	popl  %ebp
	ret

#void x86_sse2_xmmcpy_1024(void *dest, void *src, unsigned int count)
.global x86_sse2_xmmcpy_1024
x86_sse2_xmmcpy_1024:

	pushl %ebp
	movl  %esp, %ebp

	movl    16(%ebp),    %edx  /* count   */
	movl    12(%ebp),    %eax  /* src     */
	movl     8(%ebp),    %ebx  /* dst     */
	xorl       %ecx,     %ecx  /* counter */

cont_loop_1024:

	movdqa    (%eax),     %xmm0
	movdqa  16(%eax),     %xmm1
	movdqa  32(%eax),     %xmm2
	movdqa  48(%eax),     %xmm3
	movdqa  64(%eax),     %xmm4
	movdqa  80(%eax),     %xmm5
	movdqa  96(%eax),     %xmm6
	movdqa 112(%eax),     %xmm7
	movntdq    %xmm0,    (%ebx)
	movntdq    %xmm1,  16(%ebx)
	movntdq    %xmm2,  32(%ebx)
	movntdq    %xmm3,  48(%ebx)
	movntdq    %xmm4,  64(%ebx)
	movntdq    %xmm5,  80(%ebx)
	movntdq    %xmm6,  96(%ebx)
	movntdq    %xmm7, 112(%ebx)
	inc        %ecx
	add        $128,      %eax
	add        $128,      %ebx
	cmp        %edx,      %ecx
	jne        cont_loop_1024

	pop  %ebp
	ret


# slow copy for benchmarking
#void x86_slocpy_512(void *dest, void *src, unsigned int count)
.global x86_slocpy_512
x86_slocpy_512:

	pushl %ebp
	movl  %esp, %ebp

	movl    16(%ebp),    %edx  /* count   */
	movl    12(%ebp),    %eax  /* src     */
	movl     8(%ebp),    %ebx  /* dst     */
	xorl       %ecx,     %ecx  /* counter */

cont_loop_slo512:

	pushl %edx

	movl   (%eax),      %edx
	movl    %edx,      (%ebx)
	movl  4(%eax),      %edx
	movl    %edx,     4(%ebx)
	movl  8(%eax),      %edx
	movl    %edx,     8(%ebx)
	movl 12(%eax),      %edx
	movl    %edx,    12(%ebx)
	movl 16(%eax),      %edx
	movl    %edx,    16(%ebx)
	movl 20(%eax),      %edx
	movl    %edx,    20(%ebx)
	movl 24(%eax),      %edx
	movl    %edx,    24(%ebx)
	movl 28(%eax),      %edx
	movl    %edx,    28(%ebx)
	movl 32(%eax),      %edx
	movl    %edx,    32(%ebx)
	movl 36(%eax),      %edx
	movl    %edx,    36(%ebx)
	movl 40(%eax),      %edx
	movl    %edx,    40(%ebx)
	movl 44(%eax),      %edx
	movl    %edx,    44(%ebx)
	movl 48(%eax),      %edx
	movl    %edx,    48(%ebx)
	movl 52(%eax),      %edx
	movl    %edx,    52(%ebx)
	movl 56(%eax),      %edx
	movl    %edx,    56(%ebx)
	movl 60(%eax),      %edx
	movl    %edx,    60(%ebx)

	popl    %edx

	inc     %ecx
	add     $64,          %eax
	add     $64,          %ebx
	cmp     %edx,         %ecx
	jne     cont_loop_slo512

	popl  %ebp
	ret

