/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <stdbool.h>
#include <stdint.h>

// --------------------------------------------
// preferences

// some typedefs so the xmacros will work
typedef char* string;
typedef bool boolean;
typedef int integer;

#define PREFERENCES_LIST \
    P(string,  Bindings, launch,     DEFAULT_HOTKEY) \
    P(boolean, Matching, executable, true) \
    P(boolean, Icons,    show,       true) \
    P(boolean, Icons,    scale,      true) \
    P(string,  Border,   color,      "default") \
    P(integer, Border,   width,      2) \
    P(string,  Window,   color,      "default") \
    P(string,  Window,   width,      200) \
    P(string,  Window,   height,     100) \
    P(boolean, Window,   round,      true) \
    P(boolean, Window,   arch,       true) \
    P(string,  Labels,   color,      "default") \
    P(integer, Labels,   fontsize1,  14) \
    P(integer, Labels,   fontsize2,  10) \
    P(boolean, Labels,   showinput,  true)
    
#define P(type, group, key, value) extern type group##_##key = value;
PREFERENCES_LIST
#undef P

// --------------------------------------------
// helper functions

inline static int imin(int a, int b) 
{ 
    return a < b ? a : b; 
}

inline static int iclamp(int v, int min, int max) 
{ 
    return v < min ? min : v > max ? max : v; 
}

// --------------------------------------------
// string.c functions

typedef struct {
    char* str;
    uint32_t len;
    bool can_free;
} String;

// string "constructor" for creating strings with no runtime overhead at compile time
// for static strings
#define STR_S(arg) ((String) {(arg), sizeof(arg) - 1, false})
// for initializer lists, etc
#define STR_I(arg) {(arg), sizeof(arg) - 1, false}

#define STR_END UINT32_MAX

// wrap a zero terminated string
// freeing has no effect
String str_wrap(const char* s);

// wrap a string of length len
// freeing has no effect
String str_wrap_n(const char* s, uint32_t len);

// wrap a zero terminated string, and take ownership
// s will be freed by str_free()
// must be freed with str_free()
String str_own(const char* s);

// create a new string as copy of zero terminated string
// s will not be freed by str_free()
// must be freed with str_free()
String str_new(const char* s);

// free strings allocated with str_duplicate, str_own, or str_new
// safe to use for static strings
void str_free(String s);

// create an empty zero padded string
// must be freed with str_free()
String str_create(uint32_t len);

// duplicate string
// must be freed with str_free()
String str_duplicate(String s);

// concatinate (append) two strings
// a and b remain unchanged
// must be freed with str_free()
String str_concat(String a, String b);

// create a new path string from two parts
// example 1: "/foo" "bar" -> "/foo/bar"
// example 2: "/foo/" "bar" -> "/foo/bar"
// must be freed with str_free()
String str_assemble_path(String a, String b);

// create substring, using same memory as s
String str_substring(String s, uint32_t begin, uint32_t length);

// return true if equal, case sensitive
bool str_equals(String a, String b);

// returns true if s contains what, case sensitive
bool str_contains(String s, String what);
// returns true if s contains what, not case sensitive
bool str_contains_i(String s, String what);
// returns STR_END if no match was found, case sensitive
uint32_t str_find_first(String s, String what);
// returns STR_END if no match was found, not case sensitive
uint32_t str_find_first_i(String s, String what);


// returns true if s starts with prefix, case sensitive
bool str_starts_with(String s, String prefix);
// returns true if s starts with prefix, not case sensitive
bool str_starts_with_i(String s, String prefix);
// returns true if s ends with suffix, case sensitive
bool str_ends_with(String s, String suffix);
// returns true if s ends with suffix, not case sensitive
bool str_ends_with_i(String s, String suffix);


// like strcmp, case sensitive
int str_compare(String a, String b);
// like strcmp, not case sensitive
int str_compare_i(String a, String b);
// wrapper for str_compare() == 0
bool str_equal(String a, String b);
// wrapper for str_compare_i() == 0
bool str_equal_i(String a, String b);


// converts string to lowercase, returned string is same as s
String str_to_lower(String s);
