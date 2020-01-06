#ifndef _ESTRING_H_
#define _ESTRING_H_

/*** custom string ***/
typedef struct estring
{
    char* s;
    int len;
} estring;

#define ESTR_INIT {NULL, 0}

void estrAppend(estring* es, const char* s, int len);
void estrFree(estring* es);

#endif // _ESTRING_H_
