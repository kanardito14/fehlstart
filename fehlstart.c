/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2011 maep and contributors
*
*   some terminology:
*   action: the thing that the user selects
*   launcher: program launcher defined by a .desktop file
*   command: user defined action from commands.rc file
*/

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <strings.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>

#include <keybinder.h>

#include "string.h"

// macros
#define WELCOME_MESSAGE     "type something"
#define NO_MATCH_MESSAGE    "no match"
#define APPLICATION_ICON    "applications-other"
#define DEFAULT_HOTKEY      "<Super>space"
#define ICON_SIZE           GTK_ICON_SIZE_DIALOG
#define DEFAULT_ICON        GTK_STOCK_FIND
#define NO_MATCH_ICON       GTK_STOCK_DIALOG_QUESTION
#define INPUT_STRING_SIZE   0x80
#define SHOW_IGNORE_TIME    100000
#define APPLICATIONS_DIR_0  "/usr/share/applications"
#define APPLICATIONS_DIR_1  "/usr/local/share/applications"
#define USER_APPLICATIONS_DIR   ".local/share/applications"

typedef struct Action {
    String  key;                // map key, .desktop file
    time_t  file_time;          // .desktop time stamp
    String  name;               // display caption
    String  exec;               // executable / hint
    String  mnemonic;           // what user typed
    String  icon;
    int     score;              // calculated prority
    time_t  time;               // last used timestamp
    void    (*action)(String, struct Action*);
    bool    used;               // unused actions are kept for caching
} Action;

//------------------------------------------
// forward declarations

static void launch_action(String, Action*);
static void command_action(String, Action*);
static void edit_settings_action(String, Action*);

//------------------------------------------
// global variables

// preferences
static char*    pref_hotkey = DEFAULT_HOTKEY;
static unsigned pref_hotkey_key;
static bool     pref_match_executable = true;
static bool     pref_show_icon = true;
static bool     pref_one_time;
static char*    pref_border_color = "default";
static int      pref_border_width = 1;
static int      pref_window_width = 200;
static int      pref_window_height = 100;
static GdkModifierType pref_hotkey_mod;

// launcher stuff
static GHashTable*  action_map;
static GArray*      filter_list;
static unsigned     selection;
static char         input_string[INPUT_STRING_SIZE];
static unsigned     input_string_size;
static pthread_mutex_t map_mutex;

// gtk widgets
static GtkWidget*   window;
static GtkWidget*   image;
static GtkWidget*   action_label;
static GtkWidget*   input_label;

// files
static char*    config_file;
static char*    action_file;
static char*    commands_file;
static time_t   config_file_time;
static time_t   commands_file_time;
static char*    user_app_dir;

//------------------------------------------
// helper functions

static bool is_readable_file(const char* file)
{
    FILE* f = fopen(file, "r");
    if (f)
        fclose(f);
    return f != 0;
}

static const char* get_home_dir(void)
{
    const char* home = getenv("HOME");
    if (!home)
        home = g_get_home_dir();
    return home;
}

static String get_first_input_word(void)
{
    for (uint32_t i = 0; i < input_string_size; i++)
        if (input_string[i] == ' ')
            return str_wrap_n(input_string, i);
    return str_wrap_n(input_string, input_string_size);
}

//------------------------------------------
// action functions

static void add_action(const char* name, const char* hint, const char* icon, void (*action)(String, Action*))
{
    Action* a = calloc(1, sizeof(Action));
    a->key = str_new(name);
    a->name = str_new(name);
    a->exec = str_new(hint);
    a->icon = str_new(icon);
    a->action = action;
    a->used = true;
    g_hash_table_insert(action_map, a->key.str, a);
}

static void free_action(gpointer data)
{
    Action* a = data;
    str_free(a->key);
    str_free(a->name);
    str_free(a->icon);
    str_free(a->exec);
    str_free(a->mnemonic);
    free(a);
}

static void load_launcher(String file, Action* action)
{
    struct stat st = {0};
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
        if (pref_match_executable)
            action->exec = str_new(g_app_info_get_executable(app));
        GIcon* icon = g_app_info_get_icon(G_APP_INFO(app));
        if (icon != NULL && pref_show_icon)
            action->icon = str_own(g_icon_to_string(icon));
    }
    g_object_unref(info);
}

