/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <strings.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>

#include <keybinder.h>

#include "str.h"

// types

typedef struct Action {
    String      key;                // map key, .desktop file
    time_t      file_time;          // .desktop time stamp
    String      name;               // display caption
    String      exec;               // executable / hint
    String      mnemonic;           // what user typed
    String      icon;
    int         score;              // calculated prority
    time_t      time;               // last used timestamp
    void        (*action)(String, struct Action*);
    bool        used;               // unused actions are cached to speed scans
} Action;

typedef struct {
    double      r;
    double      g;
    double      b;
    double      a;
} Color;

typedef char* string;
typedef bool boolean;
typedef int integer;

typedef struct Settings {
    #define SETTING(type, group, key, value) type group##_##key;
    #include "settings.def"
    #undef SETTING
    bool one_time;
} Settings;

// forward declarations

static void launch_action(String, Action*);
static void command_action(String, Action*);
static void edit_settings_action(String, Action*);
static void* update_all(void*);

// macros
#define WELCOME_MESSAGE         "..."
#define NO_MATCH_MESSAGE        "???"
#define APPLICATION_ICON        "applications-other"
#define DEFAULT_HOTKEY          "<Super>space"
#define DEFAULT_ICON            GTK_STOCK_FIND
#define NO_MATCH_ICON           GTK_STOCK_DIALOG_QUESTION
#define INPUT_STRING_SIZE       20
#define SHOW_IGNORE_TIME        100000
#define PI                      (0x1.921fb54442d18p+1)
#define APPLICATIONS_DIR_0      "/usr/share/applications"
#define APPLICATIONS_DIR_1      "/usr/local/share/applications"
#define APPLICATIONS_DIR_2      "/usr/share/applications/kde4"
#define USER_APPLICATIONS_DIR   ".local/share/applications"
#define COUNTOF(array)          (sizeof array / sizeof array[0])

// preferences
static Settings settings = {
    #define SETTING(type, group, name, value) .group##_##name = value,
    #include "settings.def"
    #undef SETTING
    .one_time = false
};

// launcher stuff
static GHashTable*      action_map;
static GArray*          filter_list;
static unsigned         selection;
static char             input_string[INPUT_STRING_SIZE];
static unsigned         input_string_size;
static pthread_mutex_t  map_mutex;

// user interface
static unsigned         hotkey_key;
static GdkModifierType  hotkey_mod;
static GdkPixbuf*       icon_pixbuf;
static GtkWidget*       window;
static const char*      action_name;

// files
static char*            setting_file;
static char*            mnemonic_file;
static char*            commands_file;
static char*            user_app_dir;

//------------------------------------------
// helper functions

inline static int imin(int a, int b) { return a < b ? a : b; }

inline static int iclamp(int v, int min, int max) { return v < min ? min : v > max ? max : v; }

static bool is_readable_file(const char* file)
{
    FILE* f = fopen(file, "r");
    if (f)
        fclose(f);
    return f != 0;
}

static bool timestamp_changed(const char* file, time_t* timestamp)
{
    struct stat st;
    bool changed = !(stat(file, &st) && st.st_mtime == *timestamp);
    *timestamp = st.st_mtime;
    return changed;
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

#if !GLIB_CHECK_VERSION(2,32,0)
static bool g_hash_table_contains(GHashTable* hash_table, gconstpointer key)
{
    return g_hash_table_lookup_extended(hash_table, key, NULL, NULL);
}
#endif

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

static void load_launcher(String file, Action* action, bool match_executable)
{
    timestamp_changed(file.str, &action->file_time);
    action->action = launch_action;
    GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(file.str);
    if (!info)
        return;
    action->used = !g_desktop_app_info_get_is_hidden(info) && g_app_info_should_show(G_APP_INFO(info));
    if (action->used) {
        GAppInfo* app = G_APP_INFO(info);
        action->name = str_new(g_app_info_get_name(app));
        if (match_executable)
            action->exec = str_new(g_app_info_get_executable(app));
        GIcon* icon = g_app_info_get_icon(G_APP_INFO(app));
        if (icon)
            action->icon = str_own(g_icon_to_string(icon));
    }
    g_object_unref(info);
}

static void reload_launcher (Action* action, bool match_executable)
{
    str_free(action->name);
    str_free(action->exec);
    str_free(action->icon);
    action->used = false;
    load_launcher(action->key, action, match_executable);
}

static Action* new_launcher(String file, bool match_executable)
{
    Action* a = calloc(1, sizeof(Action));
    a->key = file;
    load_launcher(file, a, match_executable);
    return a;
}

static void update_launcher(gpointer key, gpointer value, gpointer user_data)
{
    Action* a = value;
    struct stat st;
    if (a->action != launch_action)
        return;
    if (stat(a->key.str, &st))
        a->used = false; // stat fails if file doesn't exist
    else if (a->file_time != st.st_mtime)
        reload_launcher(a, settings.Matching_executable);
}

static void add_launchers(String dir_name)
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
        if (g_hash_table_contains(action_map, full_path.str)) {
            str_free(full_path);
        } else {
            pthread_mutex_lock(&map_mutex);
            Action* launcher = new_launcher(full_path, settings.Matching_executable);
            g_hash_table_insert(action_map, full_path.str, launcher);
            pthread_mutex_unlock(&map_mutex);
        }
    }
    closedir(dir);
}

