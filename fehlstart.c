/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2011 maep and contributors
*/

#include <string.h>
#include <stdlib.h>

#include <dirent.h>
#include <signal.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>

#include <keybinder.h>

#include "string.h"

// macros
#define WELCOME_MESSAGE     "type something"
#define NO_MATCH_MESSAGE    "no match"

#define DEFAULT_HOTKEY      "<Super>space"
#define WINDOW_WIDTH        200
#define WINDOW_HEIGHT       100

#define ICON_SIZE           GTK_ICON_SIZE_DIALOG
#define DEFAULT_ICON        GTK_STOCK_FIND
#define NO_MATCH_ICON       GTK_STOCK_DIALOG_QUESTION
#define APPLICATION_ICON    "applications-other"

#define APPLICATIONS_DIR                "/usr/share/applications"
#define USER_APPLICATIONS_DIR           ".local/share/applications"

#define INITIAL_LAUNCH_LIST_CAPACITY    0xff
#define INPUT_STRING_SIZE               0x80
#define SHOW_IGNORE_TIME                100000

typedef struct
{
    String  file;
    String  name;
    String  executable;
    String  icon;
} Launch;

#define LAUNCH_INITIALIZER {STR_S(""), STR_S(""), STR_S(""), STR_S("")}

typedef struct Action_
{
    String  name;
    String  hidden_key;
    String  short_key;
    String  icon;
    int     score;
    void*   data;
    void    (*action) (String, struct Action_*);
} Action;

#define ACTION_INITIALIZER {STR_S(""), STR_S(""), STR_S(""), STR_S(""), 0, NULL, NULL}

//------------------------------------------
// forward declarations

static void launch_action(String, Action*);
static void settings_action(String, Action*);
static void commands_action(String, Action*);
static void update_action(String, Action*);
static void quit_action(String, Action*);

//------------------------------------------
// build-in actions

#define _ACTION(name, hint, icon, action) {STR_I(name), STR_I(hint), STR_I(""), STR_I(icon), 0, 0 , action}
#define NUM_ACTIONS 4
static Action actions[NUM_ACTIONS] = {
    _ACTION("update fehlstart", "reload", GTK_STOCK_REFRESH, update_action),
    _ACTION("quit fehlstart", "exit", GTK_STOCK_QUIT, quit_action),
    _ACTION("edit settings", "config preferences", GTK_STOCK_PREFERENCES, settings_action),
    _ACTION("edit commands", "", GTK_STOCK_EXECUTE, commands_action)
};
#undef _ACTION

//------------------------------------------
// global variables

// preferences
static struct
{
    gchar *hotkey;
    guint64 update_timeout;
    bool strict_matching;
    bool match_executable;
    bool show_icon;
    bool one_time;
} prefs = {
    DEFAULT_HOTKEY,
    15,
    false,
    true,
    true,
    false
};

// launcher stuff
static Launch* launch_list = NULL;
static uint32_t launch_list_capacity = 0;
static uint32_t launch_list_size = 0;

static Action* action_list = NULL;
static uint32_t action_list_capacity = 0;
static uint32_t action_list_size = 0;

static Action** filter_list = NULL;
static uint32_t filter_list_capacity = 0;
static uint32_t filter_list_size = 0;
static uint32_t filter_list_choice = 0;

static char input_string[INPUT_STRING_SIZE];
static uint32_t input_string_size = 0;

static GStaticMutex lists_mutex = G_STATIC_MUTEX_INIT;

// gtk widgets
static GtkWidget* window = NULL;
static GtkWidget* image = NULL;
static GtkWidget* action_label = NULL;
static GtkWidget* input_label = NULL;

static char* config_file = 0;
static char* action_file = 0;
static char* commands_file = 0;
static char* user_app_dir = 0;

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
    for (uint32_t i = 0;i < input_string_size; i++)
        if (input_string[i] == ' ')
            return str_wrap_n(input_string, i);
    return str_wrap_n(input_string, input_string_size);
}

//------------------------------------------
// launcher function

static void add_launcher(Launch launch)
{
    if (launch_list_size + 1 > launch_list_capacity)
    {
        launch_list_capacity += INITIAL_LAUNCH_LIST_CAPACITY;
        launch_list = realloc(launch_list, launch_list_capacity * sizeof(Launch));
    }
    launch_list[launch_list_size] = launch;
    launch_list_size++;
}

