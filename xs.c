#include "xs.h"

static void xs_allocate_data(xs *x, size_t len, bool reallocate)
{
    size_t n = 1 << x->capacity;

    /* Medium string */
    if (len < LARGE_STRING_LEN) {
        x->ptr = reallocate ? krealloc(x->ptr, n, GFP_KERNEL)
                            : kmalloc(n, GFP_KERNEL);
        return;
    }

    /*
     * Large string
     */
    x->is_large_string = 1;

    /* The extra 4 bytes are used to store the reference count */
    x->ptr = reallocate ? krealloc(x->ptr, n + 4, GFP_KERNEL)
                        : kmalloc(n + 4, GFP_KERNEL);

    xs_set_ref_count(x, 1);
}

xs *xs_new(xs *x, const void *p)
{
    *x = xs_literal_empty();
    size_t len = strlen(p) + 1;
    if (len > 16) {
        x->capacity = ilog2(len) + 1;
        x->size = len - 1;
        x->is_ptr = true;
        xs_allocate_data(x, x->size, 0);
        memcpy(xs_data(x), p, len);
    } else {
        memcpy(x->data, p, len);
        x->space_left = 15 - (len - 1);
    }
    return x;
}

/* grow up to specified size */
xs *xs_grow(xs *x, size_t len)
{
    char buf[16];

    if (len <= xs_capacity(x))
        return x;

    /* Backup first */
    if (!xs_is_ptr(x))
        memcpy(buf, x->data, 16);

    x->is_ptr = true;
    x->capacity = ilog2(len) + 1;

    if (xs_is_ptr(x)) {
        xs_allocate_data(x, len, 1);
    } else {
        xs_allocate_data(x, len, 0);
        memcpy(xs_data(x), buf, 16);
    }
    return x;
}

static bool xs_cow_lazy_copy(xs *x, char **data)
{
    if (xs_get_ref_count(x) <= 1)
        return false;

    /*
     * Lazy copy
     */
    xs_dec_ref_count(x);
    xs_allocate_data(x, x->size, 0);

    if (data) {
        memcpy(xs_data(x), *data, x->size);

        /* Update the newly allocated pointer */
        *data = xs_data(x);
    }
    return true;
}

xs *xs_concat(xs *string, const xs *prefix, const xs *suffix)
{
    size_t pres = xs_size(prefix), sufs = xs_size(suffix),
           size = xs_size(string), capacity = xs_capacity(string);

    char *pre = xs_data(prefix), *suf = xs_data(suffix),
         *data = xs_data(string);

    xs_cow_lazy_copy(string, &data);

    if (size + pres + sufs <= capacity) {
        memmove(data + pres, data, size);
        memcpy(data, pre, pres);
        memcpy(data + pres + size, suf, sufs + 1);

        if (xs_is_ptr(string))
            string->size = size + pres + sufs;
        else
            string->space_left = 15 - (size + pres + sufs);
    } else {
        xs tmps = xs_literal_empty();
        xs_grow(&tmps, size + pres + sufs);
        char *tmpdata = xs_data(&tmps);
        memcpy(tmpdata + pres, data, size);
        memcpy(tmpdata, pre, pres);
        memcpy(tmpdata + pres + size, suf, sufs + 1);
        xs_free(string);
        *string = tmps;
        string->size = size + pres + sufs;
    }
    return string;
}

xs *xs_trim(xs *x, const char *trimset)
{
    if (!trimset[0])
        return x;

    char *dataptr = xs_data(x), *orig = dataptr;

    if (xs_cow_lazy_copy(x, &dataptr))
        orig = dataptr;

    /* similar to strspn/strpbrk but it operates on binary data */
    uint8_t mask[32] = {0};

#define check_bit(byte) (mask[(uint8_t) byte / 8] & 1 << (uint8_t) byte % 8)
#define set_bit(byte) (mask[(uint8_t) byte / 8] |= 1 << (uint8_t) byte % 8)

    size_t i, slen = xs_size(x), trimlen = strlen(trimset);

    for (i = 0; i < trimlen; i++)
        set_bit(trimset[i]);
    for (i = 0; i < slen; i++)
        if (!check_bit(dataptr[i]))
            break;
    for (; slen > 0; slen--)
        if (!check_bit(dataptr[slen - 1]))
            break;
    dataptr += i;
    slen -= i;

    /* reserved space as a buffer on the heap.
     * Do not reallocate immediately. Instead, reuse it as possible.
     * Do not shrink to in place if < 16 bytes.
     */
    memmove(orig, dataptr, slen);
    /* do not dirty memory unless it is needed */
    if (orig[slen])
        orig[slen] = 0;

    if (xs_is_ptr(x))
        x->size = slen;
    else
        x->space_left = 15 - slen;
    return x;
#undef check_bit
#undef set_bit
}

void xs_copy(xs *dest, xs *src)
{
    *dest = *src;

    /*
     * src string from stack: No need to invoke memcpy() since the data
     * has been copied from the statement '*dest = *src'
     */
    if (!xs_is_ptr(src))
        return;

    if (xs_is_large_string(src)) {
        /* CoW: simply increase the reference count */
        xs_inc_ref_count(src);
    } else {
        /* Medium string */
        dest->ptr = kmalloc((size_t) 1 << src->capacity, GFP_KERNEL);
        memcpy(dest->ptr, src->ptr, src->size);
    }
}

void xs_trivia_test(void)
{
    xs string = *xs_tmp("\n foobarbar \n\n\n");
    xs backup_string;

    xs_copy(&backup_string, &string);

    xs_trim(&string, "\n ");

    xs prefix = *xs_tmp("((("), suffix = *xs_tmp(")))");
    xs_concat(&string, &prefix, &suffix);
}
