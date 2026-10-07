/* Original homebrew probe, no game assets. Exercises gpSP's Gen3 SIO protocol
 * through ARM instructions and real hardware registers, rather than sending
 * frontend-generated packets. RAM +0 = peer marker, +4 = receive count.
 * Playable link duel: A attacks on a rising edge; each side has 20 hit points.
 * Red bar is local HP, blue bar is peer HP; no copyrighted assets. */
.syntax unified
.arm
.text
.global _start
_start:
    b boot
    .space 188
boot:
    ldr sp, =0x03007f00
    ldr r8, =0x04000120
    ldr r9, =0x02000000
    /* Exercise watched ARM64 STR/STRB/STRH/STM paths without game data. */
    ldr r4, =0x020033f0
    ldr r0, =0x12345678
    str r0, [r4]
    ldr r1, [r4]
    cmp r0, r1
    bne watch_store_failed
    mov r0, #0xab
    strb r0, [r4, #3]
    ldr r2, =0xab345678
    ldr r1, [r4]
    cmp r1, r2
    bne watch_store_failed
    ldr r0, =0xcdef
    strh r0, [r4]
    ldr r2, =0xab34cdef
    ldr r1, [r4]
    cmp r1, r2
    bne watch_store_failed
    ldr r0, =0x87654321
    stmia r4, {r0}
    ldr r1, [r4]
    cmp r1, r0
    bne watch_store_failed
    mov r0, #0
    str r0, [r9]
    str r0, [r9, #4]
    str r0, [r9, #8]
    str r0, [r9, #12]
    str r0, [r9, #24]
    str r0, [r9, #28]
    mov r0, #20
    str r0, [r9, #16]
    str r0, [r9, #20]
    ldr r1, =0x04000000
    ldr r0, =0x0403
    strh r0, [r1]
    mov r0, #0
    strh r0, [r8, #20]
    ldr r0, =0x2003
    strh r0, [r8, #8]
    ldrh r0, [r8, #8]
    and r10, r0, #0x30
    ldr r6, =0xB9A0
    strh r6, [r8, #10]
    cmp r10, #0
    bne slave
    mov r7, #0
host_handshake:
    bl frame
    mov r0, r6
    bl transfer
    ldrh r0, [r8, #2]
    cmp r0, r6
    addeq r7, r7, #1
    cmp r7, #24
    blo host_handshake
    ldr r0, =0x8FFF
    bl transfer
    ldr r6, =0x1234
host_packets:
    bl frame
    mov r0, #0
    bl transfer
    mov r7, #8
host_words:
    ldr r0, [r9, #24]
    orr r0, r0, r6
    bl transfer
    ldrh r0, [r8, #2]
    bic r2, r0, #0x8000
    ldr r1, =0x5678
    cmp r2, r1
    bleq received
    subs r7, r7, #1
    bne host_words
    b host_packets
slave:
    ldr r6, =0x5678
slave_wait:
    /* Until the master is connected, keep sending its handshake token. */
    ldrh r0, [r8]
    ldr r1, =0x1234
    cmp r0, r1
    bne slave_wait
    strh r6, [r8, #10]
slave_packets:
    bl frame
    ldr r0, [r9, #24]
    orr r0, r0, r6
    strh r0, [r8, #10]
    ldrh r0, [r8]
    bic r2, r0, #0x8000
    ldr r1, =0x1234
    cmp r2, r1
    bleq received
    b slave_packets
received:
    and r3, r0, #0x8000
    str r3, [r9, #8]
    bic r0, r0, #0x8000
    str r0, [r9]
    ldr r2, [r9, #4]
    add r2, r2, #1
    str r2, [r9, #4]
    bx lr
transfer:
    strh r0, [r8, #10]
    ldr r0, =0x2083
    strh r0, [r8, #8]
1:
    ldrh r0, [r8, #8]
    tst r0, #0x80
    bne 1b
    bx lr
frame:
    push {r0-r7, lr}
    ldr r3, =0x04000006
2:
    ldrh r0, [r3]
    cmp r0, #160
    bhs 2b
3:
    ldrh r0, [r3]
    cmp r0, #160
    blo 3b
    /* Poll actual GBA KEYINPUT, not a frontend-generated network message. */
    ldr r3, =0x04000130
    ldrh r0, [r3]
    tst r0, #1
    moveq r0, #0x8000
    movne r0, #0
    str r0, [r9, #24]
    ldr r1, [r9, #28]
    str r0, [r9, #28]
    cmp r0, #0
    beq peer_attack
    cmp r1, #0
    bne peer_attack
    ldr r2, [r9, #20]
    cmp r2, #0
    subne r2, r2, #1
    str r2, [r9, #20]
peer_attack:
    ldr r0, [r9, #8]
    ldr r1, [r9, #12]
    str r0, [r9, #12]
    cmp r0, #0
    beq paint
    cmp r1, #0
    bne paint
    ldr r2, [r9, #16]
    cmp r2, #0
    subne r2, r2, #1
    str r2, [r9, #16]
paint:
    /* Two narrow bars keep rendering comfortably within a GBA frame budget. */
    ldr r7, =0x06002580 /* row 20 */
    mov r5, #0
row:
    mov r4, #0
    cmp r5, #4
    ldrlo r1, [r9, #16]
    ldrhs r1, [r9, #20]
    add r1, r1, r1, lsl #2
    mov r1, r1, lsl #1
pixel:
    mov r0, #0
    cmp r4, r1
    bhs write_pixel
    cmp r5, #4
    movlo r0, #31
    ldrhs r0, =0x7c00
write_pixel:
    strh r0, [r7], #2
    add r4, r4, #1
    cmp r4, #200
    blo pixel
    add r7, r7, #80
    add r5, r5, #1
    cmp r5, #4
    ldreq r7, =0x06004b00 /* row 40 */
    cmp r5, #8
    blo row
    ldr r7, =0x06007080 /* row 60 */
    ldr r1, [r9]
    mov r0, #0
    cmp r1, #0
    ldrne r0, =0x03e0
    mov r5, #0
indicator_row:
    mov r4, #0
indicator_pixel:
    strh r0, [r7], #2
    add r4, r4, #1
    cmp r4, #16
    blo indicator_pixel
    add r7, r7, #448
    add r5, r5, #1
    cmp r5, #8
    blo indicator_row
    pop {r0-r7, pc}
    .ltorg

watch_store_failed:
    ldr r0, =0xdead0001
    str r0, [r9]
    b watch_store_failed
