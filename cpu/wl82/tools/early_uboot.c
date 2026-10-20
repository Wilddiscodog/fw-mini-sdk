/* After stock uboot init. Bitbang 'U' at ~1000000 8N1 on PB3 and PB8,
 * then the packer redirects the last jump to call the app. */
static void delay(void)
{
    for (volatile int i = 0; i < 60; i++) {
    }
}

static void pins(unsigned int mask, int high)
{
    volatile unsigned int *pb = (volatile unsigned int *)0x50040;
    if (high)
        pb[0] |= mask;
    else
        pb[0] &= ~mask;
}

void early_uboot(void)
{
    volatile unsigned int *pb = (volatile unsigned int *)0x50040;
    volatile unsigned int *gate = (volatile unsigned int *)0x5101c;
    const unsigned int mask = (1u << 3) | (1u << 8);
    int n, b;
    unsigned char ch;

    gate[0] |= 0x80u;
    gate[0] &= ~0x40u;
    gate[3] |= 8u;
    pb[3] |= mask;
    pb[2] &= ~mask;
    pb[0] |= mask;

    for (n = 0; n < 16; n++) {
        ch = 'U';
        pins(mask, 0);
        delay();
        for (b = 0; b < 8; b++) {
            pins(mask, ch & 1u);
            ch >>= 1;
            delay();
        }
        pins(mask, 1);
        delay();
    }
    ((void (*)(void))0x1C05000)();
}
