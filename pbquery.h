#ifndef pbquery_h
#define pbquery_h

#include <protobuf-c/protobuf-c.h>

struct pbquery_stmt;
struct pbquery_ctx;
struct pbquery_result {
    size_t nresults;
    void **resultptrs;
    size_t *lengths;
    size_t resultbufsize;
};

struct pbquery_stmt {
    struct pbq_path *path;
    size_t nbinds;
    void *binds;
};



extern ProtobufCMessageDescriptor *pbquery_init(void *libhandle,
                                                const char *rootname);
extern struct pbquery_stmt *pbquery_compile(struct ProtobufCMessageDescriptor*,
                                            const char *query_string);
extern struct pbquery_result *pbquery_simple(void *buf, size_t len,
                                             struct pbquery_stmt*);
extern struct pbquery_result *pbquery_binds(void *buf, struct pbquery_stmt*,
                                            ...);

extern void pbquery_free_result(struct pbquery_result *);
extern void pbquery_free_stmt(struct pbquery_stmt *);


#include <stdarg.h>
static void pbquery_debug(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}


#endif
