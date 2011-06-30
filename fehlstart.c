/*
*   fehlstart - a small launcher written in gnu c99
*   this source is publieshed under the GPLv3 license.
*   copyright 2011 by maep
*   build: gcc fehlstart.c -o fehlstart -std=gnu99 `pkg-config --cflags --libs gtk+-2.0`
*/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#include <malloc.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <endian.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gdesktopappinfo.h>

// macros
#define WELCOME_MESSAGE     "type something"
#define NO_MATCH_MESSAGE    "no match"

#define TRIGGER_KEY     GDK_space
#define TRIGGER_MOD     GDK_MOD4_MASK // MOD1 is alt, CONTROL is control

#define WINDOW_WIDTH    200
#define WINDOW_HEIGHT   100
#define DEFAULT_ICON    GTK_STOCK_FIND
#define NO_MATCH_ICON   GTK_STOCK_DIALOG_QUESTION
#define ICON_SIZE       GTK_ICON_SIZE_DIALOG

#define APPLICATIONS_DIR        "/usr/share/applications"
#define USER_APPLICATIONS_DIR   ".local/share/applications"
#define SETTINGS_FILE           ".config/fehlstart/settings"

#define INITIAL_LAUNCH_LIST_CAPACITY    0xff
#define INITIAL_STRING_HEAP_CAPACITY    0x4000
#define INPUT_STRING_SIZE               0xff
#define SHOW_IGNORE_TIME                100000

// string "constructor" macros
#define STR_S(arg) ((String) {(arg), sizeof(arg) - 1, false})   // for static strings (comile time)
#define STR_I(arg) {(arg), sizeof(arg) - 1, false}              // for initializer (compile time)
#define STR_D(arg) str_wrap(arg)                                // for dymanic strings (runtime)

typedef struct
{
    char* str;
    uint32_t len;
    bool can_free;
} String;

typedef struct
{
    String file;
    String name;
    String icon;
} Launch;

typedef struct
{
    String key;
    union
    {
        double  d;
        int64_t i;
        String  s;
    } value;
} Setting;

typedef struct Action_
{
    String label;
    String keyword;
    String icon;
    void*  data;
    void (*action) (String, struct Action_*);
} Action;

//------------------------------------------
// forward declarations
void update_action(String, Action*);
void quit_action(String, Action*);

//------------------------------------------
// actions

#define NUM_ACTIONS 2
static Action actions[NUM_ACTIONS] = {
    {STR_I("update fehlstart"), STR_I("update fehlstart"), STR_I(GTK_STOCK_REFRESH), 0 , update_action},
    {STR_I("quit fehlstart"), STR_I("quit fehlstart"), STR_I(GTK_STOCK_QUIT), 0, quit_action}
    };

// settings
#define SHOW_ICON 2
#define NUM_SETTINGS 3
static Setting settings[NUM_SETTINGS] = {
    {STR_I("s_browser"), {.s = STR_I("x-www-browser")}},
    {STR_I("s_terminal"), {.s = STR_I("x-terminal-emulator")}},
    {STR_I("b_show_icon"), {.i = 1}}
    };

//------------------------------------------
// global variables

static Launch* launch_list = 0;
static uint32_t launch_list_capacity = 0;
static uint32_t launch_list_size = 0;

static Action* action_list = 0;
static uint32_t action_list_capacity = 0;
static uint32_t action_list_size = 0;

static Action** filter_list = 0;
static uint32_t filter_list_capacity = 0;
static uint32_t filter_list_size = 0;
static uint32_t filter_list_choice = 0;

static char input_string[INPUT_STRING_SIZE];
static uint32_t input_string_size = 0;

// workaround for the focus_out_event bug
static struct timeval last_hide = {0, 0};

// gtk widgets
static GtkWidget* window = 0;
static GtkWidget* image = 0;
static GtkWidget* action_label = 0;
static GtkWidget* input_label = 0;

//------------------------------------------
// memory functions

static size_t heap_size = 0;