static void reload_launcher (Action* action)
{
    str_free(action->name);
    str_free(action->exec);
    str_free(action->icon);
    action->used = false;
    load_launcher(action->key, action);
}

static Action* new_launcher(String file)
{
    Action* a = calloc(1, sizeof(Action));
    a->key = file;
    load_launcher(file, a);
    return a;
}

static void update_launcher(gpointer key, gpointer value, gpointer user_data)
{
    Action* a = value;
    struct stat st = {0};
    if (a->action != launch_action)
        return;
    if (stat(a->key.str, &st))
        a->used = false;
    else if (a->file_time != st.st_mtime)
        reload_launcher(a);
}

static void add_launchers(String dir_name)
{
    DIR* dir = opendir(dir_name.str);
    if (!dir)
        return;
    printf("reading %s\n", dir_name.str);
    struct dirent* ent = NULL;
    while ((ent = readdir(dir))) {
        String file_name = str_wrap(ent->d_name);
        if (!str_ends_with_i(file_name, STR_S(".desktop")))
            continue;
        String full_path = str_assemble_path(dir_name, file_name);
        if (g_hash_table_contains(action_map, full_path.str)) {
            str_free(full_path);
        } else {
            pthread_mutex_lock(&map_mutex);
            g_hash_table_insert(action_map, full_path.str, new_launcher(full_path));
            pthread_mutex_unlock(&map_mutex);
        }
    }
    closedir(dir);
}

