#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pbquery.h"

static char *munge_messagename(const char* messagename)
{
    size_t ncaps = 0;
    size_t namelen = strlen(messagename);
    for (int i = 0; i < namelen; i++) {
        if (isupper(messagename[i])) {
            ncaps++;
        }
    }

    /* replace [A-Z] with "_${lc $1}" */
    char *ret = malloc(namelen + ncaps);
    char *ret_put = ret; 
    *ret_put++ = tolower(*messagename++);

    do {
        if (isupper(*messagename)) {
            *ret_put++ = '_';
            *ret_put++ = tolower(*messagename);
        } else {
            *ret_put++ = *messagename;
        } 
    } while (*messagename++);

    return ret;
}
    
ProtobufCMessageDescriptor *pbquery_init(void *libhandle, const char *rootname)
{
    const char *dot = rindex(rootname, '.');
    if (!dot) dot = rootname;
    size_t namespacelen = dot - rootname;
    char *namespace = malloc(namespacelen + 1);
    memcpy(namespace, rootname, namespacelen);
    namespace[namespacelen] = 0;

    const char *messagename = dot + 1;
    char *messagename_munged = munge_messagename(messagename);

    size_t sym_name_len = namespacelen + 2
        + strlen(messagename_munged)
        + 2 + strlen("__descriptor") + 1;
    char *sym_name = malloc(sym_name_len + 1);
    snprintf(sym_name, sym_name_len, "%s__%s__descriptor",
             namespace, messagename_munged);
    ProtobufCMessageDescriptor *root_descriptor = dlsym(libhandle, sym_name);
    free(sym_name);
    if (!root_descriptor) {
        fprintf(stderr, "Could not load descriptor %s (%s): %s",
                messagename, messagename_munged, dlerror());
        errno = ENOENT;
    }

    free(namespace);
    free(messagename_munged);

    return root_descriptor;
}
