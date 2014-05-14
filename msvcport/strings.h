#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

    int    bcmp(const void * p1, const void * p2, size_t n);
    void   bcopy(const void * src, void * dst, size_t n);
    void   bzero(void * p, size_t n);
    char * index(const char * s, int c);
    char * rindex(const char * s, int c);
    int    strcasecmp(const char * s1, const char * s2);
    int    strncasecmp(const char * s1, const char * s2, size_t n);
    int    ffs(int v);

#ifdef __cplusplus
}
#endif
