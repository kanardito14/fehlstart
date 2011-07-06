/*
*   fehlstart - a small launcher written in gnu c99
*   this source is publieshed under the GPLv3 license.
*   copyright 2011 maep
*/

#define _GNU_SOURCE // for strcasestr function

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <malloc.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

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
#define DEFAULT_ICON        GTK_STOCK_FIND
#define NO_MATCH_ICON       GTK_STOCK_DIALOG_QUESTION
#define ICON_SIZE           GTK_ICON_SIZE_DIALOG

#define APPLICATIONS_DIR                "/usr/share/applications"
#define USER_APPLICATIONS_DIR           ".local/share/applications"

#define INITIAL_LAUNCH_LIST_CAPACITY    0xff
#define INPUT_STRING_SIZE               0x80
#define SHOW_IGNORE_TIME                100000

typedef struct
{
    String file;
    String name;
    String executable;
    String icon;
} Launch;

typedef struct Action_
{
    String name;
    String hidden_key;
    String short_key;
    String icon;
    int score;
    void*  data;
    void (*action) (String, struct Action_*);
} Action;

//------------------------------------------
// forward declarations

void update_action(String, Action*);
void quit_action(String, Action*);
void launch_action(String, Action*);

//------------------------------------------
// build-in actions

#define NUM_ACTIONS 2
static Action actions[NUM_ACTIONS] = {
    {STR_I("update fehlstart"), STR_I(""), STR_I(""), STR_I(GTK_STOCK_REFRESH), 0, 0 , update_action},
    {STR_I("quit fehlstart"), STR_I(""), STR_I(""), STR_I(GTK_STOCK_QUIT), 0, 0, quit_action}
};

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

// workaround for the focus_out_event bug
static struct timeval last_hide = {0, 0};

// gtk widgets
static GtkWidget* window = NULL;
static GtkWidget* image = NULL;
static GtkWidget* action_label = NULL;
static GtkWidget* input_label = NULL;

char* config_file = 0;
char* action_file = 0;

//------------------------------------------
// helper functions

bool is_file(const char* path)
{
    struct stat s;
    return stat(path, &s) == 0 ? S_ISREG(s.st_mode) : false;
}

bool is_directory(const char* path)
{
    struct stat s;
    return stat(path, &s) == 0 ? S_ISDIR(s.st_mode) : false;
}

void touch_file(const char* file_name)
{
    FILE* f = fopen(file_name, "a");
    if (f)
        fclose(f);
}

const char* get_home_dir(void)
{
     const char* home = getenv("HOME");
     if (!home)
        home = g_get_home_dir();
    return home;
}

String get_first_input_word(void)
{
    for (uint32_t i = 0;i < input_string_size; i++)
        if (input_string[i] == ' ')
            return str_wrap_n(input_string, i);
    return str_wrap_n(input_string, input_string_size);
}

//------------------------------------------
// launcher function

void add_launcher(Launch launch)
{
    if (launch_list_size + 1 > launch_list_capacity)
    {
        launch_list_capacity += INITIAL_LAUNCH_LIST_CAPACITY;
        launch_list = realloc(launch_list, launch_list_capacity * sizeof(Launch));
    }
    launch_list[launch_list_size] = launch;
    launch_list_size++;
}

bool load_launcher(String file_name, Launch* launcher)
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
        str = g_app_info_get_executable(G_APP_INFO(info));
        launcher->executable = str_new(str);
        // get icon
        str = g_key_file_get_value(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, 0);
        String icon = STR_S("applications-other");
        if (str)
        {
            icon = str_wrap(str);
            // if icon is not a full path but ends with file extension...
            // the skype package does this, and icon lookup works only without the .png
            if (!g_path_is_absolute(icon.str)
                && (str_ends_with_i(icon, STR_S(".png"))
                || str_ends_with_i(icon, STR_S(".svg"))
                || str_ends_with_i(icon, STR_S(".xpm"))))
                icon.len -= 4;
        }
        launcher->icon = str_duplicate(icon);
        g_free((gpointer)str);
    }

    g_object_unref(G_OBJECT(info));
    g_key_file_free(file);
    return used;
}

void free_launcher(Launch* launcher)
{
    str_free(launcher->file);
    str_free(launcher->name);
    str_free(launcher->executable);
    str_free(launcher->icon);
}

void populate_launch_list(String dir_name)
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
            String full_path = assemble_path(dir_name, file_name);
            Launch launcher;
            if (load_launcher(full_path, &launcher))
                add_launcher(launcher);
            else
                str_free(full_path);
        }
    }
    closedir(dir);
}

