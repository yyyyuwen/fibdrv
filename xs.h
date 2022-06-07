#ifndef FIBDRV_XS_H
#define FIBDRV_XS_H

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#define MAX_STR_LEN_BITS (54)
#define MAX_STR_LEN ((1UL << MAX_STR_LEN_BITS) - 1)

#define LARGE_STRING_LEN 256

#define xs_literal_empty() \
    (xs) { .space_left = 15 }

#define xs_tmp(x) xs_new(&xs_literal_empty(), x)

typedef union {
    /* allow strings up to 15 bytes to stay on the stack
     * use the last byte as a null terminator and to store flags
     * much like fbstring:
     * https://github.com/facebook/folly/blob/master/folly/docs/FBString.md
     */
    char data[16];

    struct {
        uint8_t filler[15],
            /* how many free bytes in this stack allocated string
             * same idea as fbstring
             */
            space_left : 4,
            /* if it is on heap, set to 1 */
            is_ptr : 1, is_large_string : 1, flag2 : 1, flag3 : 1;
    };

    /* heap allocated */
    struct {
        char *ptr;
        /* supports strings up to 2^MAX_STR_LEN_BITS - 1 bytes */
        size_t size : MAX_STR_LEN_BITS,
                      /* capacity is always a power of 2 (unsigned)-1 */
                      capacity : 6;
        /* the last 4 bits are important flags */
    };
} xs;

static inline bool xs_is_ptr(const xs *x)
{
    return x->is_ptr;
}
static inline bool xs_is_large_string(const xs *x)
{
    return x->is_large_string;
}
static inline size_t xs_size(const xs *x)
{
    return xs_is_ptr(x) ? x->size : 15 - x->space_left;
}
static inline char *xs_data(const xs *x)
{
    if (!xs_is_ptr(x))
        return (char *) x->data;

    if (xs_is_large_string(x)) {
        return (char *) (x->ptr + 4);
    }
    return (char *) x->ptr;
}
static inline size_t xs_capacity(const xs *x)
{
    return xs_is_ptr(x) ? ((size_t) 1 << x->capacity) - 1 : 15;
}
static inline void xs_set_ref_count(const xs *x, int val)
{
    *((int *) ((size_t) x->ptr)) = val;
}
static inline void xs_inc_ref_count(const xs *x)
{
    if (xs_is_large_string(x))
        ++(*(int *) ((size_t) x->ptr));
}
static inline int xs_dec_ref_count(const xs *x)
{
    if (!xs_is_large_string(x))
        return 0;
    return --(*(int *) ((size_t) x->ptr));
}

static inline int xs_get_ref_count(const xs *x)
{
    if (!xs_is_large_string(x))
        return 0;
    return *(int *) ((size_t) x->ptr);
}

static inline xs *xs_newempty(xs *x)
{
    *x = xs_literal_empty();
    return x;
}

static inline xs *xs_free(xs *x)
{
    if (xs_is_ptr(x) && xs_dec_ref_count(x) <= 0)
        kfree(x->ptr);
    return xs_newempty(x);
}

xs *xs_new(xs *x, const void *p);
xs *xs_grow(xs *x, size_t len);
xs *xs_concat(xs *string, const xs *prefix, const xs *suffix);
xs *xs_trim(xs *x, const char *trimset);
void xs_copy(xs *dest, xs *src);
void xs_trivia_test(void);

#endif /* FIBDRV_XS_H */
