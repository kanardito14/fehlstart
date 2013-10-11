/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <stdlib.h>
#include <strings.h>
#include "util.h"

const char* get_desktop_env(void)
{
    // see http://standards.freedesktop.org/menu-spec/latest/apb.html
    // the problem with DESKTOP_SESSION is that some distros put their name there
    char* kde0 = getenv("KDE_SESSION_VERSION");
    char* kde1 = getenv("KDE_FULL_SESSION");
    char* gnome = getenv("GNOME_DESKTOP_SESSION_ID");
    char* session = getenv("DESKTOP_SESSION");
    char* current_desktop = getenv("XDG_CURRENT_DESKTOP");
    char* xdg_prefix = getenv("XDG_MENU_PREFIX");

    session = session ? session : "";
    xdg_prefix = xdg_prefix ? xdg_prefix : "";
    current_desktop = current_desktop ? current_desktop : "";

    const char* desktop = "Old";
    if (CONTAINS(session, "kde") || kde0 != NULL || kde1 != NULL)
        desktop = "KDE";
    else if (CONTAINS(session, "gnome") || gnome != NULL)
        desktop = "GNOME";
    else if (CONTAINS(session, "xfce") || CONTAINS(xdg_prefix, "xfce"))
        desktop = "XFCE";
    else if (CONTAINS(session, "lxde") || CONTAINS(current_desktop, "lxde"))
        desktop = "LXDE";
    else if (CONTAINS(session, "rox")) // verify
        desktop = "ROX";

    // TODO: add MATE, Razor, TDE, Unity
    return desktop;
}

bool is_readable_file(const char* file)
{
    FILE* f = fopen(file, "r");
    if (f)
        fclose(f);
    return f != 0;
}

bool file_changed(const char* file, time_t* time)
{
    struct stat st;
    if (!stat(file, &st))
        return false;
    bool changed = (st.st_mtime != *time);
    *time = st.st_mtime;
    return changed;
}

const char* get_home_dir(void)
{
    const char* home = getenv("HOME");
    if (!home)
        home = g_get_home_dir();
    return home;
}

// forks and opens file in an editor and returns immediately
// the plan was that run_editor only returns after the editor exits.
// that way I could reload the settings after changes have been made.
// but xdg-open and friends return immediately so that plan was foiled :(
static void run_editor(const char* file)
{
    if (!is_readable_file(file))
        return;
    pid_t pid = fork();
    if (pid != 0)
        return;
    signal(SIGCHLD, SIG_DFL); // go back to default child behaviour
    execlp("xdg-open", "", file, (char*)0);
    // TODO check if editor is sensible, maybe replace with EDITOR env?
    execlp("x-terminal-emulator", "", "-e", "editor", file, (char*)0);
    execlp("xterm", "", "-e", "vi", file, (char*)0); // getting desperate
    printf("failed to open editor for %s\n", file);
    exit(EXIT_FAILURE);
}

void save_key_file(GKeyFile* kf, const char* file_name)
{
    FILE* f = fopen(file_name, "w");
    if (!f)
        return;
    gsize length = 0;
    char* data = g_key_file_to_data(kf, &length, NULL);
    fwrite(data, 1, length, f);
    g_free(data);
    fclose(f);
}

#if !GLIB_CHECK_VERSION(2,32,0)
static bool g_hash_table_contains(GHashTable* hash_table, gconstpointer key)
{
    return g_hash_table_lookup_extended(hash_table, key, NULL, NULL);
}
#endif

#if !GLIB_CHECK_VERSION(2,26,0)
void g_key_file_set_uint64(GKeyFile* kf, const char* group, const char* key, uint64_t value)
{
    char* str_value = g_strdup_printf("%llu", (unsigned long long)value);
    g_key_file_set_string(kf, group, key, str_value);
    g_free(str_value);
}

uint64_t g_key_file_get_uint64(GKeyFile* kf, const char* group, const char* key, GError** error)
{
    char* value = g_key_file_get_string(kf, group, key, error);
    return value ? g_ascii_strtoull(value, 0, 10) : 0;
}
#endif
