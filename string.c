#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>

#include "string.h"

// No sense in including all of glib for this
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

String str_wrap_n(const char* s, uint32_t n)
{
    return (s == 0) ?
        ((String) {"", 0, false}) :
        ((String) {(char*)s, n, false});
}

String str_wrap(const char* s)
{
    return str_wrap_n(s, strlen(s));
}

String str_create(uint32_t len)
{
    String s = {malloc(len + 1), len, true};
    memset(s.str, 0, len + 1);
    return s;
}

void str_free(String s)
{
    if (s.can_free)
        free(s.str);
}

String str_duplicate(String s)
{
    String dst = str_create(s.len);
    uint32_t i = 0;
    for (; s.str[i] != 0 && i < s.len; i++)
        dst.str[i] = s.str[i];
    dst.str[i] = 0;
    return dst;
}

String str_substring(String s, uint32_t begin, uint32_t length)
{
    char* str = s.str + MIN(begin, s.len);
    uint32_t len = MIN(length, s.len - begin);
    return str_wrap_n(str, len);
}

bool str_contains(String s, String what)
{
    if (what.len > s.len)
        return false;
    uint32_t wi = 0;
    for (uint32_t i = 0; i < s.len; i++)
    {
        wi = (s.str[i] == what.str[wi]) ? wi + 1 : 0;
        if (wi == what.len)
            return true;
    }
    return false;
}

bool str_contains_i(String s, String what)
{
    if (what.len > s.len)
        return false;
    uint32_t wi = 0;
    for (uint32_t i = 0; i < s.len; i++)
    {
        wi = (tolower(s.str[i]) == tolower(what.str[wi])) ? wi + 1 : 0;
        if (wi == what.len)
            return true;
    }
    return false;
}

bool str_ends_with_i(String s, String suffix)
{
    if (s.len < suffix.len)
        return false;
    for (uint32_t i = 1; i <= suffix.len; i++)
        if (tolower(s.str[s.len - i]) != tolower(suffix.str[suffix.len - i]))
            return false;
    return true;
}

String str_to_lower(String s)
{
    for (uint32_t i = 0; i < s.len; i++)
        s.str[i] = tolower(s.str[i]);
    return s;
}

int str_compare_i(String a, String b)
{
    for (uint32_t i = 0; i < a.len && i < b.len; i++)
    {
        int diff = tolower(a.str[i]) - tolower(b.str[i]);
        if (diff != 0)
            return diff;
    }
    return a.len - b.len;
}

bool str_equal_i(String a, String b)
{
    return str_compare_i(a, b) == 0;
}

int str_compare(String a, String b)
{
    for (uint32_t i = 0; i < a.len && i < b.len; i++)
    {
        int diff = a.str[i] - b.str[i];
        if (diff != 0)
            return diff;
    }
    return a.len - b.len;
}

bool str_equal(String a, String b)
{
    return str_compare(a, b) == 0;
}

// creates a new string that must be freed
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

