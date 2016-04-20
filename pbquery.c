#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pbquery.h"
#include "pbquery-expr.h"

typedef struct {
    void *buf;
    size_t len;
} slice;


static uint64_t pb_read_varint(uint8_t *buf, size_t *skip)
{
    uint64_t acc = 0;
    *skip = 0;
    do {
        acc += ((*buf & 0x7f) << (7 * (*skip)++));
    } while (*buf++ & 0x80);
    return acc;
}

static uint64_t pb_read_uint(uint8_t *buf, size_t len)
{
    switch (len) {
    case 4:
        return *((uint32_t*)buf);
    case 8:
        return *((uint64_t*)buf);
    default:
        assert(0);
    }
    return 0;
}

static double pb_read_float(uint8_t *buf, size_t len)
{
#if __BYTE_ORDER != __LITTLE_ENDIAN
#error "Only little endian supported for now"
#endif
    switch (len) {
    case 8:
        return *((double*)buf);
    case 4:
        return *((float*)buf);
    default:
        assert(0);
    }
    return 0;
}

int pb_compare_val(void *buf, size_t len, struct pbq_item *val)
{
    switch(val->type) {
    case ITEM_STR: {
        if (len != strlen(val->v.strval)) return 0;
        return !strncmp(val->v.strval, buf, len);
    }
    case ITEM_INT: {
        int64_t ival = pb_read_uint(buf, len);
        return ival == val->v.intval;
    }
    case ITEM_FLOAT: {
        double fval = pb_read_float(buf, len);
        return (fval == val->v.floatval);
    }
    case ITEM_AT:
    case ITEM_PATH:
        assert(!"Not yet supported to compare to path");
        return 0;
    }
    return 0;
}

static void *pb_read_msg(void *buf, size_t *msglen, uint32_t *tag)
{
    size_t taglen;
    *tag = pb_read_varint(buf, &taglen);
    buf += taglen;
    switch(*tag & 0x7) {
    case PROTOBUF_C_WIRE_TYPE_32BIT:
        *msglen = 4;
        break;
    case PROTOBUF_C_WIRE_TYPE_64BIT:
        *msglen = 8;
        break;
    case PROTOBUF_C_WIRE_TYPE_VARINT:
        pb_read_varint(buf, msglen);
        break;
    case PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED:
        *msglen = pb_read_varint(buf, &taglen);
        buf += taglen;
        break;
    default:
        assert(0);
    }
    return buf;
}

static int eval_filter(void *buf, size_t len, struct pbq_filter *filter);

typedef int (*find_path_cb)(void *cbdata, void *buf, size_t len);

static void find_paths(void *buf, size_t len, struct pbq_path *path,
                       find_path_cb callback, void *cbdata)
{
    void *end = buf + len;
    while (buf < end) {
        uint32_t tag;
        size_t msglen;
        void *msgstart = pb_read_msg(buf, &msglen, &tag);
        buf = msgstart + msglen;
        if (tag >> 3 != path->path[0] ||
            !eval_filter(msgstart, msglen, path->filters)) {
            // not the droids we're looking for
            continue;
        }
        if (path->count == 1) {
            if ((*callback)(cbdata, msgstart, msglen))
                continue;
            else
                return;
        }
        // we need to keep descending
        assert((tag & 7) == PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED);
        struct pbq_path subpath = {
            .count = path->count - 1,
            .path = path->path + 1,
            .filters = path->filters + 1,
        };
        find_paths(msgstart, msglen, &subpath, callback, cbdata);
    }
}

static int append_result(void *cbdata, void* a, size_t b)
{
    struct pbquery_result *res = (struct pbquery_result*)cbdata;
    if (res->nresults >= res->resultbufsize) {
        res->resultbufsize *= 2;
        res->resultptrs = realloc(res->resultptrs,
                                  sizeof(*res->resultptrs) * res->resultbufsize);
        res->lengths = realloc(res->lengths,
                               sizeof(*res->lengths) * res->resultbufsize);
    }
    res->resultptrs[res->nresults] = a;
    res->lengths[res->nresults] = b;
    res->nresults++;
    return 1;
}

// (buf<'A>, stmt<'B>) -> result<'A>
struct pbquery_result *pbquery_simple(void *buf, size_t len,
                                      struct pbquery_stmt* stmt)
{
    struct pbquery_result *res = malloc(sizeof(*res));
    res->nresults = 0;
    res->resultptrs = malloc(16 * sizeof(*res->resultptrs));
    res->lengths = malloc(16 * sizeof(*res->lengths));
    res->resultbufsize = 16;
    find_paths(buf, len, stmt->path, append_result, res);
    return res;
}

        
static int find_one_path_cb(void *cbdata, void* buf, size_t len)
{
    slice *result = (slice *)cbdata;
    result->buf = buf;
    result->len = len;
    return 0;
}

/* Find the first occurrence of path in buf and return a pointer
   to the first byte of the message tag 
   Returns null if no such message is found.
*/
static slice find_path(void *buf, size_t len, struct pbq_path *path)
{
    slice result = {};
    find_paths(buf, len, path, find_one_path_cb, &result);
    return result;
}

static int eval_filter(void *buf, size_t len, struct pbq_filter *filter)
{
    switch (filter->type) {
    case FILTER_NONE:
        return 1;
    case FILTER_IDX:
        assert(0);
        return 0;
    case FILTER_EQ: {
        struct pbq_item *left = filter->v.eq_filter.left;
        struct pbq_item *right = filter->v.eq_filter.right;
        assert(left->type == ITEM_PATH || left->type == ITEM_AT);
        slice submsg = { .buf = buf, .len = len };
        if (left->type == ITEM_PATH) {
            submsg = find_path(buf, len, left->v.path);
            if (!submsg.buf) return 0;
        }
        int result = pb_compare_val(submsg.buf, submsg.len, right);

        return filter->v.eq_filter.invert ? !result : result;
    }
    case FILTER_LIST:
        assert(0);
        return 0;
    case FILTER_MATCH:
        assert(0);
        return 0;
    }
    return 0;
}