static void update_commands(void)
{
    static time_t commands_file_time;
    if (!timestamp_changed(commands_file, &commands_file_time))
        return;

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

    if (a->score > 0) {
        a->score += a->mnemonic.len > 0;
        g_array_append_val(filter_list, a);
    }
}

static int compare_score(gconstpointer a, gconstpointer b)
{
    Action* a1 = *(Action**)a;
    Action* a2 = *(Action**)b;
    return (a2->score - a1->score) + (a2->time < a1->time ? -1 : 1);
}

static void filter_action_list(String filter)
{
    if (filter_list->len)
        g_array_remove_range(filter_list, 0, filter_list->len);
    if (filter.len == 0)
        return;
    g_hash_table_foreach(action_map, filter_scrore_add, &filter);
    g_array_sort(filter_list, compare_score);
}

static void run_selected(void)
{
    if (!filter_list->len || selection >= filter_list->len)
        return;
    Action* a = g_array_index(filter_list, Action*, selection);
    if (!a->action != 0)
        return;
    str_free(a->mnemonic);
    a->mnemonic = str_duplicate(get_first_input_word());
    a->time = time(NULL);
    String str = str_wrap_n(input_string, input_string_size);
    a->action(str, a);
}

//------------------------------------------
// gui functions

static void rectangle(cairo_t* cr, double x, double y, double w, double h, double r)
{
    if (r > 0) {
        cairo_arc(cr, x + w - r, y + r, r, 1.5 * PI, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * PI);
        cairo_arc(cr, x + r, y + h - r, r, 0.5 * PI, PI);
        cairo_arc(cr, x + r, y + r, r, PI, 1.5 * PI);
        cairo_close_path(cr);
    } else {
        cairo_rectangle(cr, 0, 0, w, h);
    }
}

static Color parse_color(const char* color_name, GdkColor default_color)
{
    GdkColor c = {0, 0, 0, 0};
    if (!gdk_color_parse(color_name, &c))
        c = default_color;
    Color col = {c.red / 65535.0, c.green / 65535.0, c.blue / 65535.0, 1};
    return col;
}

static void draw_labels(cairo_t* cr, Settings* set, GtkStyle* sty, const char* action, const char* input)
{
    cairo_text_extents_t extents;
    Color c = parse_color(set->Labels_color, sty->text[GTK_STATE_SELECTED]);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);

    int max_width = set->Window_width - set->Border_width * 2;
    int size = set->Labels_size1;
    do {
        cairo_set_font_size(cr, size--);
        cairo_text_extents(cr, action, &extents);
    } while (extents.width > max_width && size > 6);
    double x = (set->Window_width - extents.width) / 2.0;
    double y = set->Window_height * 0.75;
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, action);

    if (set->Labels_showinput) {
        cairo_set_font_size(cr, set->Labels_size2);
        cairo_text_extents(cr, input, &extents);
        x = (set->Window_width - extents.width) / 2.0;
        y = set->Window_height - set->Border_width * 2.0;
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, input);
    }
}

static void draw_icon(cairo_t* cr, Settings* set, GdkPixbuf* icon)
{
    if (!icon)
        return;
    double x = (set->Window_width - gdk_pixbuf_get_width(icon)) / 2.0;
    double y = fmax(set->Window_height / 2.0 - gdk_pixbuf_get_height(icon), set->Border_width);
    gdk_cairo_set_source_pixbuf(cr, icon, x, y);
    cairo_paint(cr);
}

