#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    char* str;
    uint32_t len;
    bool can_free;
} String;

// string "constructor" for creating strings with no runtime overhead at comile time
// for static strings
#define STR_S(arg) ((String) {(arg), sizeof(arg) - 1, false})
// for initializer lists, etc
#define STR_I(arg) {(arg), sizeof(arg) - 1, false}

#define STR_END UINT32_MAX

// wrap a zero terminated c string
String str_wrap(const char* s);

// wrap a c string of length len
String str_wrap_n(const char* s, uint32_t len);

// create a new string from a zero terminated c string
// must be freed with str_free()
String str_new(const char* s);

// free strings allocated with str_duplicate, or str_new
// safe to use for static strings
void str_free(String s);

// create an empsty sero padded string
// must be freed with str_free()
String str_create(uint32_t len);

// duplicate string
// must be freed with str_free()
String str_duplicate(String s);

// concatinate (append) two strings
// must be freed with str_free()
String str_concat(String a, String b);

// create a new path string from two parts
// example 1: "/foo" "bar" -> "/foo/bar"
// example 2: "/foo/" "bar" -> "/foo/bar"
// must be freed with str_free()
String assemble_path(String a, String b);

// create substring, using same memory as s
String str_substring(String s, uint32_t begin, uint32_t length);


// returns true if s contains what, case sensitive
bool str_contains(String s, String what);
// returns true if s contains what, not case sensitive
bool str_contains_i(String s, String what);
// find first match of what, returns STR_END if no match was found
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