static bool load_launcher(String file_name, Launch* launcher)
{
    GKeyFile* file = g_key_file_new();
    g_key_file_load_from_file(file, file_name.str, G_KEY_FILE_NONE, 0);

    GDesktopAppInfo* info = 0;
    info = g_desktop_app_info_new_from_keyfile(file);

    bool used = (info != 0)
        && !g_desktop_app_info_get_is_hidden(info)
        && g_app_info_should_show(G_APP_INFO(info));

    if (used)
    {
        const char* str = 0;
        launcher->file = file_name;
        // get name
        str = g_app_info_get_name(G_APP_INFO(info));
        launcher->name = str_new(str);
        // get executable
        if (prefs.match_executable)
        {
            str = g_app_info_get_executable(G_APP_INFO(info));
            launcher->executable = str_new(str);
        }
        // get icon
        str = g_key_file_get_value(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, 0);
        String icon = (str) ? str_own(str) : STR_S(APPLICATION_ICON);
        // if icon is not a full path but ends with file extension...
        // the skype package does this, and icon lookup works only without the .png
        if (!g_path_is_absolute(icon.str)
            && (str_ends_with_i(icon, STR_S(".png"))
            || str_ends_with_i(icon, STR_S(".svg"))
            || str_ends_with_i(icon, STR_S(".xpm"))))
            icon.len -= 4;
        launcher->icon = icon;
    }

    g_object_unref(G_OBJECT(info));
    g_key_file_free(file);
    return used;
}

static void free_launcher(Launch* launcher)
{
    str_free(launcher->file);
    str_free(launcher->name);
    str_free(launcher->executable);
    str_free(launcher->icon);
}

static void clear_launch_list(void)
{
    for (uint32_t i = 0; i < launch_list_size; i++)
        free_launcher(launch_list + i);
    launch_list_size = 0;
}

static void add_launchers_from_dir(String dir_name)
{
    DIR* dir = opendir(dir_name.str);
    if (dir == 0)
        return;
    printf("reading %s\n", dir_name.str);

    struct dirent* ent = 0;
    while ((ent = readdir(dir)) != 0)
    {
        String file_name = str_wrap(ent->d_name);
        if (str_ends_with_i(file_name, STR_S(".desktop")))
        {
            String full_path = str_assemble_path(dir_name, file_name);
            Launch launcher = LAUNCH_INITIALIZER;
            if (load_launcher(full_path, &launcher))
                add_launcher(launcher);
            else
                str_free(full_path);
        }
    }
    closedir(dir);
}

static void add_launchers_from_commands(void)
{
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, commands_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
    gchar** groups = g_key_file_get_groups(kf, NULL);
    for (size_t i = 0; groups[i]; i++)
    {
        Launch launch = {STR_I("!command"), // mark as command
            str_new(groups[i]),
            str_own(g_key_file_get_string(kf, groups[i], "Exec", NULL)),
            str_own(g_key_file_get_string(kf, groups[i], "Icon", NULL))
        };
        add_launcher(launch);
    }
    g_strfreev(groups);
    g_key_file_free(kf);
}

static void update_launch_list(void)
{
    clear_launch_list();
    add_launchers_from_dir(STR_S(APPLICATIONS_DIR));
    add_launchers_from_dir(str_wrap(user_app_dir));
    add_launchers_from_commands();
}

//------------------------------------------
// action functions

static void add_action(Action* action)
{
    if (action_list_size + 1 > action_list_capacity)
    {
        action_list_capacity += INITIAL_LAUNCH_LIST_CAPACITY;
        action_list = realloc(action_list, action_list_capacity * sizeof(Action));
    }
    action_list[action_list_size] = *action;
    action_list_size++;
}

static void clear_action_list(void)
{
    for (uint32_t i = 0; i < action_list_size; i++)
        str_free(action_list[i].short_key);
    action_list_size = 0;
}

inline static int cmp_action_name(const void* a, const void* b)
{
    return str_compare_i(((Action*)a)->name, ((Action*)b)->name);
}

static void update_action_list(void)
{
    clear_action_list();
    for (size_t i = 0; i < launch_list_size; i++)
    {
        Launch* l = launch_list + i;
        String keyword = str_duplicate(l->name);
        str_to_lower(keyword);
        Action action = {
            l->name,        // name
            l->executable,  // hidden_key
            STR_S(""),      // short_key
            l->icon,        // icon
            0,              // score
            l,              // data
            launch_action}; // action
        add_action(&action);
    }
    for (size_t i = 0; i < NUM_ACTIONS; i++)
        add_action(actions + i);
    qsort(action_list, action_list_size, sizeof(Action), cmp_action_name);
}