static void draw_dots(cairo_t* cr, Settings* set, GtkStyle* sty, int index, int max)
{
    if (max < 1)
        return;
    double r = 2.0; // circle radius
    double w = set->Window_width;
    double y = set->Window_height / 2.0;
    Color c = parse_color(set->Labels_color, sty->text[GTK_STATE_SELECTED]);

    for (int i = 0; i < imin(3, index); i++)
        cairo_arc(cr, r * 3 * (i + 1), y, r, 0, 2 * PI);
    for (int i = 0; i < imin(3, max - index - 1); i++)
        cairo_arc(cr, w - (r * 3 * (i + 1)), y, r, 0, 2 * PI);

    cairo_close_path(cr);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_fill(cr);
}

static void draw_window(cairo_t* cr, Settings* set, GtkStyle* sty)
{
    double w = set->Window_width;
    double h = set->Window_height;
    double brad = set->Window_round ? fmax(w, h) / 10 : 0; // corner radius
    double w2 = w / 2, h3 = w * 3;
    double crad = sqrt(w2 * w2 + h3 * h3);
    Color c = parse_color(set->Window_color, sty->bg[GTK_STATE_SELECTED]);

    rectangle(cr, 0, 0, w, h, brad);
    cairo_clip(cr);

    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_paint(cr);

    if (set->Window_arch) {
        cairo_move_to(cr, 0, 0);
        cairo_line_to(cr, w, 0);
        // TODO properly calculate angles (asin/acos)
        cairo_arc_negative(cr, w2, h * 0.6 + h3, crad, -0.10 * PI, -0.80 * PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
        cairo_fill(cr);
    }

    c = parse_color(set->Border_color, sty->text[GTK_STATE_SELECTED]);
    rectangle(cr, 0, 0, w, h, brad);
    cairo_set_line_width(cr, set->Border_width * 2);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_stroke(cr);
}

static void clear(cairo_t* cr)
{
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

static GdkPixbuf* load_icon(const char* name, Settings* set)
{
    const int sizes[] = {256, 128, 48, 32};
    if (!set->Icons_show)
        return NULL;
    // if the size is uncommon, svgs might be used which load
    // too slow on my atom machine
    int h = set->Window_height / 2;
    if (!set->Icons_scale) {
        int i = 0;
        for (; i < (int)COUNTOF(sizes) && h < sizes[i]; i++) {}
        h = sizes[i];
    }
    if (g_path_is_absolute(name)) {
        return gdk_pixbuf_new_from_file_at_scale(name, -1, h, true, NULL);
    } else {
        int flags = GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_USE_BUILTIN;
        GtkIconTheme* theme = gtk_icon_theme_get_default();
        return gtk_icon_theme_load_icon(theme, name, h, flags, NULL);
    }
}

static void show_selected(void)
{
    const char* icon_name = NO_MATCH_ICON;
    action_name = NO_MATCH_MESSAGE;

    if (input_string_size == 0) {
        action_name = WELCOME_MESSAGE;
        icon_name = DEFAULT_ICON;
    } else if (filter_list->len > 0) {
        Action* a = g_array_index(filter_list, Action*, selection);
        action_name = a->name.str;
        icon_name = a->icon.str;
    }

    if (icon_pixbuf)
        g_object_unref(icon_pixbuf);
    icon_pixbuf = load_icon(icon_name, &settings);
    gtk_widget_queue_draw(window);
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

    if (settings.one_time) // configured for one-time use
        gtk_main_quit();

    gtk_widget_hide(window);
    input_string[0] = 0;
    input_string_size = 0;
    selection = 0;
    if (filter_list->len)
        g_array_remove_range(filter_list, 0, filter_list->len);
}

static void show_window(void)
{
    if (gtk_widget_get_visible(window))
        return;

    pthread_t thread = 0;
    pthread_create(&thread, NULL, update_all, NULL);
    show_selected();
    gtk_widget_set_size_request(window, settings.Window_width, settings.Window_height);
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
    case GDK_Left:
    case GDK_Up:
        if (filter_list->len)
            selection = (selection + filter_list->len - 1) % filter_list->len;
        show_selected();
        break;
    case GDK_Right:
    case GDK_Tab:
    case GDK_Down:
        if (filter_list->len)
            selection = (selection + 1) % filter_list->len;
        show_selected();
        break;
    default:
        // libkeybinder doesn't work when the popup window grabs the keyboard focus, it has to be caught manually
        if ((event->state & hotkey_mod) && (event->keyval == hotkey_key)) {
            hide_window();
            break;
        }
        handle_text_input(event);
        show_selected();
        break;
    }
    pthread_mutex_unlock(&map_mutex);
    return true;
}

static void screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer userdata)
{
    GdkScreen *screen = gtk_widget_get_screen(widget);
    GdkColormap *colormap = gdk_screen_get_rgba_colormap(screen);
    if (!colormap)
        colormap = gdk_screen_get_rgb_colormap(screen);
    gtk_widget_set_colormap(widget, colormap);
}

