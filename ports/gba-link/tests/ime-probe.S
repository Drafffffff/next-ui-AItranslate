/* Original test: reserved IME bits must not admit IRQs into a critical section. */
.syntax unified
.arm
.text
.global _start
_start:
 b boot
 .space 188
boot:
 mov r0, #0xd2
 msr cpsr_c, r0
 ldr sp, =0x03007fa0
 mov r0, #0x1f
 msr cpsr_c, r0
 ldr sp, =0x03007e00
 ldr r8, =0x02000000
 mov r0, #0
 str r0, [r8]
 str r0, [r8, #16]
 adr r0, irq_handler
 ldr r1, =0x03007ffc
 str r0, [r1]
 ldr r9, =0x04000208
 ldr r0, =0x0208
 strh r0, [r9]
 ldrh r0, [r9]
 str r0, [r8, #8]
 ldr r1, =0x04000200
 mov r0, #8
 strh r0, [r1]
 ldr r1, =0x04000100
 ldr r0, =0x00c0ff00
 str r0, [r1]
 ldr r5, =0x4000
 ldr r1, =0x04000202
wait_pending:
 ldrh r0, [r1]
 tst r0, #8
 bne got_pending
 subs r5, r5, #1
 bne wait_pending
got_pending:
 str r0, [r8, #12]
 ldr r0, [r8, #16]
 cmp r0, #0
 movne r0, #1
 bne fail
 ldr r0, [r8, #8]
 cmp r0, #0
 movne r0, #2
 bne fail
 ldr r0, =0x0209
 strh r0, [r9]
 ldrh r0, [r9]
 cmp r0, #1
 movne r0, #3
 bne fail
 ldr r5, =0x4000
wait_irq:
 ldr r0, [r8, #16]
 cmp r0, #1
 beq irq_received
 subs r5, r5, #1
 bne wait_irq
 mov r0, #4
 b fail
irq_received:
 mov r0, #0
 strh r0, [r9]
 mov r0, #2
 strb r0, [r9]
 ldrh r0, [r9]
 cmp r0, #0
 movne r0, #5
 bne fail
 ldr r0, =0xffff0001
 str r0, [r9]
 ldr r0, [r9]
 cmp r0, #1
 movne r0, #6
 bne fail
 mov r0, #0
 strh r0, [r9]
 ldr r0, =0x50415353
 str r0, [r8]
done:
 b done
fail:
 str r0, [r8, #4]
 ldr r0, =0x4641494c
 str r0, [r8]
 b done
irq_handler:
 ldr r0, =0x04000102
 mov r1, #0
 strh r1, [r0]
 ldr r0, =0x04000202
 mov r1, #8
 strh r1, [r0]
 ldr r0, =0x02000010
 ldr r1, [r0]
 add r1, r1, #1
 str r1, [r0]
 bx lr
.ltorg
