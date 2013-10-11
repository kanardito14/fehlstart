
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include "action.h"

void add_action(GHashTable* map, const char* name, const char* hint, const char* icon, void (*action)(String, Action*))
{
    Action* a = calloc(1, sizeof(Action));
    a->key = str_new(name);
    a->name = str_new(name);
    a->exec = str_new(hint);
    a->icon = str_new(icon);
    a->action = action;
    a->used = true;
    g_hash_table_insert(map, a->key.str, a);
}

void free_action(gpointer data)
{
    Action* a = data;
    str_free(a->key);
    str_free(a->name);
    str_free(a->exec);
    str_free(a->icon);
    str_free(a->mnemonic);
    free(a);
}

void load_launcher(String file, Action* action, bool use_exec_as_hint)
{
    struct stat st;
    stat(file.str, &st);
    action->file_time = st.st_mtime;
    action->action = launch_action;
    GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(file.str);
    if (!info)
        return;
    action->used = !g_desktop_app_info_get_is_hidden(info) && g_app_info_should_show(G_APP_INFO(info));
    if (action->used) {
        GAppInfo* app = G_APP_INFO(info);
        action->name = str_new(g_app_info_get_name(app));
        if (use_exec_as_hint)
            action->exec = str_new(g_app_info_get_executable(app));
        GIcon* icon = g_app_info_get_icon(G_APP_INFO(app));
        if (icon != NULL && Icons_show)
            action->icon = str_own(g_icon_to_string(icon));
    }
    g_object_unref(info);
}

void reload_launcher(Action* action)
{
    str_free(action->name);
    str_free(action->exec);
    str_free(action->icon);
    action->used = false;
    load_launcher(action->key, action);
}

Action* new_launcher(String file)
{
    Action* a = calloc(1, sizeof(Action));
    a->key = file;
    load_launcher(file, a);
    return a;
}

void update_launcher(gpointer key, gpointer value, gpointer user_data)
{
    Action* a = value;
    struct stat st;
    if (a->action != launch_action)
        return;
    if (stat(a->key.str, &st))
        a->used = false;
    else if (a->file_time != st.st_mtime)
        reload_launcher(a);
}

void add_launchers(GHashTable* map, String dir_name, pthread_mutex_t map_mutex)
{
    DIR* dir = opendir(dir_name.str);
    if (!dir)
        return;
    struct dirent* ent = NULL;
    while ((ent = readdir(dir))) {
        String file_name = str_wrap(ent->d_name);
        if (!str_ends_with_i(file_name, STR_S(".desktop")))
            continue;
        String full_path = str_join_path(dir_name, file_name);
        if (g_hash_table_contains(map, full_path.str)) {
            str_free(full_path);
        } else {
            pthread_mutex_lock(&map_mutex);
            g_hash_table_insert(map, full_path.str, new_launcher(full_path));
            pthread_mutex_unlock(&map_mutex);
        }
    }
    closedir(dir);
}

void update_commands(void)
{
    GHashTableIter iter;
    gpointer key = NULL, value = NULL;
    g_hash_table_iter_init(&iter, action_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Action* a = value;
        if (a->action == command_action)
            a->used = false;
    }

    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, commands_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
    char** groups = g_key_file_get_groups(kf, NULL);
    for (unsigned i = 0; groups[i]; i++) {
        String key = str_concat(STR_S("!cmd:"), str_wrap(groups[i]));
        Action* a = g_hash_table_lookup(action_map, key.str);
        if (a) {
            str_free(a->exec);
            str_free(a->icon);
            str_free(key);
        } else {
            a = calloc(1, sizeof(Action));
            a->action = command_action;
            a->name = str_new(groups[i]);
            a->key = key;
            pthread_mutex_lock(&map_mutex);
            g_hash_table_insert(action_map, key.str, a);
            pthread_mutex_unlock(&map_mutex);
        }
        a->exec = str_own(g_key_file_get_string(kf, groups[i], "Exec", NULL));
        a->icon = str_own(g_key_file_get_string(kf, groups[i], "Icon", NULL));
        a->used = true;
    }
    g_strfreev(groups);
    g_key_file_free(kf);
}

void save_actions(void)
{
    GKeyFile* kf = g_key_file_new();
    GHashTableIter iter;
    gpointer key = NULL, value = NULL;
    g_hash_table_iter_init(&iter, action_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Action* a = value;
        if (a->mnemonic.len > 0) {
            g_key_file_set_string(kf, a->key.str, "mnemonic", a->mnemonic.str);
            g_key_file_set_uint64(kf, a->key.str, "time", (uint64_t)a->time);
        }
    }
    save_key_file(kf, action_file);
    g_key_file_free(kf);
}

void load_actions(void)
{
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, action_file, G_KEY_FILE_NONE, NULL)) {
        char** groups = g_key_file_get_groups(kf, NULL);
        for (unsigned i = 0; groups[i]; i++) {
            Action* a = g_hash_table_lookup(action_map, groups[i]);
            if (!a)
                continue;
            char* s = g_key_file_get_string(kf, groups[i], "mnemonic", NULL);
            a->mnemonic = str_own(s);
            a->time = (time_t)g_key_file_get_uint64(kf, groups[i], "time", NULL);
        }
    }
    g_key_file_free(kf);
}