void clear_launch_list(void)
{
    for (uint32_t i = 0; i < launch_list_size; i++)
        free_launcher(launch_list + i);
    launch_list_size = 0;
}

void update_launch_list(void)
{
    clear_launch_list();
    populate_launch_list(STR_S(APPLICATIONS_DIR));
    const char* home = get_home_dir();
    if (home != 0)
    {
        String home_dir = str_wrap(home);
        String user_dir = STR_S(USER_APPLICATIONS_DIR);
        String full_path = assemble_path(home_dir, user_dir);
        populate_launch_list(full_path);
        str_free(full_path);
    }
}

//------------------------------------------
// action functions

void add_action(Action* action)
{
    if (action_list_size + 1 > action_list_capacity)
    {
        action_list_capacity += INITIAL_LAUNCH_LIST_CAPACITY;
        action_list = realloc(action_list, action_list_capacity * sizeof(Action));
    }
    action_list[action_list_size] = *action;
    action_list_size++;
}

void clear_action_list(void)
{
    for (uint32_t i = 0; i < action_list_size; i++)
        str_free(action_list[i].short_key);
    action_list_size = 0;
}

int cmp_action_name(const void* a, const void* b)
{
    return str_compare_i(((Action*)a)->name, ((Action*)b)->name);
}

void update_action_list(void)
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
void update_action_score(Action* action, String filter)
{
    int score = -1;
    if (str_starts_with(action->short_key, filter))
        score = 10000 + (INPUT_STRING_SIZE - filter.len);

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

int cmp_action_score(const void* a, const void* b)
{
    return (*((Action**)b))->score - (*((Action**)a))->score;
}

void filter_action_list(String filter)
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

void run_selected(void)
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

void image_set_from_name(GtkImage* img, const char* name, GtkIconSize size)
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

void show_selected(void)
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

void handle_text_input(GdkEventKey* event)
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

void hide_window(void)
{
    if (!gtk_widget_get_visible(window))
        return;

    gdk_keyboard_ungrab(GDK_CURRENT_TIME);

    if (prefs.one_time) // configured for one-time use
        gtk_main_quit();
    else
    {
        gtk_widget_hide(window);
        input_string[0] = 0;
        input_string_size = 0;
        filter_list_size = 0;
        filter_list_choice = 0;
        gettimeofday(&last_hide, 0);
    }
}

void show_window(void)
{
    // cope with x stealing focus
    struct timeval now, elapsed;
    gettimeofday(&now, 0);
    timersub(&now, &last_hide, &elapsed);
    bool x11_sucks = (elapsed.tv_sec == 0 && elapsed.tv_usec < SHOW_IGNORE_TIME);

    if (!x11_sucks && !gtk_widget_get_visible(window))
    {
        show_selected();
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_present(GTK_WINDOW(window));
        gtk_window_set_keep_above(GTK_WINDOW(window), true);
        gdk_keyboard_grab(window->window, true, GDK_CURRENT_TIME);
    }
}

void toggle_window(const char *keystring, void *data)
{
    if (gtk_widget_get_visible(window))
        hide_window();
    else
        show_window();
}

void destroy(GtkWidget* widget, gpointer data)
{
    gtk_main_quit();
}

gboolean delete_event(GtkWidget* widget, GdkEvent* event, gpointer data)
{
    return true;
}

gboolean focus_out_event(GtkWidget* widget, GdkEventFocus* event, gpointer data)
{
    hide_window();
    return true;
}

gboolean key_press_event(GtkWidget* widget, GdkEventKey* event, gpointer data)
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

// macro for writing to keyfile
#define WRITE_PREF(type, group, key, var) \
    g_key_file_set_##type (kf, group, key, prefs.var)

void save_config(void)
{
    touch_file(config_file);
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_file, G_KEY_FILE_NONE, NULL))
    {
        WRITE_PREF(string, "Bindings", "launch", hotkey);
        WRITE_PREF(boolean, "Matching", "strict", strict_matching);
        WRITE_PREF(boolean, "Matching", "executable", match_executable);
        WRITE_PREF(uint64, "Update", "interval", update_timeout);
        WRITE_PREF(boolean, "Icons", "show", show_icon);
    }
    g_key_file_free(kf);
}

// macro for reading from keyfile, without overwriting default values
#define READ_PREF(type, group, key, var)            \
    if (g_key_file_has_key(kf, group, key, NULL))   \
        prefs.var = g_key_file_get_##type (kf, group, key, NULL)

void read_config(void)
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

void save_actions(void)
{
    touch_file(action_file);
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, action_file, G_KEY_FILE_NONE, NULL))
        for (size_t i = 0; i < action_list_size; i++)
        {
            Action* a = action_list + i;
            if (a->short_key.len > 0)
                g_key_file_set_string(kf, a->name.str, "short_key", a->short_key.str);
        }

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