// calculates a score that determines in which order the results are displayed
static void update_action_score(Action* action, String filter)
{
    int score = -1;
    if (str_starts_with(action->short_key, filter))
        score = 10000 + (INPUT_STRING_SIZE - action->short_key.len);

    if (score < 0)
    {
        uint32_t pos = str_find_first_i(action->name, filter);
        if (pos != STR_END)
            score = 100 + (filter.len - pos);
    }

    if (score < 0)
    {
        uint32_t pos = str_find_first_i(action->hidden_key, filter);
        if (pos != STR_END)
            score = 1 + (filter.len - pos);
    }
    action->score = score;
}

inline static int cmp_action_score(const void* a, const void* b)
{
    return (*((Action**)b))->score - (*((Action**)a))->score;
}

static void filter_action_list(String filter)
{
    if (filter.len == 0)
        return;
    g_static_mutex_lock(&lists_mutex);

    if (filter_list_capacity < action_list_capacity)
    {
        filter_list_capacity = action_list_capacity;
        filter_list = realloc(filter_list, filter_list_capacity * sizeof(Action*));
    }

    filter_list_size = 0;
    for (size_t i = 0; i < action_list_size; i++)
    {
        update_action_score(action_list + i, filter);
        if (action_list[i].score > 0)
            filter_list[filter_list_size++] = action_list + i;
    }

    qsort(filter_list, filter_list_size, sizeof(Action*), cmp_action_score);
    g_static_mutex_unlock(&lists_mutex);
}

static void run_selected(void)
{
    g_static_mutex_lock(&lists_mutex);
                                // check in case async list update fails
    if (filter_list_size > 0 && filter_list_choice < action_list_size)
    {
        Action* action = filter_list[filter_list_choice];
        if (action->action != 0)
        {
            str_free(action->short_key);
            action->short_key = str_duplicate(get_first_input_word());
            String str = str_wrap_n(input_string, input_string_size);
            action->action(str, action);
        }
    }
    g_static_mutex_unlock(&lists_mutex);
}

//------------------------------------------
// gui functions

static void image_set_from_name(GtkImage* img, const char* name, GtkIconSize size)
{
    if (!prefs.show_icon)
        return;

    GIcon* icon = 0;
    if (g_path_is_absolute(name))
    {
          GFile* file;
          file = g_file_new_for_path(name);
          icon = g_file_icon_new(file);
          g_object_unref(file);
    }
    else
        icon = g_themed_icon_new_with_default_fallbacks(name);

    gtk_image_set_from_gicon(img, icon, size);
    g_object_unref(icon);
}

static void show_selected(void)
{
    const char* action_text = NO_MATCH_MESSAGE;
    String icon_name = STR_S(NO_MATCH_ICON);

    g_static_mutex_lock(&lists_mutex);
    if (input_string_size == 0)
    {
        action_text = WELCOME_MESSAGE;
        icon_name = STR_S(DEFAULT_ICON);
    }                             // check in case async list update fails
    else if (filter_list_size > 0 && filter_list_size <= action_list_size)
    {
        action_text = filter_list[filter_list_choice]->name.str;
        icon_name = filter_list[filter_list_choice]->icon;
    }
    g_static_mutex_unlock(&lists_mutex);

    gtk_label_set_text(GTK_LABEL(input_label), input_string);
    gtk_label_set_text(GTK_LABEL(action_label), action_text);
    gtk_widget_queue_draw(input_label);
    gtk_widget_queue_draw(action_label);

    image_set_from_name(GTK_IMAGE(image), icon_name.str, ICON_SIZE);
    gtk_widget_queue_draw(image);
}

static void handle_text_input(GdkEventKey* event)
{
    if (event->keyval == GDK_KEY_BackSpace && input_string_size > 0)
        input_string_size--;
    else if (event->length == 1
        && (input_string_size + 1) < INPUT_STRING_SIZE
        && (input_string_size > 0 || event->keyval != GDK_KEY_space))
        input_string[input_string_size++] = event->keyval;

    input_string[input_string_size] = 0;
    filter_action_list(get_first_input_word());
    filter_list_choice = 0;
}

static void hide_window(void)
{
    if (!gtk_widget_get_visible(window))
        return;

    gdk_keyboard_ungrab(GDK_CURRENT_TIME);

    if (prefs.one_time) // configured for one-time use
        gtk_main_quit();

    gtk_widget_hide(window);
    input_string[0] = 0;
    input_string_size = 0;
    filter_list_size = 0;
    filter_list_choice = 0;
}

