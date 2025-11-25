	.file	"init.c"
	.option pic
	.option norelax
	.text
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align	3
.LC0:
	.string	"Working Test!!\n"
	.align	3
.LC1:
	.string	"\ninit: starting %d\n"
	.align	3
.LC2:
	.string	"init: fork failed\n"
	.align	3
.LC3:
	.string	"init: exec %s failed\n"
	.align	3
.LC4:
	.string	"init: wait returned an error\n"
	.section	.text.startup,"ax",@progbits
	.align	1
	.globl	main
	.type	main, @function
main:
	addi	sp,sp,-48
	li	a2,0
	li	a1,1
	li	a0,2
	sd	ra,40(sp)
	sd	s0,32(sp)
	sd	s2,16(sp)
	sd	s3,8(sp)
	sd	s1,24(sp)
	call	dev@plt
	li	a0,0
	call	dup@plt
	li	a0,0
	call	dup@plt
	lla	a0,.LC0
	call	printf@plt
	li	s0,0
	lla	s2,.LANCHOR1
	lla	s3,.LC1
.L2:
	lw	a5,0(s2)
	bgt	a5,s0,.L6
	call	shutdown@plt
	ld	ra,40(sp)
	ld	s0,32(sp)
	ld	s1,24(sp)
	ld	s2,16(sp)
	ld	s3,8(sp)
	li	a0,0
	addi	sp,sp,48
	jr	ra
.L6:
	mv	a1,s0
	mv	a0,s3
	call	printf@plt
	call	fork@plt
	mv	s1,a0
	bge	a0,zero,.L3
	lla	a0,.LC2
.L11:
	call	printf@plt
	j	.L10
.L3:
	bne	a0,zero,.L4
	lla	a1,.LANCHOR0
	slli	s0,s0,3
	add	s0,a1,s0
	ld	a0,16(s0)
	call	exec@plt
	ld	a1,16(s0)
	lla	a0,.LC3
	call	printf@plt
.L10:
	li	a0,1
	call	exit@plt
.L4:
	li	a0,0
	call	wait@plt
	beq	s1,a0,.L5
	bge	a0,zero,.L4
	lla	a0,.LC4
	j	.L11
.L5:
	addiw	s0,s0,1
	j	.L2
	.size	main, .-main
	.globl	counts
	.globl	tests
	.section	.rodata.str1.8
	.align	3
.LC5:
	.string	"brk"
	.align	3
.LC6:
	.string	"chdir"
	.align	3
.LC7:
	.string	"clone"
	.align	3
.LC8:
	.string	"close"
	.align	3
.LC9:
	.string	"dup"
	.align	3
.LC10:
	.string	"dup2"
	.align	3
.LC11:
	.string	"execve"
	.align	3
.LC12:
	.string	"exit"
	.align	3
.LC13:
	.string	"fork"
	.align	3
.LC14:
	.string	"fstat"
	.align	3
.LC15:
	.string	"getcwd"
	.align	3
.LC16:
	.string	"getdents"
	.align	3
.LC17:
	.string	"getpid"
	.align	3
.LC18:
	.string	"getppid"
	.align	3
.LC19:
	.string	"gettimeofday"
	.align	3
.LC20:
	.string	"mkdir_"
	.align	3
.LC21:
	.string	"mmap"
	.align	3
.LC22:
	.string	"munmap"
	.align	3
.LC23:
	.string	"open"
	.align	3
.LC24:
	.string	"openat"
	.align	3
.LC25:
	.string	"pipe"
	.align	3
.LC26:
	.string	"read"
	.align	3
.LC27:
	.string	"sleep"
	.align	3
.LC28:
	.string	"times"
	.align	3
.LC29:
	.string	"uname"
	.align	3
.LC30:
	.string	"unlink"
	.align	3
.LC31:
	.string	"wait"
	.align	3
.LC32:
	.string	"waitpid"
	.align	3
.LC33:
	.string	"write"
	.align	3
.LC34:
	.string	"yield"
	.globl	argv
	.align	3
.LC35:
	.string	"sh"
	.data
	.align	2
	.set	.LANCHOR1,. + 0
	.type	counts, @object
	.size	counts, 4
counts:
	.word	30
	.section	.data.rel.local,"aw"
	.align	3
	.set	.LANCHOR0,. + 0
	.type	argv, @object
	.size	argv, 16
argv:
	.dword	.LC35
	.dword	0
	.type	tests, @object
	.size	tests, 240
tests:
	.dword	.LC5
	.dword	.LC6
	.dword	.LC7
	.dword	.LC8
	.dword	.LC9
	.dword	.LC10
	.dword	.LC11
	.dword	.LC12
	.dword	.LC13
	.dword	.LC14
	.dword	.LC15
	.dword	.LC16
	.dword	.LC17
	.dword	.LC18
	.dword	.LC19
	.dword	.LC20
	.dword	.LC21
	.dword	.LC22
	.dword	.LC23
	.dword	.LC24
	.dword	.LC25
	.dword	.LC26
	.dword	.LC27
	.dword	.LC28
	.dword	.LC29
	.dword	.LC30
	.dword	.LC31
	.dword	.LC32
	.dword	.LC33
	.dword	.LC34
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