void init_config_files(void)
{
    gchar* dir = g_build_filename(g_get_user_config_dir(), "fehlstart", NULL);
    g_mkdir_with_parents(dir, 0700);
    config_file = g_build_filename(dir, "fehlstart.rc", NULL);
    action_file = g_build_filename(dir, "actions.rc", NULL);
    g_free(dir);
    atexit(save_config);
    atexit(save_actions);
}

//------------------------------------------
// actions

void quit_action(String command, Action* action)
{
    gtk_main_quit();
}

void update_action(String command, Action* action)
{
    save_actions();
    update_launch_list();
    update_action_list();
    load_actions();
    malloc_stats();
}

void launch_action(String command, Action* action)
{
    pid_t pid = fork();
    if (pid != 0)
        return;

    setsid(); // "detatch" from parent process
    signal(SIGCHLD, SIG_DFL); // go back to default child behaviour
    Launch* launch = action->data;
    GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(launch->file.str);
    if (info != 0)
    {
        // todo: I'd like to pass arguments here, g_app_info_launch supports
        // uris and files but that's not really what I'm looking for
        g_app_info_launch(G_APP_INFO(info), NULL, NULL, NULL);
        g_object_unref(G_OBJECT(info));
    }
    exit(EXIT_SUCCESS);
}

//------------------------------------------
// misc

void register_hotkey(void)
{
    if (!prefs.one_time)
    {
        keybinder_init();
        keybinder_bind(prefs.hotkey, toggle_window, NULL);
        printf("hit %s to show window\n", prefs.hotkey);
    }
}

// gets run periodically
bool update_lists_cb(void *data)
{
    if (g_static_mutex_trylock(&lists_mutex))
    {
        update_action(STR_S(""), 0);
        g_static_mutex_unlock(&lists_mutex);
    }
    return true;
}

void run_updates(void)
{
    if (prefs.update_timeout && !prefs.one_time)
        g_timeout_add_seconds(60 * prefs.update_timeout, (GSourceFunc) update_lists_cb, NULL);
}

void create_widgets(void)
{
    window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_size_request(window, WINDOW_WIDTH, WINDOW_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(window), false);
    gtk_window_set_accept_focus(GTK_WINDOW(window), true);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_event), 0);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy), 0);
    g_signal_connect(window, "key-press-event", G_CALLBACK(key_press_event), 0);
    g_signal_connect(window, "focus-out-event", G_CALLBACK(focus_out_event), 0);

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

const char* get_desktop_env(void)
{
    // the problem with DESKTOP_SESSION is that some distros put their name there
    char* kde0 = getenv("KDE_SESSION_VERSION");
    char* kde1 = getenv("KDE_FULL_SESSION");
    char* gnome = getenv("GNOME_DESKTOP_SESSION_ID");
    char* session = getenv("DESKTOP_SESSION");
    char* current_desktop = getenv("XDG_CURRENT_DESKTOP");
    char* xdg_prefix = getenv("XDG_MENU_PREFIX");

    session = session ? : "";
    xdg_prefix = xdg_prefix ? : "";
    current_desktop = current_desktop ? : "";

    const char* desktop = "Old";
    if (strcasestr(session, "kde") || kde0 != NULL || kde1 != NULL)
        desktop = "KDE";
    else if (strcasestr(session, "gnome") || gnome != NULL)
        desktop = "GNOME";
    else if (strcasestr(session, "xfce") || strcasestr(xdg_prefix, "xfce"))
        desktop = "XFCE";
    else if (strcasestr(session, "lxde") || strcasestr(current_desktop, "lxde"))
        desktop = "LXDE";
    else if (strcasestr(session, "rox"))
        desktop = "ROX";

    printf("detected desktop: %s\n", desktop);
    return desktop;
}

void parse_commandline(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--one-way"))
            prefs.one_time = true;
        else if (!strcmp(argv[i], "--help"))
        {
            printf("fehlstart 0.2.4 (c) 2011 maep\noptions:\n"\
                "\t--one-way\texit after one use\n");
            exit(EXIT_SUCCESS);
        }
        else
            printf("invalid option: %s\n", argv[i]);
    }
}


// main

int main (int argc, char** argv)
{
    gtk_init(&argc, &argv);
    parse_commandline(argc, argv);

    signal(SIGCHLD, SIG_IGN); // let kernel raep the children, mwhahaha
    g_chdir(get_home_dir());
    g_desktop_app_info_set_desktop_env(get_desktop_env());

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