static void show_window(void)
{
    if (gtk_widget_get_visible(window))
        return;

    show_selected();
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_present(GTK_WINDOW(window));
    gtk_window_set_keep_above(GTK_WINDOW(window), true);
    gdk_keyboard_grab(window->window, true, GDK_CURRENT_TIME);
}

static void toggle_window(const char *keystring, void *data)
{
    if (gtk_widget_get_visible(window))
        hide_window();
    else
        show_window();
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
    switch (event->keyval)
    {
        case GDK_KEY_Escape:
            hide_window();
        break;
        case GDK_KEY_Return:
            run_selected();
            hide_window();
        break;
        case GDK_KEY_Up:
            filter_list_choice += (filter_list_size - 1);
            filter_list_choice %= filter_list_size;
            show_selected();
        break;
        case GDK_KEY_Tab:
        case GDK_KEY_Down:
            filter_list_choice++;
            filter_list_choice %= filter_list_size;
            show_selected();
        break;
        default:
            handle_text_input(event);
            show_selected();
        break;
    }
    return true;
}

//------------------------------------------
// config files

static void key_file_save(GKeyFile* kf, const char* file_name)
{
    FILE* f = fopen(file_name, "w");
    if (!f)
        return;
    gsize length = 0;
    gchar* data = g_key_file_to_data(kf, &length, NULL);
    fwrite(data, 1, length, f);
    g_free(data);
    fclose(f);
}

// macro for writing to keyfile
#define WRITE_PREF(type, group, key, var) \
    g_key_file_set_##type (kf, group, key, prefs.var)

static void save_config(void)
{
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, config_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
    WRITE_PREF(string, "Bindings", "launch", hotkey);
    WRITE_PREF(boolean, "Matching", "strict", strict_matching);
    WRITE_PREF(boolean, "Matching", "executable", match_executable);
    WRITE_PREF(uint64, "Update", "interval", update_timeout);
    WRITE_PREF(boolean, "Icons", "show", show_icon);
    key_file_save(kf, config_file);
    g_key_file_free(kf);
}

// macro for reading from keyfile, without overwriting default values
#define READ_PREF(type, group, key, var)            \
    if (g_key_file_has_key(kf, group, key, NULL))   \
        prefs.var = g_key_file_get_##type (kf, group, key, NULL)

static void read_config(void)
{
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_file, G_KEY_FILE_NONE, NULL))
    {
        READ_PREF(string, "Bindings", "launch", hotkey);
        READ_PREF(boolean, "Matching", "strict", strict_matching);
        READ_PREF(boolean, "Matching", "executable", match_executable);
        READ_PREF(uint64, "Update", "interval", update_timeout);
        READ_PREF(boolean, "Icons", "show", show_icon);
    }
    g_key_file_free(kf);
}

static void save_actions(void)
{
    GKeyFile* kf = g_key_file_new();
    for (size_t i = 0; i < action_list_size; i++)
    {
        Action* a = action_list + i;
        if (a->short_key.len > 0)
            g_key_file_set_string(kf, a->name.str, "short_key", a->short_key.str);
    }
    key_file_save(kf, action_file);
    g_key_file_free(kf);
}

void load_actions(void)
{
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, action_file, G_KEY_FILE_NONE, NULL))
        for (size_t i = 0; i < action_list_size; i++)
        {
            Action* a = action_list + i;
            if (g_key_file_has_group(kf, a->name.str))
            {
                gchar* v = g_key_file_get_string(kf, a->name.str, "short_key", NULL);
                a->short_key = str_wrap(v);;
                a->short_key.can_free = true;
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

// does what it says, lists_mutex must be locked before calling
static void reload_settings_and_actions(void)
{
    save_actions();
    read_config();
    update_launch_list();
    update_action_list();
    load_actions();
}

// opens file in an editor and returns immediately
// the plan was that run_editor only returns after the editor exits.
// that way I could reload the settings after changes have been made.
// but xdg-open and friends return imediately so that plan is spoiled :(
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
    if (!prefs.one_time)
    {
        keybinder_init();
        keybinder_bind(prefs.hotkey, toggle_window, NULL);
        printf("hit %s to show window\n", prefs.hotkey);
    }
}

// gets run periodically
static gboolean update_lists_cb(void *data)
{
    if (g_static_mutex_trylock(&lists_mutex))
    {
        reload_settings_and_actions();
        g_static_mutex_unlock(&lists_mutex);
    }
    return true;
}

static void run_updates(void)
{
    if (prefs.update_timeout && !prefs.one_time)
        g_timeout_add_seconds(60 * prefs.update_timeout, update_lists_cb, NULL);
}

static void create_widgets(void)
{
    window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_size_request(window, WINDOW_WIDTH, WINDOW_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(window), false);
    gtk_window_set_accept_focus(GTK_WINDOW(window), true);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_event), 0);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy), 0);
    g_signal_connect(window, "key-press-event", G_CALLBACK(key_press_event), 0);

    GtkWidget* vbox = gtk_vbox_new(false, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_widget_show(vbox);

    image = gtk_image_new();
    image_set_from_name(GTK_IMAGE(image), DEFAULT_ICON, ICON_SIZE);
    gtk_box_pack_start(GTK_BOX(vbox), image, true, false, 0);
    gtk_widget_show(image);

    action_label = gtk_label_new(WELCOME_MESSAGE);
    gtk_box_pack_start(GTK_BOX(vbox), action_label, true, true, 0);
    gtk_widget_show(action_label);

    input_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), input_label, true, false, 0);
    gtk_widget_show(input_label);
}