static void update_commands()
{
    struct stat st = {0};
    if (!stat(commands_file, &st) && st.st_mtime == commands_file_time)
        return;

    GHashTableIter iter = {0};
    gpointer key = NULL, value = NULL;
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

static void* update_actions(void* user_data)
{
    update_commands();
    g_hash_table_foreach(action_map, update_launcher, NULL);
    add_launchers(STR_S(APPLICATIONS_DIR_0));
    add_launchers(STR_S(APPLICATIONS_DIR_1));
    add_launchers(STR_S(USER_APPLICATIONS_DIR));
    return NULL;
}

//------------------------------------------
// filter functions

static void filter_scrore_add(gpointer key, gpointer value, gpointer user_data)
{
    Action* a = value;
    String filter = *(String*)user_data;
    if (!a->used)
        return;

    a->score = -1;
    if (str_starts_with(a->mnemonic, filter))
        a->score = 100000;

    if (a->score < 0) {
        unsigned pos = str_find_first_i(a->name, filter);
        if (pos != STR_END)
            a->score = 100 + (filter.len - pos);
    }

    if (a->score < 0) {
        unsigned pos = str_find_first_i(a->exec, filter);
        if (pos != STR_END)
            a->score = 1 + (filter.len - pos);
    }
    if (a->score > 0)
        g_array_append_val(filter_list, a);
}

static int compare_score(gconstpointer a, gconstpointer b)
{
    Action* a1 = *(Action**)a;
    Action* a2 = *(Action**)b;
    return (a2->score - a1->score) + (a2->time < a1->time ? -1 : 1);
}

static void filter_action_list(String filter)
{
    if (filter.len == 0)
        return;
    g_array_remove_range(filter_list, 0, filter_list->len);
    g_hash_table_foreach(action_map, filter_scrore_add, &filter);
    g_array_sort(filter_list, compare_score);
}

static void run_selected(void)
{
    if (filter_list->len > 0 && selection < filter_list->len) {
        Action* a = g_array_index(filter_list, Action*, selection);
        if (!a->action != 0)
            return;
        str_free(a->mnemonic);
        a->mnemonic = str_duplicate(get_first_input_word());
        a->time = time(NULL);
        String str = str_wrap_n(input_string, input_string_size);
        a->action(str, a);
    }
}

//------------------------------------------
// gui functions

static void image_set_icon(GtkImage* img, const char* name)
{
    if (!pref_show_icon) {
        gtk_image_clear(img);
        return;
    }

    GIcon* icon = NULL;
    if (g_path_is_absolute(name)) {
        GFile* file = g_file_new_for_path(name);
        icon = g_file_icon_new(file);
        g_object_unref(file);
    } else {
        icon = g_themed_icon_new(name);
    }
    gtk_image_set_from_gicon(img, icon, ICON_SIZE);
    g_object_unref(icon);
}

static void show_selected(void)
{
    const char* action_text = NO_MATCH_MESSAGE;
    const char* icon_name = NO_MATCH_ICON;

    if (input_string_size == 0) {
        action_text = WELCOME_MESSAGE;
        icon_name = DEFAULT_ICON;
    } else if (filter_list->len > 0) {
        Action* a = g_array_index(filter_list, Action*, selection);
        action_text = a->name.str;
        icon_name = a->icon.str;
    }

    gtk_label_set_text(GTK_LABEL(input_label), input_string);
    gtk_label_set_text(GTK_LABEL(action_label), action_text);
    image_set_icon(GTK_IMAGE(image), icon_name);

    gtk_widget_queue_draw(input_label);
    gtk_widget_queue_draw(action_label);
    gtk_widget_queue_draw(image);
}

static void handle_text_input(GdkEventKey* event)
{
    if (event->keyval == GDK_BackSpace && input_string_size > 0)
        input_string_size--;
    else if (event->length == 1 &&
             (input_string_size + 1) < INPUT_STRING_SIZE &&
             (input_string_size > 0 || event->keyval != GDK_space))
        input_string[input_string_size++] = event->keyval;

    input_string[input_string_size] = 0;
    filter_action_list(get_first_input_word());
    selection = 0;
}

static void hide_window(void)
{
    if (!gtk_widget_get_visible(window))
        return;

    gdk_keyboard_ungrab(GDK_CURRENT_TIME);

    if (pref_one_time) // configured for one-time use
        gtk_main_quit();

    gtk_widget_hide(window);
    input_string[0] = 0;
    selection = 0;
}

static void show_window(void)
{
    if (gtk_widget_get_visible(window))
        return;

    pthread_t thread = 0;
    pthread_create(&thread, NULL, update_actions, NULL);

    show_selected();
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_present(GTK_WINDOW(window));
    gtk_window_set_keep_above(GTK_WINDOW(window), true);
    gdk_keyboard_grab(window->window, true, GDK_CURRENT_TIME);
    gdk_pointer_grab(window->window, true, GDK_BUTTON_PRESS_MASK, NULL, NULL, GDK_CURRENT_TIME);
}

static void toggle_window(const char *keystring, void *data)
{
    if (gtk_widget_get_visible(window))
        hide_window();
    else
        show_window();
}

static gboolean button_press_event(GtkWidget* widget, GdkEvent *event, gpointer data)
{
    hide_window();
    return true;
}

static void destroy(GtkWidget* widget, gpointer data)
{
    gtk_main_quit();
}

static gboolean delete_event(GtkWidget* widget, GdkEvent* event, gpointer data)
{
    return true;
}

static gboolean key_press_event(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
    pthread_mutex_lock(&map_mutex);
    switch (event->keyval) {
    case GDK_Escape:
        hide_window();
        break;
    case GDK_KP_Enter:
    case GDK_Return:
        run_selected();
        hide_window();
        break;
    case GDK_Up:
    case GDK_Tab:
    case GDK_Down:
        if (filter_list->len) {
            selection += (event->keyval == GDK_Up) ? filter_list->len - 1 : 1;
            selection %= filter_list->len;
        }
        show_selected();
        break;
    default:
        // the keybind-thingie doesn't work when the popup window
        // grabs the keyboard focus, it has to be caught like this
        if ((event->state & pref_hotkey_mod) && (event->keyval == pref_hotkey_key)) {
            hide_window();
        } else {
            handle_text_input(event);
            show_selected();
        }
        break;
    }
    pthread_mutex_unlock(&map_mutex);
    return true;
}

static gboolean expose_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    double r = 0, g = 0, b = 0;
    GdkColor color;

    if (!strcasecmp(pref_border_color, "default"))
        color = gtk_widget_get_style(window)->bg[GTK_STATE_SELECTED];
    else
        gdk_color_parse(pref_border_color, &color);

    r = (double)color.red / 0xffff;
    g = (double)color.green / 0xffff;
    b = (double)color.blue / 0xffff;

    int width = 0, height = 0;
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);

    cairo_t* cr = gdk_cairo_create(widget->window);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, 0, 0, width, height);

    /* because we're drawing right at the edge of the window, we need to double
     * the border width to get the expected result */
    cairo_set_line_width(cr, pref_border_width * 2);
    cairo_stroke(cr);
    cairo_destroy(cr);

    return false;
}

//------------------------------------------
// config files

static void key_file_save(GKeyFile* kf, const char* file_name)
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

