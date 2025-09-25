	.file	"init.c"
	.option pic
	.option norelax
	.text
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align	3
.LC0:
	.string	"init: fork failed\n"
	.align	3
.LC1:
	.string	"init: exec %s failed\n"
	.align	3
.LC2:
	.string	"init: wait returned an error\n"
	.section	.text.startup,"ax",@progbits
	.align	1
	.globl	main
	.type	main, @function
main:
	addi	sp,sp,-32
	li	a2,0
	li	a1,1
	li	a0,2
	sd	ra,24(sp)
	sd	s0,16(sp)
	sd	s2,0(sp)
	sd	s1,8(sp)
	call	dev@plt
	li	a0,0
	call	dup@plt
	li	a0,0
	call	dup@plt
	li	s0,0
	li	s2,5
.L5:
	call	fork@plt
	mv	s1,a0
	bge	a0,zero,.L2
	lla	a0,.LC0
.L11:
	call	printf@plt
	j	.L10
.L2:
	bne	a0,zero,.L3
	lla	a1,.LANCHOR0
	slli	s0,s0,3
	add	s0,a1,s0
	ld	a0,16(s0)
	call	exec@plt
	ld	a1,16(s0)
	lla	a0,.LC1
	call	printf@plt
.L10:
	li	a0,1
	call	exit@plt
.L3:
	li	a0,0
	call	wait@plt
	beq	s1,a0,.L4
	bge	a0,zero,.L3
	lla	a0,.LC2
	j	.L11
.L4:
	addiw	s0,s0,1
	bne	s0,s2,.L5
	call	shutdown@plt
	ld	ra,24(sp)
	ld	s0,16(sp)
	ld	s1,8(sp)
	ld	s2,0(sp)
	li	a0,0
	addi	sp,sp,32
	jr	ra
	.size	main, .-main
	.globl	tests
	.section	.rodata.str1.8
	.align	3
.LC3:
	.string	"getcwd"
	.align	3
.LC4:
	.string	"write"
	.align	3
.LC5:
	.string	"getpid"
	.align	3
.LC6:
	.string	"times"
	.align	3
.LC7:
	.string	"uname"
	.globl	argv
	.align	3
.LC8:
	.string	"sh"
	.section	.data.rel.local,"aw"
	.align	3
	.set	.LANCHOR0,. + 0
	.type	argv, @object
	.size	argv, 16
argv:
	.dword	.LC8
	.dword	0
	.type	tests, @object
	.size	tests, 40
tests:
	.dword	.LC3
	.dword	.LC4
	.dword	.LC5
	.dword	.LC6
	.dword	.LC7
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