static gboolean expose_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    cairo_t* cr = gdk_cairo_create(widget->window);
    GtkStyle* sty = gtk_widget_get_style(window);
    clear(cr);
    draw_window(cr, &settings, sty);
    draw_icon(cr, &settings, icon_pixbuf);
    draw_dots(cr, &settings, sty, selection, filter_list->len);
    draw_labels(cr, &settings, sty, action_name, input_string);
    cairo_destroy(cr);
    return false;
}

static void create_widgets(void)
{
    window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_app_paintable(window, true);
    gtk_window_set_resizable(GTK_WINDOW(window), false);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK);
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_event), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(key_press_event), NULL);
    g_signal_connect(window, "button-press-event", G_CALLBACK(button_press_event), NULL);
    g_signal_connect(window, "expose-event", G_CALLBACK(expose_event), NULL);
    g_signal_connect(window, "screen-changed", G_CALLBACK(screen_changed), NULL);

    screen_changed(window, NULL, NULL);
}

//------------------------------------------
// misc

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

void read_settings(const char* file_name, Settings* set)
{
    static time_t config_file_time;
    if (!timestamp_changed(file_name, &config_file_time))
        return;

    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, file_name, G_KEY_FILE_NONE, NULL)) {
        #define SETTING(type, group, key, value) if (g_key_file_has_key(kf, #group, #key, NULL))\
            set->group##_##key = g_key_file_get_##type(kf, #group, #key, NULL);
        #include "settings.def"
        #undef SETTING
    }
    g_key_file_free(kf);
    set->Border_width = iclamp(set->Border_width, 0, 20);
    set->Window_width = iclamp(set->Window_width, 200, 800);
    set->Window_height = iclamp(set->Window_height, 100, 800);
    set->Labels_size1 = iclamp(set->Labels_size1, 6, 32);
    set->Labels_size2 = iclamp(set->Labels_size2, 6, 32);
}

void save_settings(const char* file_name, Settings* set)
{
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, file_name, G_KEY_FILE_KEEP_COMMENTS, NULL);
    #define SETTING(type, group, key, value) g_key_file_set_##type (kf, #group, #key, set->group##_##key);
    #include "settings.def"
    #undef SETTING
    key_file_save(kf, file_name);
    g_key_file_free(kf);
}

void save_mnemonics(const char* file_name, GHashTable* map)
{
    GKeyFile* kf = g_key_file_new();
    GHashTableIter iter;
    gpointer key = NULL, value = NULL;
    g_hash_table_iter_init(&iter, map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Action* a = value;
        if (a->mnemonic.len > 0) {
            g_key_file_set_string(kf, a->key.str, "mnemonic", a->mnemonic.str);
            g_key_file_set_uint64(kf, a->key.str, "time", (uint64_t)a->time);
        }
    }
    key_file_save(kf, file_name);
    g_key_file_free(kf);
}

void load_mnemonics(const char* file_name, GHashTable* map)
{
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, file_name, G_KEY_FILE_NONE, NULL)) {
        char** groups = g_key_file_get_groups(kf, NULL);
        for (unsigned i = 0; groups[i]; i++) {
            Action* a = g_hash_table_lookup(map, groups[i]);
            if (!a)
                continue;
            char* s = g_key_file_get_string(kf, groups[i], "mnemonic", NULL);
            a->mnemonic = str_own(s);
            a->time = (time_t)g_key_file_get_uint64(kf, groups[i], "time", NULL);
        }
    }
    g_key_file_free(kf);
}

static void* update_all(void* user_data)
{
    read_settings(setting_file, &settings);
    update_commands();
    g_hash_table_foreach(action_map, update_launcher, NULL);
    add_launchers(STR_S(APPLICATIONS_DIR_0));
    add_launchers(STR_S(APPLICATIONS_DIR_1));
    add_launchers(STR_S(APPLICATIONS_DIR_2));
    add_launchers(STR_S(user_app_dir));
    return NULL;
}

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