void* resize_mem(void* old_ptr, size_t size)
{
    size_t old_size = (old_ptr != 0) ? malloc_usable_size(old_ptr) : 0;
    void* ptr = realloc(old_ptr, size);
    if (ptr == 0)
    {
        printf("out of memory /o\\\n");
        exit(EXIT_FAILURE);
    }
    heap_size += - old_size + malloc_usable_size(ptr);
    // printf("%p -> %p (%u)\n", old_ptr, ptr, size);
    return ptr;
}

void* get_mem(size_t size)
{
    return resize_mem(0, size);
}

void free_mem(void* ptr)
{
    if (ptr == 0)
        return;
    heap_size -= malloc_usable_size(ptr);
    free(ptr);
}

//------------------------------------------
// string functions

String str_wrap(const char* s)
{
    return (s == 0) ?
        ((String) {"", 0, false}) :
        ((String) {(char*)s, strlen(s), false});
}

String str_wrap_n(const char* s, uint32_t n)
{
    return (s == 0) ?
        ((String) {"", 0, false}) :
        ((String) {(char*)s, n, false});
}

String str_create(uint32_t len)
{
    String s = {get_mem(len + 1), len, true};
    memset(s.str, 0, len + 1);
    return s;
}

void str_free(String s)
{
    if (s.can_free)
        free_mem(s.str);
}

// copies a string onto the string heap
String str_duplicate(String s)
{
    String dst = str_create(s.len);
    uint32_t i = 0;
    for (; s.str[i] != 0 && i < s.len; i++)
        dst.str[i] = s.str[i];
    dst.str[i] = 0;
    return dst;
}

String str_substring(String s, uint32_t begin, uint32_t length)
{
    char* str = s.str + MIN(begin, s.len);
    uint32_t len =  MIN(length, s.len - begin);
    return ((String) {str, len, false});
}

// ignoring the case
bool str_ends_with_i(String s, String suffix)
{
    uint32_t len = s.len;
    uint32_t slen = suffix.len;
    if (slen > len)
        return false;
    len -= slen;
    for (uint32_t i = 0; i < slen; i++)
        if (tolower(s.str[len + i]) != tolower(suffix.str[i]))
            return false;
    return true;
}

String str_to_lower(String s)
{
    for (uint32_t i = 0; i < s.len; i++)
        s.str[i] = tolower(s.str[i]);
    return s;
}

int str_compare_i(String a, String b)
{
    for (uint32_t i = 0; i < a.len && i < b.len; i++)
    {
        int diff = tolower(a.str[i]) - tolower(b.str[i]);
        if (diff != 0)
            return diff;
    }
    return a.len - b.len;
}

bool str_equal_i(String a, String b)
{
    return str_compare_i(a, b) == 0;
}

int str_compare(String a, String b)
{
    for (uint32_t i = 0; i < a.len && i < b.len; i++)
    {
        int diff = a.str[i] - b.str[i];
        if (diff != 0)
            return diff;
    }
    return a.len - b.len;
}

bool str_equal(String a, String b)
{
    return str_compare(a, b) == 0;
}

// creates a new string that must be freed
String assemble_path(String prefix, String suffix)
{
    String path = str_create(prefix.len + suffix.len + 1);

    strncpy(path.str, prefix.str, prefix.len);
    if (path.str[prefix.len - 1] != '/')
        path.str[prefix.len++] = '/';

    strncpy(path.str + prefix.len, suffix.str, suffix.len);

    path.str[prefix.len + suffix.len] = 0;
    path.len = prefix.len + suffix.len;

    return path;
}

//------------------------------------------
// filesystem helpers

bool is_file(String path)
{
    struct stat s;
    return stat(path.str, &s) == 0 ? S_ISREG(s.st_mode) : false;
}

bool is_directory(String path)
{
    struct stat s;
    return stat(path.str, &s) == 0 ? S_ISDIR(s.st_mode) : false;
}

off_t get_file_size(String path)
{
    struct stat s;
    return stat(path.str, &s) == 0 ? s.st_size : 0;
}

//------------------------------------------
// file io helpers

