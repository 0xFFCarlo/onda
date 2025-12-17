Assuming your VM stack pointer is in x20, points to the
next free 8-byte slot, and the “stack” grows upward (so
popping is -8), then “add top two elements and shorten
stack” (pop2 → push1, net -8 bytes) can be:

AArch64 instruction hex (32-bit words):
    ldr x0, [x20, #-8]! = 0xF85F8E80
    ldr x1, [x20, #-8]! = 0xF85F8E81
    add x0, x1, x0 = 0x8B000020
    str x0, [x20], #8 = 0xF8008680

If you’re writing bytes into a buffer on a little-endian
system, the bytes are:
    80 8E 5F F8
    81 8E 5F F8
    20 00 00 8B
    80 86 00 F8

Push a 1-byte constant:
    mov  w0, #1
    strb w0, [x20], #1
The bytes are:
    20 00 80 52
    80 06 00 38