static void register_hotkey(const char* binding)
{
    keybinder_init();
    if (keybinder_bind(binding, toggle_window, NULL)) {
        gtk_accelerator_parse(binding, &hotkey_key, &hotkey_mod);
        printf("hit %s to show window\n", binding);
    } else {
        GtkWidget* dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_NONE, "Hotkey '%s' is already being used!", binding);
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Edit Settings", GTK_RESPONSE_ACCEPT,
            "Cancel", GTK_RESPONSE_REJECT, NULL);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            edit_settings_action(STR_S(""), NULL); // launch editor
        gtk_widget_destroy(dialog);
        exit(EXIT_FAILURE);
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
    unsigned sp = str_find_first(command, STR_S(" ")); // everything after first space is arguments
    String cmd = str_concat(action->exec, str_substring(command, sp, STR_END));
    if (system(cmd.str)) {};    // shut up gcc warning
    str_free(cmd);
    exit(EXIT_SUCCESS);
}

static void edit_settings_action(String command, Action* action)
{
    save_settings(setting_file, &settings);
    run_editor(setting_file);
}

static void edit_commands_action(String command, Action* action)
{
    if (!is_readable_file(commands_file)) {
        FILE* f = fopen(commands_file, "w");
        if (!f)
            return;
        fputs("#example: run a command in xterm\n#'run top' will start top in xterm\n"\
              "#[Run in Terminal]\n#Exec=xterm -e #args are put here\n#Icon=terminal\n", f);
        fclose(f);
    }
    run_editor(commands_file);
}

//------------------------------------------
// settings etc...

void parse_commandline(int argc, char** argv, Settings* set)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--one-way")) {
            set->one_time = true;
        } else if (!strcmp(argv[i], "--help")) {
            printf("fehlstart 0.4.0 (c) 2013 maep\noptions:\n"
                   "\t--one-way\texit after one use\n");
            exit(EXIT_SUCCESS);
        } else {
            printf("invalid option: %s\n", argv[i]);
        }
    }
}

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

    // TODO: get rid of this
    #define CONTAINS(a, b) str_contains_i(str_wrap(a), STR_S(b))
    const char* desktop = "Old";
    if (CONTAINS(session, "kde") || kde0 != NULL || kde1 != NULL)
        desktop = "KDE";
    else if (CONTAINS(current_desktop, "unity"))
	desktop = "Unity";
    else if (CONTAINS(session, "gnome") || gnome != NULL)
        desktop = "GNOME";
    else if (CONTAINS(session, "xfce") || CONTAINS(xdg_prefix, "xfce"))
        desktop = "XFCE";
    else if (CONTAINS(session, "lxde") || CONTAINS(current_desktop, "lxde"))
        desktop = "LXDE";
    else if (CONTAINS(session, "rox")) // verify
        desktop = "ROX";
    #undef CONTAINS
    // TODO: add MATE, Razor, TDE, Unity
    printf("detected desktop: %s\n", desktop);
    return desktop;
}

//------------------------------------------
// main

int main(int argc, char** argv)
{
    gtk_init(&argc, &argv);
    parse_commandline(argc, argv, &settings);

    signal(SIGCHLD, SIG_IGN); // let kernel raep the children, mwhahaha
    g_chdir(get_home_dir());
    g_desktop_app_info_set_desktop_env(get_desktop_env());
    user_app_dir = g_build_filename(get_home_dir(), USER_APPLICATIONS_DIR, NULL);
    action_map = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free_action);
    filter_list = g_array_sized_new (false, true, sizeof(Action*), 250);

    add_action("quit fehlstart", "exit", GTK_STOCK_QUIT, quit_action);
    add_action("fehlstart settings", "config preferences", GTK_STOCK_PREFERENCES, edit_settings_action);
    add_action("fehlstart actions", "commands", GTK_STOCK_EXECUTE, edit_commands_action);

    // init config files
    gchar* dir = g_build_filename(g_get_user_config_dir(), "fehlstart", NULL);
    g_mkdir_with_parents(dir, 0700);
    setting_file = g_build_filename(dir, "fehlstart.rc", NULL);
    mnemonic_file = g_build_filename(dir, "actions.rc", NULL);
    commands_file = g_build_filename(dir, "commands.rc", NULL);
    g_free(dir);

    update_all(NULL); // read config and launchers
    load_mnemonics(mnemonic_file, action_map);
    create_widgets();
    if (settings.one_time) // one-time use
        show_window();
    else
        register_hotkey(settings.Bindings_launch);

    gtk_main();

    save_settings(setting_file, &settings);
    save_mnemonics(mnemonic_file, action_map);
    return EXIT_SUCCESS;
}