void fwrite_32(FILE* file, uint32_t value)
{
    value = htole32(value);
    fwrite(&value, sizeof(uint32_t), 1, file);
}

uint32_t fread_32(FILE* file)
{
    uint32_t value = 0;
    size_t err = fread(&value, sizeof(uint32_t), 1, file);
    value = le32toh(value);
    return (err == 1) ? value : 0;
}

void fwrite_str(FILE* file, String str)
{
    fwrite_32(file, str.len);
    fwrite(str.str, 1, str.len, file);
}

String fread_str(FILE* file)
{
    uint32_t len = fread_32(file);
    String s = str_create(len);
    size_t err = fread(s.str, 1, len, file);
    s.str[len] = 0;
    return (err == len) ?  s : STR_S("");
}

//------------------------------------------
// launcher function

void add_launcher(Launch* launch)
{
    if (launch_list_size + 1 > launch_list_capacity)
    {
        launch_list_capacity += INITIAL_LAUNCH_LIST_CAPACITY;
        launch_list = resize_mem(launch_list, launch_list_capacity * sizeof(Launch));
    }
    launch_list[launch_list_size] = *launch;
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
        launcher->file = file_name;
        const char* str = g_app_info_get_name(G_APP_INFO(info));
        launcher->name = str_duplicate(STR_D(str));

        str = g_key_file_get_value(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, 0);
        // if icon is not a full path but ends with file extension...
        // the skype package does this, and icon lookup works only without the .png
        String icon = STR_D(str);
        if (!g_path_is_absolute(icon.str)
            && (str_ends_with_i(icon, STR_S(".png"))
            || str_ends_with_i(icon, STR_S(".svg"))
            || str_ends_with_i(icon, STR_S(".xpm"))))
            icon.len -= 4;
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
    str_free(launcher->icon);
}

void populate_launch_list(String dir_name)
{
    printf("reading %s\n", dir_name.str);
    DIR* dir = opendir(dir_name.str);
    if (dir == 0)
        return;
    struct dirent* ent = 0;
    while ((ent = readdir(dir)) != 0)
    {
        String file_name = STR_D(ent->d_name);
        if (str_ends_with_i(file_name, STR_S(".desktop")))
        {
            String full_path = assemble_path(dir_name, file_name);
            Launch launcher;
            if (load_launcher(full_path, &launcher))
                add_launcher(&launcher);
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
    char* home = getenv("HOME");
    if (home != 0)
    {
        String home_dir = STR_D(home);
        String user_dir = STR_S(USER_APPLICATIONS_DIR);
        String full_path = assemble_path(home_dir, user_dir);
        populate_launch_list(full_path);
        str_free(full_path);
    }
}

void launch_action(String cmd, Action* action)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        setsid(); // "detatch" from parent process
        signal(SIGCHLD, SIG_DFL); // go back to default child behaviour
        Launch* launch = action->data;
        GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(launch->file.str);
        if (info != 0)
        {
            g_app_info_launch(G_APP_INFO(info), 0, 0, 0);
            g_object_unref(G_OBJECT(info));
        }
        exit(EXIT_SUCCESS);
    }
}

//------------------------------------------
// action functions

void add_action(Action* action)
{
    if (action_list_size + 1 > action_list_capacity)
    {
        action_list_capacity += INITIAL_LAUNCH_LIST_CAPACITY;
        action_list = resize_mem(action_list, action_list_capacity * sizeof(Action));
    }
    action_list[action_list_size] = *action;
    action_list_size++;
}

void clear_action_list(void)
{
    for (uint32_t i = 0; i < action_list_size; i++)
        str_free(action_list[i].keyword);
    action_list_size = 0;
}

int compare_action(const void* a, const void* b)
{
    return str_compare(((Action*)a)->keyword, ((Action*)b)->keyword);
}

