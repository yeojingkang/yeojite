/*** include ***/
#include <stdlib.h>
#include <string.h>

#include "estring.h"

void estrAppend(estring* es, const char* s, int len)
{
    // Note: original estring's content is preserved
    char* newES = realloc(es->s, es->len + len);

    if (newES == NULL)
        return;

    // Append new content
    memcpy(&newES[es->len], s, len);
    es->s = newES;
    es->len += len;
}

void estrFree(estring* es)
{
    free(es->s);
}
