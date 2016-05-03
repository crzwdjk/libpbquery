#include <ctype.h>
#include <errno.h>
#include <protobuf-c/protobuf-c.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pbquery.h"
#include "pbquery-expr.h"


static struct pbq_path *parse_path(const char *query_string,
                                   ProtobufCMessageDescriptor *ctx,
                                   size_t *len);
static const char *chomp_ws(const char *string)
{
    while (isspace(*string)) string++;
    return string;
}

static char *parse_ident(const char *string, size_t *end)
{
    const char *t = string;
    if (*string != '_' && !isalpha(*string)) {
        return NULL;
    }
    while (isalnum(*t) || *t == '_') t++;

    *end = t - string;

    char *buf = malloc(*end + 1);
    memcpy(buf, string, *end + 1);
    buf[*end] = 0;

    return buf;
}

//   item: <path> | <lit> | '@'
//   lit: <str> | <int> | <float>
static struct pbq_item *parse_item(const char *string,
                                   ProtobufCMessageDescriptor *ctx,
                                   size_t *end)
{
    struct pbq_item *item = malloc(sizeof(*item));
    if (string[0] == '@') {
        *end = 1;
        item->type = ITEM_AT;
    }
    else if (string[0] == '"' || string[0] == '\'') {
        item->type = ITEM_STR;
        const char *p = string + 1;
        size_t len = 0;
        while (*p && *p != string[0]) {
            if (*p == '\\' && (*(p + 1) == '\\'
                               || *(p + 1) == '\''
                               || *(p + 1) == '"')) p++;
            p++;
            len++;
        }
        item->v.strval = calloc(1, len + 1);
        *end = (p + 1) - string;
        p = string + 1;
        for (size_t i = 0; i < len; i++) {
            if (*p == '\\' && (*(p + 1) == '\\'
                               || *(p + 1) == '\''
                               || *(p + 1) == '"')) p++;
            item->v.strval[i] = *p++;
        }
    }
    else if (isdigit(string[0]) ||
             ((string[0] == '+' || string[0] == '-' ) && isdigit(string[1]))) {
        char *endi, *endf;
        long ival = strtol(string, &endi, 10);
        double fval = strtod(string, &endf);
        *end = endf - string;
        if (endi == endf) {
            item->type = ITEM_INT;
            item->v.intval = ival;
        } else {
            item->type = ITEM_FLOAT;
            item->v.floatval = fval;
        }
    }
    else {
        item->type = ITEM_PATH;
        item->v.path = parse_path(string, ctx, end);
        if (!item->v.path) {
            free(item);
            return NULL;
        }
    }

    return item;
}

//   expr: [ <int> | <relation> ]
//   relation: <item> = <item>
//           | <item> != <item>
//           | <item> =~ <regex>
//           | <item> in <list>
static int parse_filter(const char *string,
                        struct pbq_filter *filter,
                        ProtobufCMessageDescriptor *ctx,
                        size_t *end)
{
    size_t incr;
    const char *orig_s = string;
    string = chomp_ws(string);
    struct pbq_item *item = parse_item(string, ctx, &incr);
    string += incr;
    if (!item) return 0;
    string = chomp_ws(string);
    if (string[0] == ']') {
        if (item->type == ITEM_INT) {
            // XXX: type check for indexability
            // parentctx->fields[item->name].label == PROTOBUF_C_LABEL_REPEATED
            filter->type = FILTER_IDX;
            filter->v.idx = item->v.intval;
        } else {
            return 0;
        }
    }
    if (string[0] == '=' || (string[0] == '!' && string[1] == '=')) {
        filter->v.eq_filter.invert = (string[0] == '!');
        string++;
        if (string[0] == '=') string++;
        string = chomp_ws(string);
        filter->type = FILTER_EQ;
        filter->v.eq_filter.left = item;
        filter->v.eq_filter.right = parse_item(string, ctx, &incr);
        if (!filter->v.eq_filter.right) {
            free(item);
            return 0;
        }
        
        // XXX: type check RHS
        string += incr;
    }
    else if (string[0] == '~') {
        // XXX: parse regex
        return 0;
    }
    else if (string[0] == 'i' && string[1] == 'n') {
        // XXX: parse list
        return 0;
    }
    string = chomp_ws(string);
    *end = string - orig_s;
    return 1;
}


// ident [ '[' expr | list ']' ]
static int parse_node(const char* string, char **name,
                      struct pbq_filter *filter,
                      ProtobufCMessageDescriptor *ctx,
                      size_t *end)
{
    size_t pos = 0;
    *name = parse_ident(string, &pos);
    if (!*name) {
        *end = 0;
        return 0;
    }
    if (string[pos] == '[') {
        size_t incr = 0;
        pos++;

        // filter paths are evaluated in the context of the parent node
        const ProtobufCFieldDescriptor *field =
            protobuf_c_message_descriptor_get_field_by_name(ctx, *name);
        ctx = (ProtobufCMessageDescriptor*)(field->descriptor);
        
        if (!parse_filter(string + pos, filter, ctx, &incr)) {
            free(*name);
            return 0;
        }
        pos += incr;

        if (string[pos] != ']') {
            free(*name);
            return 0;
        }
        pos++;
    }
    *end = pos;
    return 1;
}

static struct pbq_path *parse_path(const char *query_string,
                            ProtobufCMessageDescriptor *ctx,
                            size_t *len)
{
    struct pbq_path *path = calloc(1, sizeof(*path));
    path->ctx = ctx;
    size_t count = 0;
    size_t end = 0;
    do {
        struct pbq_filter filter = { .type = FILTER_NONE };
        char *name;
        size_t incr;
        if(!parse_node(query_string + end, &name, &filter, ctx, &incr)) {
            goto fail;
        }
        end += incr;
        count++;
        const ProtobufCFieldDescriptor *field =
            protobuf_c_message_descriptor_get_field_by_name(ctx, name);
        free(name);
        if (!field) {
            goto fail;
        }
        path->path = realloc(path->path, sizeof(*path->path) * count);
        path->filters = realloc(path->filters, sizeof(*path->filters) * count);
        path->path[count - 1] = field->id;
        path->filters[count - 1] = filter;
        if (query_string[end] == '.') {
            if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
                goto fail;
            } else {
                ctx = (ProtobufCMessageDescriptor*)(field->descriptor);
            }
        }
    } while (query_string[end++] == '.');
    *len = end - 1;
    path->count = count;
    return path;
 fail:
    free(path->path);
    free(path->filters);
    free(path);
    return NULL;
}

struct pbquery_stmt *pbquery_compile(struct ProtobufCMessageDescriptor *ctx,
                                     const char *query_string)
{
    struct pbquery_stmt *stmt = calloc(1, sizeof(*stmt));

    size_t len = 0;
    stmt->path = parse_path(query_string, ctx, &len);
    if (!stmt->path) {
        pbquery_debug("Error parsing query %s",
                      query_string);
        goto fail;        
    }
    if (query_string[len]) {
        pbquery_debug("Trailing garbage after query string: %c",
                      query_string[len]);
        goto fail;
    }
    
    return stmt;
 fail:
    free(stmt);
    errno = EINVAL;
    return NULL;
}