void update_action_list(void)
{
    clear_action_list();
    for (size_t i = 0; i < launch_list_size; i++)
    {
        Launch* l = launch_list + i;
        String keyword = str_duplicate(l->name);
        str_to_lower(keyword);
        Action action = {l->name, keyword, l->icon, l, launch_action};
        add_action(&action);
    }
    for (size_t i = 0; i < NUM_ACTIONS; i++)
        add_action(actions + i);
    qsort(action_list, action_list_size, sizeof(Action), compare_action);
}

void filter_action_list(String filter)
{
    if (filter.len == 0)
        return;

    if (filter_list_capacity < action_list_capacity)
    {
        filter_list_capacity = action_list_capacity;
        filter_list = resize_mem(filter_list, filter_list_capacity * sizeof(Action*));
    }

    filter_list_size = 0;
    for (size_t i = 0; i < action_list_size; i++)
    {
        String key = action_list[i].keyword;
        if (strstr(key.str, filter.str) != 0)
            filter_list[filter_list_size++] = action_list + i;
    }
}

void update_action(String command, Action* action)
{
    update_launch_list();
    update_action_list();
}

void change_selected(int delta)
{
    if (filter_list_size != 0)
    {
        delta = (delta < 0) ?
            filter_list_size - (-delta % filter_list_size) :
            delta;
        filter_list_choice += delta;
        filter_list_choice %= filter_list_size;
    }
}

void run_selected(void)
{
    if (filter_list_size > 0)
    {
        Action* action = filter_list[filter_list_choice];
        if (action->action != 0)
        {
            String str = {input_string, input_string_size};
            action->action(str, action);
        }
    }
}

//------------------------------------------
// gui functions

void image_set_from_name(GtkImage* img, const char* name, GtkIconSize size)
{
    if (!settings[SHOW_ICON].value.i)
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

    if (input_string_size == 0)
    {
        action_text = WELCOME_MESSAGE;
        icon_name = STR_S(DEFAULT_ICON);
    }
    else if (filter_list_size > 0)
    {
        action_text = filter_list[filter_list_choice]->label.str;
        icon_name = filter_list[filter_list_choice]->icon;
    }

    gtk_label_set_text(GTK_LABEL(input_label), input_string);
    gtk_label_set_text(GTK_LABEL(action_label), action_text);
    gtk_widget_queue_draw(input_label);
    gtk_widget_queue_draw(action_label);

    image_set_from_name(GTK_IMAGE(image), icon_name.str, ICON_SIZE);
    gtk_widget_queue_draw(image);
}

String get_first_input_word(void)
{
    for (uint32_t i = 0;i < input_string_size; i++)
        if (input_string[i] == ' ')
            return str_wrap_n(input_string, i);
    return str_wrap_n(input_string, input_string_size);
}

void handle_text_input(GdkEventKey* event)
{
    if (event->keyval == GDK_KEY_BackSpace && input_string_size > 0)
        input_string_size--;
    else if (event->length == 1
        && (input_string_size + 1) < INPUT_STRING_SIZE
        && (input_string_size > 0 || event->keyval != GDK_KEY_space))
        input_string[input_string_size++] = tolower(event->keyval);
    input_string[input_string_size] = 0;
    filter_action_list(get_first_input_word());
    filter_list_choice = 0;
}

void hide_window(void)
{
    if (gtk_widget_get_visible(window))
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
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        gtk_window_present(GTK_WINDOW(window));
        gtk_window_set_keep_above(GTK_WINDOW(window), true);
    }
}

void toggle_window(void)
{
    if (gtk_widget_get_visible(window))
        hide_window();
    else
        show_window();
}

