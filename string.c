#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <malloc.h>

#include "string.h"

// No sense in including all of glib for this, make use of gcc
#define MIN(a, b)  ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })

String str_wrap_n(const char* s, uint32_t n)
{
    return (s == 0 || n == 0) ? STR_S("") : ((String) {(char*)s, n, false});
}

String str_wrap(const char* s)
{
    return str_wrap_n(s, s != 0 ? strlen(s) : 0);
}

String str_create(uint32_t len)
{
    return ((String) {calloc(len + 1, 1), len, true});
}

String str_new(const char* s)
{
    return str_duplicate(str_wrap(s));
}

void str_free(String s)
{
    if (s.can_free)
        free(s.str);
}

String str_duplicate(String s)
{
    String dst = str_create(s.len);
    strncpy(dst.str, s.str, s.len);
    return dst;
}

String str_concat(String a, String b)
{
    String dst = str_create(a.len + b.len);
    strncpy(dst.str, a.str, a.len);
    strncpy(dst.str + a.len, b.str, b.len);
    return dst;
}

String str_substring(String s, uint32_t begin, uint32_t length)
{
    char* str = s.str + MIN(begin, s.len);
    uint32_t len = MIN(length, s.len - begin);
    return str_wrap_n(str, len);
}

inline static int cdiff_s(char a, char b)
{
    return a - b;
}

inline static int cdiff_i(char a, char b)
{
    return tolower(a) - tolower(b);
}

static bool str_contains_impl(String s, String what, int (*dif) (char, char))
{
    if (what.len > s.len)
        return false;
    uint32_t wi = 0;
    for (uint32_t i = 0; i < s.len; i++)
    {
        wi = !dif(s.str[i], what.str[wi]) ? wi + 1 : 0;
        if (wi == what.len)
            return true;
    }
    return false;
}

bool str_contains(String s, String what)
{
    return str_contains_impl(s, what, cdiff_s);
}

bool str_contains_i(String s, String what)
{
    return str_contains_impl(s, what, cdiff_i);
}

static bool str_ends_with_impl(String s, String suffix, int (*dif) (char, char))
{
    if (s.len < suffix.len)
        return false;
    for (uint32_t i = 1; i <= suffix.len; i++)
        if (dif(s.str[s.len - i], suffix.str[suffix.len - i]))
            return false;
    return true;
}

bool str_ends_with_i(String s, String suffix)
{
    return str_ends_with_impl(s, suffix, cdiff_i);
}

String str_to_lower(String s)
{
    for (uint32_t i = 0; i < s.len; i++)
        s.str[i] = tolower(s.str[i]);
    return s;
}

int str_compare_impl(String a, String b, int (*dif) (char, char))
{
    for (uint32_t i = 0; i < MIN(a.len, b.len); i++)
        if (dif(a.str[i], b.str[i]))
            return dif(a.str[i], b.str[i]);
    return 0;
}

int str_compare_i(String a, String b)
{
    return str_compare_impl(a, b, cdiff_i);
}

bool str_equal_i(String a, String b)
{
    return str_compare_impl(a, b, cdiff_i) == 0;
}

int str_compare(String a, String b)
{
    return str_compare_impl(a, b, cdiff_s);
}

bool str_equal(String a, String b)
{
    return str_compare_impl(a, b, cdiff_s) == 0;
}

String assemble_path(String prefix, String suffix)
{
    String path = str_create(prefix.len + suffix.len + 1);

    strncpy(path.str, prefix.str, prefix.len);
    if (path.str[prefix.len - 1] != '/')
        path.str[prefix.len++] = '/';

    strncpy(path.str + prefix.len, suffix.str, suffix.len);

    path.str[prefix.len + suffix.len] = 0;
    path.len = prefix.len + suffix.len;

    return path;
}