static const char* get_desktop_env(void)
{
    // replacing strcasestr, but it's a gnu extension, b must be a static cstring
    #define _CONTAINS(a, b) str_contains_i(str_wrap(a), STR_S(b))
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
    if (_CONTAINS(session, "kde") || kde0 != NULL || kde1 != NULL)
        desktop = "KDE";
    else if (_CONTAINS(session, "gnome") || gnome != NULL)
        desktop = "GNOME";
    else if (_CONTAINS(session, "xfce") || _CONTAINS(xdg_prefix, "xfce"))
        desktop = "XFCE";
    else if (_CONTAINS(session, "lxde") || _CONTAINS(current_desktop, "lxde"))
        desktop = "LXDE";
    else if (_CONTAINS(session, "rox"))
        desktop = "ROX";

    printf("detected desktop: %s\n", desktop);
    return desktop;
    #undef _CONTAINS
}

static void parse_commandline(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--one-way"))
            prefs.one_time = true;
        else if (!strcmp(argv[i], "--help"))
        {
            printf("fehlstart 0.2.5 (c) 2011 maep\noptions:\n"\
                "\t--one-way\texit after one use\n");
            exit(EXIT_SUCCESS);
        }
        else
            printf("invalid option: %s\n", argv[i]);
    }
}

static void run_launcher(String command, Launch* launch)
{
    GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(launch->file.str);
    if (info != 0)
    {
        g_app_info_launch(G_APP_INFO(info), NULL, NULL, NULL);
        g_object_unref(G_OBJECT(info));
    }
}

static void run_command(String command, Launch* launch)
{
    // extract args from command
    uint32_t sp = str_find_first(command, STR_S(" "));
    String cmd = (sp == STR_END) ?
        str_duplicate(launch->executable) :
        str_concat(launch->executable, str_substring(command, sp, STR_END));
    printf("%s\n", cmd.str);
    if (system(cmd.str)) {}; // to shut up gcc
    str_free(cmd);
}

//------------------------------------------
// actions

static void quit_action(String command, Action* action)
{
    gtk_main_quit();
}

static void update_action(String command, Action* action)
{
    // lists_mutex is locked when actions are called
    reload_settings_and_actions();
}

static void launch_action(String command, Action* action)
{
    pid_t pid = fork();
    if (pid != 0)
        return;

    setsid(); // "detatch" from parent process
    signal(SIGCHLD, SIG_DFL); // go back to default child behaviour
    Launch* launch = action->data;
    if (str_equals(launch->file, STR_S("!command")))
        run_command(command, launch);
    else
        run_launcher(command, launch);
    exit(EXIT_SUCCESS);
}

static void settings_action(String command, Action* action)
{
    save_config();
    run_editor(config_file);
}

static void commands_action(String command, Action* action)
{
    if (!is_readable_file(commands_file))
    {
        FILE* f = fopen(commands_file, "w");
        if (!f)
            return;
        fputs("#example: 'run top' would louch top in xterm\n"\
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
    g_thread_init(NULL);

    signal(SIGCHLD, SIG_IGN); // let kernel raep the children, mwhahaha
    g_chdir(get_home_dir());
    g_desktop_app_info_set_desktop_env(get_desktop_env());
    user_app_dir = g_build_filename(get_home_dir(), USER_APPLICATIONS_DIR, NULL);

    init_config_files();
    read_config();
    update_launch_list();
    update_action_list();
    load_actions();

    create_widgets();
    if (prefs.one_time) // one-time use
        show_window();

    register_hotkey();
    run_updates();

    gtk_main();

    return EXIT_SUCCESS;
}