#if !GLIB_CHECK_VERSION(2,26,0)
static void g_key_file_set_uint64(GKeyFile* kf, const char* group, const char* key, uint64_t value)
{
    char* str_value = g_strdup_printf("%llu", (unsigned long long)value);
    g_key_file_set_string(kf, group, key, str_value);
    g_free(str_value);
}

static uint64_t g_key_file_get_uint64(GKeyFile* kf, const char* group, const char* key, GError** error)
{
    char* value = g_key_file_get_string(kf, group, key, error);
    return value ? g_ascii_strtoull(value, 0, 10) : 0;
}
#endif

// macro for writing to keyfile
#define WRITE_PREF(type, group, key, var) g_key_file_set_##type (kf, group, key, pref_##var)
static void save_config(void)
{
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, config_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
    WRITE_PREF(string, "Bindings", "launch", hotkey);
    WRITE_PREF(boolean, "Matching", "executable", match_executable);
    WRITE_PREF(boolean, "Icons", "show", show_icon);
    WRITE_PREF(string, "Border", "color", border_color);
    WRITE_PREF(integer, "Border", "width", border_width);
    WRITE_PREF(integer, "Window", "width", window_width);
    WRITE_PREF(integer, "Window", "height", window_height);
    key_file_save(kf, config_file);
    g_key_file_free(kf);
}
#undef WRITE_PREF

// macro for reading from keyfile, without overwriting default values
#define READ_PREF(type, group, key, var)            \
    if (g_key_file_has_key(kf, group, key, NULL))   \
        pref_##var = g_key_file_get_##type (kf, group, key, NULL)
static void read_config(void)
{
    struct stat st = {0};
    if (!stat(config_file, &st) && st.st_mtime == config_file_time)
        return;
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_file, G_KEY_FILE_NONE, NULL)) {
        READ_PREF(string, "Bindings", "launch", hotkey);
        READ_PREF(boolean, "Matching", "executable", match_executable);
        READ_PREF(boolean, "Icons", "show", show_icon);
        READ_PREF(string, "Border", "color", border_color);
        READ_PREF(integer, "Border", "width", border_width);
        READ_PREF(integer, "Window", "width", window_width);
        READ_PREF(integer, "Window", "height", window_height);
    }
    g_key_file_free(kf);
}
#undef READ_PREF

static void save_actions(void)
{
    GKeyFile* kf = g_key_file_new();
    GHashTableIter iter = {0};
    gpointer key = NULL, value = NULL;
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Action* a = value;
        if (a->mnemonic.len > 0) {
            g_key_file_set_string(kf, a->key.str, "mnemonic", a->mnemonic.str);
            g_key_file_set_uint64(kf, a->key.str, "time", (uint64_t)a->time);
        }
    }
    key_file_save(kf, action_file);
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

static void init_config_files(void)
{
    gchar* dir = g_build_filename(g_get_user_config_dir(), "fehlstart", NULL);
    g_mkdir_with_parents(dir, 0700);
    config_file = g_build_filename(dir, "fehlstart.rc", NULL);
    action_file = g_build_filename(dir, "actions.rc", NULL);
    commands_file = g_build_filename(dir, "commands.rc", NULL);
    g_free(dir);
    atexit(save_config);
    atexit(save_actions);
}

//------------------------------------------
// misc

// opens file in an editor and returns immediately
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
    execlp("x-terminal-emulator", "", "-e", "editor", file, (char*)0);
    execlp("xterm", "", "-e", "vi", file, (char*)0); // getting desperate
    printf("failed to open editor for %s\n", file);
    exit(EXIT_FAILURE);
}

static void register_hotkey(void)
{
    if (pref_one_time)
        return;
    keybinder_init();
    if (keybinder_bind(pref_hotkey, toggle_window, NULL)) {
        gtk_accelerator_parse(pref_hotkey, &pref_hotkey_key, &pref_hotkey_mod);
        printf("hit %s to show window\n", pref_hotkey);
    } else {
        GtkWidget* dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                            GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
                            "Fehlstart can't use hotkey '%s'.", pref_hotkey);
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Quit", GTK_RESPONSE_REJECT,
                            "Quit and Edit Settings", GTK_RESPONSE_ACCEPT, NULL);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            edit_settings_action(STR_S(""), NULL); // launch editor
        gtk_widget_destroy(dialog);
        exit(EXIT_FAILURE);
    }
}

