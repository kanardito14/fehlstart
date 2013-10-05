/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

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
    P(integer, Window,   width,      200) \
    P(integer, Window,   height,     100) \
    P(boolean, Window,   round,      true) \
    P(boolean, Window,   arch,       true) \
    P(string,  Labels,   color,      "default") \
    P(integer, Labels,   size1,      14) \
    P(integer, Labels,   size2,      12) \
    P(boolean, Labels,   showinput,  true)

#define P(type, group, key, value) extern type group##_##key;
PREFERENCES_LIST
#undef P
