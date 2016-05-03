/* A pbquery path: a sequence of message selectors and filters */
struct pbq_path {
    ProtobufCMessageDescriptor *ctx;
    size_t count;
    uint32_t *path;
    struct pbq_filter *filters;
};

/* A pbquery filter expression.
   Note that by convention, in a comparison the left side is a path while
   the right side can be a constant or path. */
enum pbq_filtertype { FILTER_LIST, FILTER_EQ, FILTER_MATCH, FILTER_NONE, FILTER_IDX };
struct pbq_filter {
    enum pbq_filtertype type;
    union {
        int idx;
        struct {
            int invert;
            struct pbq_item *left;
            struct pbq_item *right;
        } eq_filter;
        struct {
            struct pbq_item *left;
            struct regex *right;
        } rx_filter;
        struct {
            struct pbq_item *left;
            size_t nitems;
            struct pbq_item **right;
        } in_filter;
    } v;
};

/* A single item from a pbquery expression. */
enum pbq_itemtype { ITEM_INT, ITEM_FLOAT, ITEM_STR, ITEM_PATH, ITEM_AT };
struct pbq_item {
    enum pbq_itemtype type;
    union {
        struct pbq_path *path;
        int intval;
        double floatval;
        char *strval;
    } v;
};

