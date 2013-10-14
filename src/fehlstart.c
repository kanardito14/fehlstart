/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>

#include <keybinder.h>

#include "strang.h"
#include "graphics.h"
#include "action.h"
#include "util.h"

// types
typedef char* string;
typedef bool boolean;
typedef int integer;

// settings struct
typedef struct Settings {
    #define X(type, group, key, value) type group##_##key;
    #include "settings.def"
    #undef X
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
#define APPLICATIONS_DIR_0      "/usr/share/applications"
#define APPLICATIONS_DIR_1      "/usr/local/share/applications"
#define USER_APPLICATIONS_DIR   ".local/share/applications"

// preferences
#define X(type, group, name, value) type group##_##name = value;;
PREFERENCES_LIST
#undef X
static bool             one_time = false;

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
static char*            config_file;
static char*            action_file;
static char*            commands_file;
static char*            user_app_dir;

//------------------------------------------
// helper functions

static String get_first_input_word(void)
{
    for (uint32_t i = 0; i < input_string_size; i++)
        if (input_string[i] == ' ')
            return str_wrap_n(input_string, i);
    return str_wrap_n(input_string, input_string_size);
}

//------------------------------------------
// action functions


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
            a->score = 100 + filter.len - pos;
    }
    
    if (a->score < 0) {
        unsigned pos = str_find_first_i(a->exec, filter);
        if (pos != STR_END)
            a->score = 1 + filter.len - pos;
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

static void load_icon(const char* name)
{
    const int sizes[] = {256, 128, 48, 32};
    if (icon_pixbuf) {
        g_object_unref(icon_pixbuf);
        icon_pixbuf = NULL;
    }
    
    if (!Icons_show)
        return;
       
    // if the size is uncommon, svgs might be used which load 
    // too slow on my atom machine
    int h = Window_height / 2;
    if (!Icons_scale) {
        int i = 0;
        for (; i < (int)COUNTOF(sizes) && h < sizes[i]; i++) {}
        h = sizes[i];
    }
    if (g_path_is_absolute(name)) {
        icon_pixbuf = gdk_pixbuf_new_from_file_at_scale(name, -1, h, true, NULL);
    } else {
        int flags = GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_USE_BUILTIN;
        GtkIconTheme* theme = gtk_icon_theme_get_default();
        icon_pixbuf = gtk_icon_theme_load_icon(theme, name, h, flags, NULL);
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

    load_icon(icon_name);
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

    if (one_time) // configured for one-time use
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
    gtk_widget_set_size_request(window, Window_width, Window_height);
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
    GtkStyle* st = gtk_widget_get_style(window);
    clear(cr);
    draw_window(cr, st);
    draw_icon(cr, icon_pixbuf);
    draw_dots(cr, st, selection, filter_list->len);
    draw_labels(cr, st, action_name, input_string);
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

void read_config(const char* config_file)
{
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_file, G_KEY_FILE_NONE, NULL)) {
        #define X(type, group, key, value) if (g_key_file_has_key(kf, #group, #key, NULL))\
            settings.group##_##key = g_key_file_get_##type(kf, #group, #key, NULL);
        #include "settings.def"
        #undef X
    }
    g_key_file_free(kf);
    // some sanity checks
    settings.Border_width = iclamp(Border_width, 0, 20);
    settings.Window_width = iclamp(Window_width, 200, 800);
    settings.Window_height = iclamp(Window_height, 100, 800);
    settings.Labels_size1 = iclamp(Labels_size1, 6, 32);
    settings.Labels_size2 = iclamp(Labels_size2, 6, 32);
}

void save_config(const char* config_file)
{
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, config_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
    #define X(type, group, key, value) g_key_file_set_##type (kf, #group, #key, group##_##key);
    #include "settings.def"
    #undef X
    key_file_save(kf, config_file);
    g_key_file_free(kf);
}

static void* update_all(void* user_data)
{
    if (file_changed())
        read_config();
    if (file_changed())
        update_commands();
    g_hash_table_foreach(action_map, update_launcher, NULL);
    add_launchers(STR_S(APPLICATIONS_DIR_0));
    add_launchers(STR_S(APPLICATIONS_DIR_1));
    add_launchers(STR_S(USER_APPLICATIONS_DIR));
    return NULL;
}

static void register_hotkey(void)
{
    if (one_time)
        return;
    keybinder_init();
    if (keybinder_bind(Bindings_launch, toggle_window, NULL)) {
        gtk_accelerator_parse(Bindings_launch, &hotkey_key, &hotkey_mod);
        printf("hit %s to show window\n", Bindings_launch);
    } else {
        GtkWidget* dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_NONE, "Hotkey '%s' is already being used!", Bindings_launch);
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Edit Settings", GTK_RESPONSE_ACCEPT,
            "Quit", GTK_RESPONSE_REJECT, NULL);
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
              "#[Run in Terminal]\n#Exec=xterm -e #args are put here\n#Icon=terminal\n", f);
        fclose(f);
    }
    run_editor(commands_file);
}

//------------------------------------------
// main

static void parse_commandline(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--one-way")) {
            one_time = true;
        } else if (!strcmp(argv[i], "--help")) {
            printf("fehlstart 0.4.0 (c) 2013 maep\noptions:\n"
                   "\t--one-way\texit after one use\n");
            exit(EXIT_SUCCESS);
        } else {
            printf("invalid option: %s\n", argv[i]);
        }
    }
}

static void exit_handler(void)
{
    save_config();
    save_mnemonics();
}

int main(int argc, char** argv)
{
    gtk_init(&argc, &argv);
    parse_commandline(argc, argv);

    signal(SIGCHLD, SIG_IGN); // let kernel raep the children, mwhahaha
    g_chdir(get_home_dir());
    g_desktop_app_info_set_desktop_env(get_desktop_env());
    user_app_dir = g_build_filename(get_home_dir(), USER_APPLICATIONS_DIR, NULL);
    action_map = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free_action);
    filter_list = g_array_sized_new (false, true, sizeof(Action*), 250);

    add_action("quit fehlstart", "exit", GTK_STOCK_QUIT, quit_action);
    add_action("fehlstart settings", "config preferences", GTK_STOCK_PREFERENCES, edit_settings_action);
    add_action("fehlstart actions", "commands", GTK_STOCK_EXECUTE, edit_commands_action);

    // create user settings file if needed
    gchar* dir = g_build_filename(g_get_user_config_dir(), "fehlstart", NULL);
    g_mkdir_with_parents(dir, 0700);
    config_file = g_build_filename(dir, "fehlstart.rc", NULL);
    action_file = g_build_filename(dir, "actions.rc", NULL);
    commands_file = g_build_filename(dir, "commands.rc", NULL);
    g_free(dir);

    update_all(NULL);       // read config and launchers
    load_mnemonics();       // load "learned" user behaviour
    create_widgets();       // create ui
    atexit(exit_handler);   // set up exit handler
    
    if (one_time)           // one-time use
        show_window();
    else
        register_hotkey();

    gtk_main();             // start main loop
    return EXIT_SUCCESS;
}
