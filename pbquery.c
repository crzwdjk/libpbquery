#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pbquery.h"
#include "pbquery-expr.h"

typedef struct {
    void *buf;
    size_t len;
} slice;

/* Read a varint. Return the value read, and set *skip to the number
   of bytes consumed */
static uint64_t pb_read_varint(uint8_t *buf, size_t *skip)
{
    uint64_t acc = 0;
    *skip = 0;
    do {
        acc += ((*buf & 0x7f) << (7 * (*skip)++));
    } while (*buf++ & 0x80);
    return acc;
}

/* Read a uint from the given slice */
static uint64_t pb_read_uint(slice msg)
{
    switch (msg.len) {
    case 4:
        return *((uint32_t*)msg.buf);
    case 8:
        return *((uint64_t*)msg.buf);
    default:
        assert(0);
    }
    return 0;
}

/* Read a float from the given slice */
static double pb_read_float(slice msg)
{
#if __BYTE_ORDER != __LITTLE_ENDIAN
#error "Only little endian supported for now"
#endif
    switch (msg.len) {
    case 8:
        return *((double*)msg.buf);
    case 4:
        return *((float*)msg.buf);
    default:
        assert(0);
    }
    return 0;
}

/* Compare whatever is in the given slice to the value. Which
   sort of comparison to use is determined by the type of val. */
int pb_compare_val(slice msg, struct pbq_item *val)
{
    switch(val->type) {
    case ITEM_STR: {
        if (msg.len != strlen(val->v.strval)) return 0;
        return !strncmp(val->v.strval, msg.buf, msg.len);
    }
    case ITEM_INT: {
        int64_t ival = pb_read_uint(msg);
        return ival == val->v.intval;
    }
    case ITEM_FLOAT: {
        double fval = pb_read_float(msg);
        return (fval == val->v.floatval);
    }
    case ITEM_AT:
    case ITEM_PATH:
        assert(!"Not yet supported to compare to path");
        return 0;
    }
    return 0;
}

static slice pb_read_msg(void *buf, uint32_t *tag)
{
    size_t taglen;
    *tag = pb_read_varint(buf, &taglen);
    slice ret = { .buf = buf + taglen };
    switch(*tag & 0x7) {
    case PROTOBUF_C_WIRE_TYPE_32BIT:
        ret.len = 4;
        break;
    case PROTOBUF_C_WIRE_TYPE_64BIT:
        ret.len = 8;
        break;
    case PROTOBUF_C_WIRE_TYPE_VARINT:
        pb_read_varint(ret.buf, &(ret.len));
        break;
    case PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED:
        ret.len = pb_read_varint(ret.buf, &taglen);
        ret.buf += taglen;
        break;
    default:
        assert(0);
    }
    return ret;
}

static int eval_filter(slice msg, struct pbq_filter *filter);

typedef int (*find_path_cb)(void *cbdata, slice msg);

static void find_paths(void *buf, size_t len, struct pbq_path *path,
                       find_path_cb callback, void *cbdata)
{
    void *end = buf + len;
    while (buf < end) {
        uint32_t tag;
        slice msg = pb_read_msg(buf, &tag);
        buf = msg.buf + msg.len;
        if (tag >> 3 != path->path[0] ||
            !eval_filter(msg, path->filters)) {
            // not the droids we're looking for
            continue;
        }
        if (path->count == 1) {
            if ((*callback)(cbdata, msg))
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
        find_paths(msg.buf, msg.len, &subpath, callback, cbdata);
    }
}

static int append_result(void *cbdata, slice msg)
{
    struct pbquery_result *res = (struct pbquery_result*)cbdata;
    if (res->nresults >= res->resultbufsize) {
        res->resultbufsize *= 2;
        res->resultptrs = realloc(res->resultptrs,
                                  sizeof(*res->resultptrs) * res->resultbufsize);
        res->lengths = realloc(res->lengths,
                               sizeof(*res->lengths) * res->resultbufsize);
    }
    res->resultptrs[res->nresults] = msg.buf;
    res->lengths[res->nresults] = msg.len;
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

        
static int find_one_path_cb(void *cbdata, slice msg)
{
    *(slice *)cbdata = msg;
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

/* Evaluate a filter on the message in msg. */
static int eval_filter(slice msg, struct pbq_filter *filter)
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
        slice submsg = { .buf = msg.buf, .len = msg.len };
        if (left->type == ITEM_PATH) {
            submsg = find_path(msg.buf, msg.len, left->v.path);
            if (!submsg.buf) return 0;
        }
        int result = pb_compare_val(submsg, right);

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
