#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    char* str;
    uint32_t len;
    bool can_free;
} String;

// string "constructor" macros
#define STR_S(arg) ((String) {(arg), sizeof(arg) - 1, false})   // for static strings (comile time)
#define STR_I(arg) {(arg), sizeof(arg) - 1, false}              // for initializer (compile time)
#define STR_D(arg) str_wrap(arg)                                // for dymanic strings (runtime)

String str_wrap_n(const char* s, uint32_t n);
String str_wrap(const char* s);
String str_create(uint32_t len);
void str_free(String s);
String str_duplicate(String s);
String str_substring(String s, uint32_t begin, uint32_t length);
bool str_contains(String s, String what);
bool str_ends_with_i(String s, String suffix);
String str_to_lower(String s);
int str_compare_i(String a, String b);
bool str_equal_i(String a, String b);
int str_compare(String a, String b);
bool str_equal(String a, String b);
String assemble_path(String prefix, String suffix);

