/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>

// pi constant, M_PI is not always present
#define PI 0x1.921fb54442d18p+1

// guesses desktop environment
// may return KDE, GNOME, XFCE, LXDE, ROX
// returned pointer must not be freed
const char* get_desktop_env(void);

// retuns true if file is readable
bool is_readable_file(const char* file);

// return the path of the home dir
// based on HOME variable or glib fallback
const char* get_home_dir(void);

// saves a GKeyFile object to file
// there is no save equivilant to g_key_file_load_from_file
void save_key_file(GKeyFile* kf, const char* file_name);

// provide missing functions for old glib 
#if !GLIB_CHECK_VERSION(2,32,0)
    bool g_hash_table_contains(GHashTable* hash_table, gconstpointer key);
#endif
#if !GLIB_CHECK_VERSION(2,26,0)
    void g_key_file_set_uint64(GKeyFile* kf, const char* group, const char* key, uint64_t value);
    uint64_t g_key_file_get_uint64(GKeyFile* kf, const char* group, const char* key, GError** error);
#endif

// return minimum of a and b
inline static int imin(int a, int b)
{
    return a < b ? a : b;
}

// returns v if min < v < max, else min or max
inline static int iclamp(int v, int min, int max) 
{ 
    return v < min ? min : v > max ? max : v; 
}

