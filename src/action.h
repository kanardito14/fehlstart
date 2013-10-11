/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#ifndef ACTION_H
#define ACTION_H

#include <time.h>
#include <stdbool.h>
#include <glib.h>
#include "strang.h"

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

// all functions modifying GHashTable arguments lock access

#endif
