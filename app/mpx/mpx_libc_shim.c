/*
 * app/mpx/mpx_libc_shim.c - pi32v2 对齐安全的 libc 覆盖(mpx/WAMR 路径)
 *
 * 杰理 libc 的 memcpy/memmove/memset/vsnprintf 对非对齐地址做字宽访问,
 * pi32v2 直接总线错误(misalign)。这里提供同名强符号:
 *   - memcpy/memmove/memset: 头尾按字节、中间按字,任意地址安全且对齐时全速
 *   - vsnprintf/snprintf:    逐字节实现(覆盖 WAMR loader 的 %s/%.*s/%d/%u/%x
 *     /%p/%c 等用法)
 * 链接时强符号胜过 libc.a 的弱引用;闭源库内部调用不受影响。
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef unsigned long jl_ulong;
typedef long jl_long;

/* ---------------- 对齐安全 内存原语 ---------------- */

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (((uintptr_t)d | (uintptr_t)s) & 3) {
        while (n--)
            *d++ = *s++;
        return dst;
    }
    while ((uintptr_t)d & 3) {
        if (!n--)
            return dst;
        *d++ = *s++;
    }
    uint32_t *dw = (uint32_t *)d;
    const uint32_t *sw = (const uint32_t *)s;
    while (n >= 4) {
        *dw++ = *sw++;
        n -= 4;
    }
    d = (uint8_t *)dw;
    s = (const uint8_t *)sw;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s || d >= s + n) {
        return memcpy(dst, src, n);
    }
    /* 重叠且 dst > src: 从尾向前 */
    d += n;
    s += n;
    if (((uintptr_t)d | (uintptr_t)s) & 3) {
        while (n--)
            *--d = *--s;
        return dst;
    }
    while ((uintptr_t)d & 3) {
        if (!n--)
            return dst;
        *--d = *--s;
    }
    uint32_t *dw = (uint32_t *)d;
    const uint32_t *sw = (const uint32_t *)s;
    while (n >= 4) {
        *--dw = *--sw;
        n -= 4;
    }
    d = (uint8_t *)dw;
    s = (const uint8_t *)sw;
    while (n--)
        *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;
    while ((uintptr_t)d & 3) {
        if (!n--)
            return dst;
        *d++ = v;
    }
    uint32_t vw = (uint32_t)(v | (v << 8) | (v << 16) | (v << 24));
    uint32_t *dw = (uint32_t *)d;
    while (n >= 4) {
        *dw++ = vw;
        n -= 4;
    }
    d = (uint8_t *)dw;
    while (n--)
        *d++ = v;
    return dst;
}

/* ---------------- 逐字节 vsnprintf ---------------- */

typedef struct mpx_pf {
    char *out;
    size_t cap; /* 含 NUL 的总容量; cap==0 表示无缓冲(计数) */
    size_t len;
} mpx_pf_t;

static void mpx_putc(mpx_pf_t *p, char c)
{
    if (p->cap == 0 || p->len + 1 < p->cap) {
        if (p->cap != 0) {
            p->out[p->len] = c;
        }
    }
    p->len++;
}

static void mpx_puts(mpx_pf_t *p, const char *s)
{
    while (*s) {
        mpx_putc(p, *s++);
    }
}

static void mpx_putn(mpx_pf_t *p, const char *s, size_t n)
{
    while (n--) {
        mpx_putc(p, *s++);
    }
}

static void mpx_putu(mpx_pf_t *p, jl_ulong v, unsigned base, int upper, int width,
                     char pad, int is_neg)
{
    char tmp[24];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    do {
        tmp[i++] = digits[v % base];
        v /= base;
    } while (v);
    if (is_neg) {
        tmp[i++] = '-';
    }
    while (width > i) {
        mpx_putc(p, pad);
        width--;
    }
    while (i--) {
        mpx_putc(p, tmp[i]);
    }
}

int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap)
{
    mpx_pf_t p;
    p.out = out;
    p.cap = cap;
    p.len = 0;

    while (*fmt) {
        if (*fmt != '%') {
            mpx_putc(&p, *fmt++);
            continue;
        }
        fmt++;
        char pad = ' ';
        int width = 0;
        int long_mod = 0;
        int dot_star = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                dot_star = 1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    fmt++;
                }
            }
        }
        while (*fmt == 'l') {
            long_mod++;
            fmt++;
        }
        char spec = *fmt ? *fmt++ : 0;
        switch (spec) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            mpx_puts(&p, s ? s : "(null)");
            break;
        }
        case '.': { /* "%.*s" 后面还有 s, 这里处理 ".**s" 组合 */
            const char *s = va_arg(ap, const char *);
            int sl = va_arg(ap, int);
            if (sl < 0) {
                sl = 0;
            }
            mpx_putn(&p, s ? s : "(null)", (size_t)sl);
            if (*fmt == 's') {
                fmt++;
            }
            break;
        }
        case 'd': {
            jl_long v = long_mod >= 2 ? (jl_long)va_arg(ap, long long)
                                      : long_mod == 1 ? (jl_long)va_arg(ap, long)
                                                      : (jl_long)va_arg(ap, int);
            int neg = v < 0;
            jl_ulong u = neg ? (jl_ulong)(-v) : (jl_ulong)v;
            mpx_putu(&p, u, 10, 0, width, pad, neg);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            jl_ulong v = long_mod >= 2 ? (jl_ulong)va_arg(ap, unsigned long long)
                                       : long_mod == 1
                                             ? (jl_ulong)va_arg(ap, unsigned long)
                                             : (jl_ulong)va_arg(ap, unsigned int);
            mpx_putu(&p, v, spec == 'u' ? 10 : 16, spec == 'X', width, pad, 0);
            break;
        }
        case 'p': {
            void *pv = va_arg(ap, void *);
            mpx_puts(&p, "0x");
            mpx_putu(&p, (jl_ulong)(uintptr_t)pv, 16, 0, 8, '0', 0);
            break;
        }
        case 'c':
            mpx_putc(&p, (char)va_arg(ap, int));
            break;
        case '%':
            mpx_putc(&p, '%');
            break;
        case 0:
            goto done;
        default:
            mpx_putc(&p, '%');
            mpx_putc(&p, spec);
            break;
        }
        (void)dot_star;
    }
done:
    if (cap != 0) {
        size_t end = p.len < cap ? p.len : cap - 1;
        out[end] = 0;
        return (int)end;
    }
    return (int)p.len;
}

int snprintf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n;
}