void quit_action(String command, Action* action)
{
    gtk_main_quit();
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
            change_selected(-1);
            show_selected();
            break;
        case GDK_KEY_Tab:
        case GDK_KEY_Down:
            change_selected(1);
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
// hotkey functions

GdkFilterReturn gdk_filter(GdkXEvent* gdk_xevent, GdkEvent* event, gpointer data)
{
    XKeyEvent* xevent = (XKeyEvent*) gdk_xevent;
    if (xevent->type == KeyPress && data != 0)
        ((void (*) (void)) data)();
    return GDK_FILTER_CONTINUE;
}

int xerror_handler(Display* display, XErrorEvent* event)
{
    const char* error_msg = (event->type == BadAccess) ?
        "failed to grab hotkey, another process is using it." :
        "got X11 error when grabbing hotkey";
    printf("%s\n", error_msg);
    exit(EXIT_FAILURE);
    return 0;
}

void register_hotkey(guint key, guint mask, void (*callback)(void))
{
    GdkWindow* rootwin = gdk_get_default_root_window();
    Display* disp = GDK_WINDOW_XDISPLAY(rootwin);
    Window win = GDK_WINDOW_XWINDOW(rootwin);
    KeyCode keycode = XKeysymToKeycode(disp, key);

    XSetErrorHandler(xerror_handler);
    XGrabKey(disp, keycode, mask, win, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(disp, keycode, LockMask | mask, win, False, GrabModeAsync, GrabModeAsync);

    XModifierKeymap* modmap = XGetModifierMapping(disp);
    size_t numlockmask = 0;
    for (size_t i = 0; i < 8; i++)
        for (size_t j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j]
                == XKeysymToKeycode(disp, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);

    if (numlockmask != 0)
    {
        XGrabKey(disp, keycode, numlockmask | mask, win, False, GrabModeAsync, GrabModeAsync);
        XGrabKey(disp, keycode, numlockmask | LockMask | mask, win, False, GrabModeAsync, GrabModeAsync);
    }
    gdk_window_add_filter(rootwin, gdk_filter, callback);
}

//------------------------------------------
// misc

void create_widgets(void)
{
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_WIDTH, WINDOW_HEIGHT);
    gtk_window_set_decorated(GTK_WINDOW(window), false);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), true);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), true);
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

int change_to_home_dir(void) // to stop gcc from bitching about ignored return value
{
    int err = chdir(getenv("HOME"));
    return err;
}

String detect_desktop_environment()
{
    // the problem with DESKTOP_SESSION is that some distros put their name there
    char* kde0 = getenv("KDE_SESSION_VERSION");
    char* kde1 = getenv("KDE_FULL_SESSION");
    char* gnome = getenv("GNOME_DESKTOP_SESSION_ID");
    char* session = getenv("DESKTOP_SESSION");
    char* xdg_prefix = getenv("XDG_MENU_PREFIX");
    session = session ? : "";
    xdg_prefix = (xdg_prefix == 0) ? "" : xdg_prefix;

    String desktop = STR_S("Old");
    if (kde0 != 0 || kde1 != 0 || strstr(session, "kde") != 0)
        desktop = STR_S("KDE");
    else if (gnome != 0 || strcmp(session, "gnome") == 0)
        desktop = STR_S("GNOME");
    else if (strstr(xdg_prefix, "xfce") != 0 || strcmp(session, "xfce") == 0)
        desktop = STR_S("XFCE");
    // todo:
    //~ LXDE	LXDE Desktop
    //~ ROX	    ROX Desktop
    //~ Unity	Unity Shell
    printf("guessing desktop environment: %s\n", desktop.str);
    return desktop;
}

// helps to find leaks
void clean_up(void)
{
    clear_action_list();
    free_mem(action_list);
    free_mem(filter_list);
    clear_launch_list();
    free_mem(launch_list);

    printf("%u bytes not freed\n", heap_size);
}

//------------------------------------------
// put it all to gether

int main (int argc, char** argv)
{
    printf("fehlstart 0.2 (c) 2011 maep\n");

    gtk_init(&argc, &argv);

    String de = detect_desktop_environment();
    g_desktop_app_info_set_desktop_env(de.str);

    update_launch_list();
    update_action_list();
    create_widgets();

    change_to_home_dir();
    signal(SIGCHLD, SIG_IGN); // let kernel raep the children, mwhahaha
    register_hotkey(TRIGGER_KEY, TRIGGER_MOD, toggle_window);

    printf("ready, hit win + space to get started.\n");
    gtk_main();

    clean_up();
    return EXIT_SUCCESS;
}
