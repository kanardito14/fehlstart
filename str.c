/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "str.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static String str_wrap_impl(const char* s, uint32_t n, bool can_free)
{
    if (!s)
        return STR_S("");
    if (n == STR_END)
        n = strlen(s);
    if (n == 0)
        return STR_S("");
    return (String) {(char*)s, n, can_free};
}

String str_wrap_n(const char* s, uint32_t n)
{
    return str_wrap_impl(s, n, false);
}

String str_wrap(const char* s)
{
    return str_wrap_impl(s, STR_END, false);
}

String str_own(const char* s)
{
    return str_wrap_impl(s, STR_END, true);
}

String str_create(uint32_t len)
{
    return (String) {calloc(len + 1, 1), len, true};
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

//------------------------------------------

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

String str_join_path(String prefix, String suffix)
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

//------------------------------------------

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

static uint32_t str_find_first_impl(String s, String what, int (*dif) (char, char))
{
    uint32_t wi = 0;
    for (uint32_t i = 0; i < s.len && wi < what.len; i++) {
        wi = !dif(s.str[i], what.str[wi]) ? wi + 1 : 0;
        if (wi == what.len)
            return i - what.len + 1;
    }
    return STR_END;
}

uint32_t str_find_first(String s, String what)
{
    return str_find_first_impl(s, what, cdiff_s);
}

uint32_t str_find_first_i(String s, String what)
{
    return str_find_first_impl(s, what, cdiff_i);
}

bool str_contains(String s, String what)
{
    return str_find_first_impl(s, what, cdiff_s) != STR_END;
}

bool str_contains_i(String s, String what)
{
    return str_find_first_impl(s, what, cdiff_i) != STR_END;
}

//------------------------------------------

static bool str_starts_with_impl(String s, String prefix, int (*dif) (char, char))
{
    if (prefix.len == 0 || s.len < prefix.len)
        return false;
    for (uint32_t i = 0; i < prefix.len; i++)
        if (dif(s.str[i], prefix.str[i]))
            return false;
    return true;
}

static bool str_ends_with_impl(String s, String suffix, int (*dif) (char, char))
{
    if (suffix.len == 0 || s.len < suffix.len)
        return false;
    for (uint32_t i = 1; i <= suffix.len; i++)
        if (dif(s.str[s.len - i], suffix.str[suffix.len - i]))
            return false;
    return true;
}

bool str_starts_with(String s, String prefix)
{
    return str_starts_with_impl(s, prefix, cdiff_s);
}

bool str_starts_with_i(String s, String prefix)
{
    return str_starts_with_impl(s, prefix, cdiff_i);
}

bool str_ends_with(String s, String suffix)
{
    return str_ends_with_impl(s, suffix, cdiff_s);
}

bool str_ends_with_i(String s, String suffix)
{
    return str_ends_with_impl(s, suffix, cdiff_i);
}

//------------------------------------------

static int str_compare_impl(String a, String b, int (*dif) (char, char))
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
    return (a.len == b.len) && str_compare_impl(a, b, cdiff_i) == 0;
}

int str_compare(String a, String b)
{
    return str_compare_impl(a, b, cdiff_s);
}

bool str_equal(String a, String b)
{
    return (a.len == b.len) && str_compare_impl(a, b, cdiff_s) == 0;
}

//------------------------------------------

String str_to_lower(String s)
{
    for (uint32_t i = 0; i < s.len; i++)
        s.str[i] = tolower(s.str[i]);
    return s;
}