static void create_widgets(void)
{
    window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_size_request(window, pref_window_width, pref_window_height);
    gtk_window_set_resizable(GTK_WINDOW(window), false);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK);
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_event), 0);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy), 0);
    g_signal_connect(window, "key-press-event", G_CALLBACK(key_press_event), 0);
    g_signal_connect(window, "button-press-event", G_CALLBACK(button_press_event), 0);

    GtkWidget* vbox = gtk_vbox_new(false, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    g_signal_connect(vbox, "expose-event", G_CALLBACK(expose_event), 0);
    gtk_widget_show(vbox);

    image = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), image, true, false, 0);
    gtk_widget_show(image);

    action_label = gtk_label_new(WELCOME_MESSAGE);
    gtk_box_pack_start(GTK_BOX(vbox), action_label, true, true, 0);
    gtk_widget_show(action_label);

    input_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), input_label, true, false, 0);
    gtk_widget_show(input_label);
}

// strcasestr it a gnu extension, b must be a static cstring
#define CONTAINS(a, b) str_contains_i(str_wrap(a), STR_S(b))
static const char* get_desktop_env(void)
{
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
    else if (CONTAINS(session, "rox"))
        desktop = "ROX";

    printf("detected desktop: %s\n", desktop);
    return desktop;
}
#undef CONTAINS

static void parse_commandline(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--one-way")) {
            pref_one_time = true;
        } else if (!strcmp(argv[i], "--help")) {
            printf("fehlstart 0.3.0 (c) 2012 maep\noptions:\n"\
                   "\t--one-way\texit after one use\n");
            exit(EXIT_SUCCESS);
        } else {
            printf("invalid option: %s\n", argv[i]);
        }
    }
}

//------------------------------------------
// actions

static void quit_action(String command, Action* action)
{
    gtk_main_quit();
}

static void launch_action(String command, Action* action)
{
    if (fork() != 0)
        return;
    setsid();                   // "detatch" from parent process
    signal(SIGCHLD, SIG_DFL);   // back to default child behaviour
    action->action(command, action);
    GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(action->key.str);
    if (info != 0)
        g_app_info_launch(G_APP_INFO(info), NULL, NULL, NULL);
    g_object_unref(info);
    exit(EXIT_SUCCESS);
}

static void command_action(String command, Action* action)
{
    if (fork() != 0)
        return;
    setsid();                   // "detatch" from parent process
    signal(SIGCHLD, SIG_DFL);   // back to default child behaviour
    // treat everythin after first word as arguments
    unsigned sp = str_find_first(command, STR_S(" "));
    String cmd = str_concat(action->key, str_substring(command, sp, STR_END));
    if (system(cmd.str)) {}; // shut up gcc warning
    str_free(cmd);
}

static void edit_settings_action(String command, Action* action)
{
    save_config();
    run_editor(config_file);
}

static void edit_commands_action(String command, Action* action)
{
    if (!is_readable_file(commands_file)) {
        FILE* f = fopen(commands_file, "w");
        if (!f)
            return;
        fputs("#example: run a command in xterm\n#'run top' will start top in xterm\n"\
              "#[Run in Terminal]\n#Exec=xterm -e\n#Icon=terminal\n", f);
        fclose(f);
    }
    run_editor(commands_file);
}

//------------------------------------------
// main

int main (int argc, char** argv)
{
    gtk_init(&argc, &argv);
    parse_commandline(argc, argv);

    signal(SIGCHLD, SIG_IGN); // let kernel raep the children, mwhahaha
    g_chdir(get_home_dir());
    g_desktop_app_info_set_desktop_env(get_desktop_env());
    user_app_dir = g_build_filename(get_home_dir(), USER_APPLICATIONS_DIR, NULL);
    action_map = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free_action);
    filter_list = g_array_sized_new (false, true, sizeof(Action*), 250);

    init_config_files();
    read_config();
    add_action("quit fehlstart", "exit", GTK_STOCK_QUIT, quit_action);
    add_action("fehlstart settings", "config preferences", GTK_STOCK_PREFERENCES, edit_settings_action);
    add_action("fehlstart actions", "commands", GTK_STOCK_EXECUTE, edit_commands_action);
    update_actions(NULL);
    load_actions();

    create_widgets();
    if (pref_one_time) // one-time use
        show_window();
    else
        register_hotkey();

    gtk_main();
    return EXIT_SUCCESS;
}
