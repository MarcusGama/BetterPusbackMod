/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2022 Saso Kiselkov. All rights reserved.
 * Copyright 2024 Robert Wellinger. All rights reserved.
 */

#include <string.h>
#include <errno.h>
#include <curl/curl.h>

#include <XPLMGraphics.h>
#include <XPWidgets.h>
#include <XPStandardWidgets.h>
#include <XPLMPlanes.h>

#include <acfutils/assert.h>
#include <acfutils/dr.h>
#include <acfutils/helpers.h>
#include <acfutils/intl.h>
#include <acfutils/wav.h>
#include <acfutils/widget.h>

#include "cfg.h"
#include "msg.h"
#include "xplane.h"
 
#define GITURL "https://api.github.com/repos/olivierbutler/BetterPusbackMod/releases/latest"
#define	DL_TIMEOUT		5L		/* seconds */
#define MAX_VERSION_BF_SIZE 32000 

#define    CONF_FILENAME    "BetterPushback.cfg"
#define    CONF_DIRS    bp_xpdir, "Output", "preferences"
#define    MISC_FILENAME    "Miscellaneous.prf"

conf_t *bp_conf = NULL;

static bool_t inited = B_FALSE;
static XPWidgetID main_win = NULL;

#define    MARGIN            30

#define    BUTTON_HEIGHT        22
#define    BUTTON_WIDTH        200
#define    CHECKBOX_SIZE        20
#define    MIN_BOX_HEIGHT        45

#define    MAIN_WINDOW_HEIGHT    (MARGIN + 16 * BUTTON_HEIGHT + MARGIN)

#define    COPYRIGHT1    "BetterPushback " BP_PLUGIN_VERSION \
    "       © 2017-2014 S.Kiselkov, Robwell, Obutler. All rights reserved."
#define    COPYRIGHT2    "BetterPushback is open-source software. See COPYING for " \
            "more information."

/* Warning this is used on PO Translation as ID */
#define    TOOLTIP_HINT    "Hint: hover your mouse cursor over any knob to " \
            "show a short description of what it does."

static struct {
    XPWidgetID chinese;
    XPWidgetID english;
    XPWidgetID french;
    XPWidgetID german;
    XPWidgetID portuguese;
    XPWidgetID russian;
    XPWidgetID spanish;
    XPWidgetID italian;
    XPWidgetID xplang;

    XPWidgetID lang_pref_match_real;
    XPWidgetID lang_pref_native;
    XPWidgetID lang_pref_match_english;

    XPWidgetID disco_when_done;
    XPWidgetID ignore_set_park_brake;
    XPWidgetID ignore_doors_check;
    XPWidgetID hide_xp11_tug;
    XPWidgetID hide_magic_squares;
    XPWidgetID show_dev_menu;

    size_t num_radio_boxes;
    XPWidgetID *radio_boxes;
    size_t num_radio_devs;
    char **radio_devs;

    size_t num_sound_boxes;
    XPWidgetID *sound_boxes;
    size_t num_sound_devs;
    char **sound_devs;

    XPWidgetID save_cfg;
} buttons;

typedef struct {
    const char *string;
    XPWidgetID *widget;
    const char *tooltip;
} checkbox_t;

static fov_t fov_values = {0};
static struct {
    dr_t fov_h_deg;
    dr_t fov_h_ratio;
    dr_t fov_roll;
    dr_t fov_v_deg;
    dr_t fov_v_ratio;
    dr_t ui_scale;
} drs;

static struct {
    int ui_scaled; // 0 'scale' not yet initialised; 1 'scale' initialised and UI not scaled (100% ); 2 'scale' initialised and UI scaled (150% or more)
    float scale;
} ui_status = {0};

static struct {
    bool_t new_version_available;
	char version[MAX_VERSION_BF_SIZE] ;
} gitHubVersion ; 
 

struct curl_memory {
  char *response;
  size_t size;
};


void ui_status_init(void);

float get_ui_scale_from_pref(void);

void get_fov_values_impl(fov_t *values);

void set_fov_values_impl(fov_t *values);

void fetchGitVersion(void);

const char *match_real_tooltip =
        "Ground crew speaks my language only if the country the airport is\n"
        "in speaks my language. Otherwise the ground crew speaks English\n"
        "with a local accent.";
const char *native_tooltip = "Ground crew speaks my language irrespective "
                             "of what country the airport is in.";
const char *match_english_tooltip = "Ground crew always speaks English "
                                    "with a local accent.";
const char *dev_menu_tooltip = "Show the developer menu options.";
const char *save_prefs_tooltip = "Save current preferences to disk.";
const char *disco_when_done_tooltip =
        "Never ask and always automatically disconnect\n"
        "the tug when the pushback operation is complete.";
const char *ignore_park_brake_tooltip =
        "Never check \"set parking brake\".\n"
        "Some aircraft stuck on this check.\n"
        "It's on the beginning and on the end.\n"
        "This should solve this problem for some aircrafts. (KA350 for instance).";
const char *hide_xp11_tug_tooltip =
        "Hides default X-Plane 11 pushback tug.\n"
        "Restart X-Plane for this change to take effect.";
const char *hide_magic_squares_tooltip =
        "Hides the shortcut buttons on the left side of the screen.\n"
        "The first button starts the planner and the second starts the push-back.";
const char *ignore_doors_check_tooltip =
        "Don't check the doors and hatches status before starting the push-back";

static void
buttons_update(void) {
    const char *lang = "XX";
    lang_pref_t lang_pref;
    bool_t disco_when_done = B_FALSE;
    bool_t ignore_park_brake = B_FALSE;
    bool_t ignore_doors_check = B_FALSE;
    bool_t hide_magic_squares = B_FALSE;
    bool_t show_dev_menu = B_FALSE;
    const char *radio_dev = "", *sound_dev = "";

    (void) conf_get_str(bp_conf, "lang", &lang);


    (void) conf_get_b_per_acf("disco_when_done", &disco_when_done);
    (void) conf_get_b_per_acf("ignore_park_brake", &ignore_park_brake);
    (void) conf_get_b_per_acf("ignore_doors_check", &ignore_doors_check);

    (void) conf_get_str(bp_conf, "radio_device", &radio_dev);
    (void) conf_get_str(bp_conf, "sound_device", &sound_dev);
    (void) conf_get_b(bp_conf, "hide_magic_squares", &hide_magic_squares);

#define    SET_LANG_BTN(btn, l) \
    (XPSetWidgetProperty(buttons.btn, xpProperty_ButtonState, \
        strcmp(lang, l) == 0))
    SET_LANG_BTN(chinese, "cn");
    SET_LANG_BTN(german, "de");
    SET_LANG_BTN(english, "en");
    SET_LANG_BTN(french, "fr");
    SET_LANG_BTN(portuguese, "pt");
    SET_LANG_BTN(spanish, "es");
    SET_LANG_BTN(italian, "it");
    SET_LANG_BTN(russian, "ru");
    SET_LANG_BTN(xplang, "XX");
#undef    SET_LANG_BTN

    if (!conf_get_i(bp_conf, "lang_pref", (int *) &lang_pref))
        lang_pref = LANG_PREF_MATCH_REAL;
    XPSetWidgetProperty(buttons.lang_pref_match_real,
                        xpProperty_ButtonState, lang_pref == LANG_PREF_MATCH_REAL);
    XPSetWidgetProperty(buttons.lang_pref_native,
                        xpProperty_ButtonState, lang_pref == LANG_PREF_NATIVE);
    XPSetWidgetProperty(buttons.lang_pref_match_english,
                        xpProperty_ButtonState, lang_pref == LANG_PREF_MATCH_ENGLISH);
    XPSetWidgetProperty(buttons.disco_when_done,
                        xpProperty_ButtonState, disco_when_done);
    XPSetWidgetProperty(buttons.ignore_set_park_brake,
                        xpProperty_ButtonState, ignore_park_brake);
    XPSetWidgetProperty(buttons.ignore_doors_check,
                        xpProperty_ButtonState, ignore_doors_check);
    XPSetWidgetProperty(buttons.hide_magic_squares,
                        xpProperty_ButtonState, hide_magic_squares);
    XPSetWidgetProperty(buttons.show_dev_menu, xpProperty_ButtonState,
                        show_dev_menu);
    // X-Plane 12 doesn't support this feature
    if (bp_xp_ver >= 11000 && bp_xp_ver < 12000) {
        bool_t dont_hide = B_FALSE;
        (void) conf_get_b(bp_conf, "dont_hide_xp11_tug", &dont_hide);
        XPSetWidgetProperty(buttons.hide_xp11_tug,
                            xpProperty_ButtonState, !dont_hide);
    }

    XPSetWidgetProperty(buttons.radio_boxes[0], xpProperty_ButtonState,
                        *radio_dev == 0);
    for (size_t i = 0; i < buttons.num_radio_devs; i++) {
        XPSetWidgetProperty(buttons.radio_boxes[i + 1],
                            xpProperty_ButtonState,
                            strcmp(radio_dev, buttons.radio_devs[i]) == 0);
    }

    XPSetWidgetProperty(buttons.sound_boxes[0], xpProperty_ButtonState,
                        *sound_dev == 0);
    for (size_t i = 0; i < buttons.num_sound_devs; i++) {
        XPSetWidgetProperty(buttons.sound_boxes[i + 1],
                            xpProperty_ButtonState,
                            strcmp(sound_dev, buttons.sound_devs[i]) == 0);
    }
}

static int
main_window_cb(XPWidgetMessage msg, XPWidgetID widget, intptr_t param1,
               intptr_t param2) {
    XPWidgetID btn = (XPWidgetID) param1;

    UNUSED(param2);

    if (msg == xpMessage_CloseButtonPushed && widget == main_win) {
        set_pref_widget_status(B_FALSE);
        XPHideWidget(main_win);
        return (1);
    } else if (msg == xpMsg_PushButtonPressed) {
        if (btn == buttons.save_cfg && !bp_started) {
            (void) bp_conf_save();
            bp_sched_reload();
            set_pref_widget_status(B_FALSE);
        }
        return (0);
    } else if (msg == xpMsg_ButtonStateChanged) {
        if (btn == buttons.xplang) {
            conf_set_str(bp_conf, "lang", NULL);
        } else if (btn == buttons.german) {
            conf_set_str(bp_conf, "lang", "de");
        } else if (btn == buttons.english) {
            conf_set_str(bp_conf, "lang", "en");
        } else if (btn == buttons.spanish) {
            conf_set_str(bp_conf, "lang", "es");
        } else if (btn == buttons.italian) {
            conf_set_str(bp_conf, "lang", "it");
        } else if (btn == buttons.french) {
            conf_set_str(bp_conf, "lang", "fr");
        } else if (btn == buttons.portuguese) {
            conf_set_str(bp_conf, "lang", "pt");
        } else if (btn == buttons.russian) {
            conf_set_str(bp_conf, "lang", "ru");
        } else if (btn == buttons.chinese) {
            conf_set_str(bp_conf, "lang", "cn");
        } else if (btn == buttons.lang_pref_match_real) {
            conf_set_i(bp_conf, "lang_pref", LANG_PREF_MATCH_REAL);
        } else if (btn == buttons.lang_pref_native) {
            conf_set_i(bp_conf, "lang_pref", LANG_PREF_NATIVE);
        } else if (btn == buttons.lang_pref_match_english) {
            conf_set_i(bp_conf, "lang_pref",
                       LANG_PREF_MATCH_ENGLISH);
        } else if (btn == buttons.disco_when_done) {
            conf_set_b_per_acf("disco_when_done", XPGetWidgetProperty(buttons.disco_when_done,
                                                                 xpProperty_ButtonState, NULL));
        } else if (btn == buttons.ignore_set_park_brake) {
            conf_set_b_per_acf("ignore_park_brake", XPGetWidgetProperty(buttons.ignore_set_park_brake,
                                                                   xpProperty_ButtonState, NULL));
        } else if (btn == buttons.ignore_doors_check) {
            conf_set_b_per_acf("ignore_doors_check", XPGetWidgetProperty(buttons.ignore_doors_check,
                                                                   xpProperty_ButtonState, NULL));
        } else if (btn == buttons.show_dev_menu) {
            conf_set_b(bp_conf, "show_dev_menu",
                       XPGetWidgetProperty(buttons.show_dev_menu,
                                           xpProperty_ButtonState, NULL));
        } else if (bp_xp_ver >= 11000 && btn == buttons.hide_xp11_tug) {
            conf_set_b(bp_conf, "dont_hide_xp11_tug",
                       !XPGetWidgetProperty(buttons.hide_xp11_tug,
                                            xpProperty_ButtonState, NULL));
        } else if (btn == buttons.hide_magic_squares) {
            conf_set_b(bp_conf, "hide_magic_squares",
                       XPGetWidgetProperty(buttons.hide_magic_squares,
                                            xpProperty_ButtonState, NULL));
        }
        for (size_t i = 1; i < buttons.num_radio_boxes; i++) {
            if (btn == buttons.radio_boxes[i]) {
                conf_set_str(bp_conf, "radio_device",
                             buttons.radio_devs[i - 1]);
                break;
            }
        }
        if (btn == buttons.radio_boxes[0])
            conf_set_str(bp_conf, "radio_device", NULL);
        for (size_t i = 1; i < buttons.num_sound_boxes; i++) {
            if (btn == buttons.sound_boxes[i]) {
                conf_set_str(bp_conf, "sound_device",
                             buttons.sound_devs[i - 1]);
                break;
            }
        }
        if (btn == buttons.sound_boxes[0])
            conf_set_str(bp_conf, "sound_device", NULL);
        buttons_update();
    }

    return (0);
}

static int
measure_checkboxes_width(checkbox_t *checkboxes) {
    int width = 0;
    for (int i = 0; checkboxes[i].string != NULL; i++) {
        int w = XPLMMeasureString(xplmFont_Proportional,
                                  checkboxes[i].string, strlen(checkboxes[i].string));
        width = MAX(width, w);
    }
    return (width + CHECKBOX_SIZE);
}

static void
layout_checkboxes(checkbox_t *checkboxes, int x, int y, tooltip_set_t *tts) {
    int width = measure_checkboxes_width(checkboxes);
    int n;

    for (n = 0; checkboxes[n].string != NULL; n++);

    (void) create_widget_rel(x, y, B_FALSE, width, BUTTON_HEIGHT, 1,
                             checkboxes[0].string, 0, main_win, xpWidgetClass_Caption);
    y += BUTTON_HEIGHT;

    (void) create_widget_rel(x, y, B_FALSE, width + 7,
                             MAX((n - 1) * BUTTON_HEIGHT, MIN_BOX_HEIGHT), 1, "", 0, main_win,
                             xpWidgetClass_SubWindow);

    for (int i = 1; i < n; i++) {
        int off_x = x;
        if (checkboxes[i].widget != NULL) {
            *checkboxes[i].widget = create_widget_rel(x, y + 2,
                                                      B_FALSE, CHECKBOX_SIZE, CHECKBOX_SIZE, 1, "",
                                                      0, main_win, xpWidgetClass_Button);
            XPSetWidgetProperty(*checkboxes[i].widget,
                                xpProperty_ButtonType, xpRadioButton);
            XPSetWidgetProperty(*checkboxes[i].widget,
                                xpProperty_ButtonBehavior,
                                xpButtonBehaviorCheckBox);
            off_x += CHECKBOX_SIZE;
        }
        (void) create_widget_rel(off_x, y, B_FALSE,
                                 width - (off_x - x), BUTTON_HEIGHT, 1,
                                 checkboxes[i].string, 0, main_win, xpWidgetClass_Caption);
        if (checkboxes[i].tooltip != NULL) {
            tooltip_new(tts, x, y, CHECKBOX_SIZE + width,
                        BUTTON_HEIGHT, _(checkboxes[i].tooltip));
        }
        y += BUTTON_HEIGHT;
    }
}

static checkbox_t *
sound_checkboxes_init(const char *name, char ***devs_p, size_t *num_devs_p,
                      XPWidgetID **boxes, size_t *num_boxes) {
    size_t num_devs;
    char **devs = openal_list_output_devs(&num_devs);
    checkbox_t *cb;

    *devs_p = devs;
    *num_devs_p = num_devs;

    *num_boxes = num_devs + 1;
    *boxes = safe_calloc(*num_boxes, sizeof(XPWidgetID));
    cb = safe_calloc((*num_boxes) + 2, sizeof(*cb));

    cb[0].string = strdup(name);
    cb[1].string = strdup(_("Default output device"));
    cb[1].widget = *boxes;
    for (size_t i = 1; i < *num_boxes; i++) {
        if (strlen(devs[i - 1]) > 30) {
            const char *dev = devs[i - 1];
            char chkbx_name[40] = {0};
            strncat(chkbx_name, dev, 22);
            strcat(chkbx_name, "...");
            strcat(chkbx_name, &dev[strlen(dev) - 8]);
            cb[i + 1].string = strdup(chkbx_name);
        } else {
            cb[i + 1].string = strdup(devs[i - 1]);
        }
        cb[i + 1].widget = (*boxes) + i;
    }

    return (cb);
}

static void
free_checkboxes(checkbox_t *boxes) {
    for (checkbox_t *b = boxes; b->string != NULL; b++) {
        free((char *) b->string);
        free((char *) b->tooltip);
    }
    free(boxes);
}

static void
create_main_window(void) {
    tooltip_set_t *tts;
    int col1_width, col2_width, col3_width, col4_width;
    int main_window_width, l;
    char *prefs_title;
    size_t main_window_height = MAIN_WINDOW_HEIGHT ;

    checkbox_t col1[] = {
            {_("User interface"), NULL,                    NULL},
            {_("X-Plane's language"), &buttons.xplang,     NULL},
            {"Deutsch",               &buttons.german,     NULL},
            {"English",               &buttons.english,    NULL},
            {"Español",               &buttons.spanish,    NULL},
            {"Italiano",              &buttons.italian,    NULL},
            {"Français",              &buttons.french,     NULL},
            {"Português",             &buttons.portuguese, NULL},
            {"Русский",               &buttons.russian,    NULL},
            {"中文",                  &buttons.chinese,    NULL},
            {NULL,                NULL,                    NULL}
    };
    checkbox_t col2[] = {
            {_("Ground crew audio"), NULL, NULL},
            {
             _("My language only at domestic airports"),
                    &buttons.lang_pref_match_real,    match_real_tooltip
            },
            {
             _("My language at all airports"),
                    &buttons.lang_pref_native,        native_tooltip
            },
            {
             _("English at all airports"),
                    &buttons.lang_pref_match_english, match_english_tooltip
            },
            {NULL,                   NULL, NULL}
    };
    checkbox_t *radio_out = sound_checkboxes_init(_("Radio output device"),
                                                  &buttons.radio_devs, &buttons.num_radio_devs,
                                                  &buttons.radio_boxes, &buttons.num_radio_boxes);
    checkbox_t *sound_out = sound_checkboxes_init(_("Sound output device"),
                                                  &buttons.sound_devs, &buttons.num_sound_devs,
                                                  &buttons.sound_boxes, &buttons.num_sound_boxes);
    checkbox_t other[7] = {
            {_("Miscellaneous"), NULL, NULL},
            {
             _("Auto disconnect when done **"),
                    &buttons.disco_when_done,       disco_when_done_tooltip
            },
            {
             _("Ignore check parking brake is set **"),
                    &buttons.ignore_set_park_brake, ignore_park_brake_tooltip
            },
            {
             _("Ignore doors and hatches check **"),
                    &buttons.ignore_doors_check, ignore_doors_check_tooltip
            },
            {
             _("Hide the magic squares"),
                    &buttons.hide_magic_squares, hide_magic_squares_tooltip
            },
            {
             _("Hide default X-Plane 11 tug"),
                    &buttons.hide_xp11_tug,         hide_xp11_tug_tooltip
            },
            {NULL,               NULL, NULL}
    };

    if ((bp_xp_ver < 11000) || (bp_xp_ver >= 12000)) //Feature only for Xp11
        other[4] = (checkbox_t) {NULL, NULL, NULL};

    col1_width = measure_checkboxes_width(col1);
    col2_width = measure_checkboxes_width(col2);
    col3_width = measure_checkboxes_width(radio_out);
    col4_width = measure_checkboxes_width(sound_out);
    main_window_width = 4 * MARGIN + col1_width + col2_width + MAX(col3_width, col4_width);
    if ((buttons.num_radio_boxes + buttons.num_sound_boxes) > 6) {
        main_window_height += (buttons.num_radio_boxes + buttons.num_sound_boxes) * BUTTON_HEIGHT ;
    }
    l = snprintf(NULL, 0, "%s", _("BetterPushback Preferences"));
    prefs_title = safe_malloc(l + 1);
    snprintf(prefs_title, l + 1, "%s", _("BetterPushback Preferences"));
    main_win = create_widget_rel(100, 100, B_FALSE, main_window_width,
                                 main_window_height, 0, prefs_title, 1, NULL,
                                 xpWidgetClass_MainWindow);
    XPSetWidgetProperty(main_win, xpProperty_MainWindowHasCloseBoxes, 1);
    XPAddWidgetCallback(main_win, main_window_cb);
    free(prefs_title);

    tts = tooltip_set_new(main_win);
    tooltip_set_font_size(tts, 14);

    layout_checkboxes(col1, MARGIN, MARGIN, tts);
    layout_checkboxes(col2, MARGIN + col1_width + MARGIN, MARGIN, tts);
    layout_checkboxes(other, MARGIN + col1_width + MARGIN,
                      MARGIN + 4.5 * BUTTON_HEIGHT, tts);
    layout_checkboxes(radio_out, 3 * MARGIN + col1_width + col2_width, MARGIN, tts);
    layout_checkboxes(sound_out, 3 * MARGIN + col1_width + col2_width,
                      MARGIN + (buttons.num_radio_boxes + 1.5) * BUTTON_HEIGHT, tts);

#define LAYOUT_PUSH_BUTTON(var, x, y, w, h, label, tooltip) \
    do { \
        buttons.var = create_widget_rel(x, y, B_FALSE, w, h, 1, \
            label, 0, main_win, xpWidgetClass_Button); \
        if (tooltip != NULL) { \
            tooltip_new(tts, x, y, w, h, _(tooltip));   \
        } \
    } while (0)

    LAYOUT_PUSH_BUTTON(save_cfg, (main_window_width - BUTTON_WIDTH) / 2,
                       main_window_height - MARGIN, BUTTON_WIDTH, BUTTON_HEIGHT,
                       _("Save preferences"), save_prefs_tooltip);

    const int MAIN_WINDOW_SPACE = 25;

    create_widget_rel(MARGIN,
                      main_window_height - 101 - (MAIN_WINDOW_SPACE + 10), B_FALSE,
                      main_window_width - 4 * MARGIN,
                      BUTTON_HEIGHT, 1, _("** Settings related to the current aircraft"), 0, main_win,
                      xpWidgetClass_Caption);

    create_widget_rel(MARGIN,
                      main_window_height - 75 - MAIN_WINDOW_SPACE, B_FALSE,
                      main_window_width - 4 * MARGIN,
                      BUTTON_HEIGHT, 1, COPYRIGHT1, 0, main_win,
                      xpWidgetClass_Caption);
    create_widget_rel(MARGIN,
                      main_window_height - 62 - MAIN_WINDOW_SPACE, B_FALSE,
                      main_window_width - 4 * MARGIN,
                      BUTTON_HEIGHT, 1, _(COPYRIGHT2), 0, main_win,
                      xpWidgetClass_Caption);
    create_widget_rel(MARGIN,
                      main_window_height - 49 - (MAIN_WINDOW_SPACE - 10), B_FALSE,
                      main_window_width - 4 * MARGIN,
                      BUTTON_HEIGHT, 1, _(TOOLTIP_HINT), 0, main_win,
                      xpWidgetClass_Caption);

    free_checkboxes(radio_out);
    free_checkboxes(sound_out);
}

static void
destroy_main_window(void) {
    free_strlist(buttons.radio_devs, buttons.num_radio_devs);
    buttons.radio_devs = NULL;
    buttons.num_radio_devs = 0;
    free_strlist(buttons.sound_devs, buttons.num_sound_devs);
    buttons.sound_devs = NULL;
    buttons.num_sound_devs = 0;

    XPDestroyWidget(main_win, 1);
    main_win = NULL;
}

bool_t
bp_conf_init(void) {
    char *path;
    FILE *fp;

    ASSERT(!inited);

    path = mkpathname(CONF_DIRS, CONF_FILENAME, NULL);
    fp = fopen(path, "rb");
    if (fp != NULL) {
        int errline;

        bp_conf = conf_read(fp, &errline);
        if (bp_conf == NULL) {
            logMsg(BP_ERROR_LOG "error parsing configuration %s: syntax error "
                                "on line %d.", path, errline);
            fclose(fp);
            free(path);
            return (B_FALSE);
        }
        fclose(fp);
    } else {
        bp_conf = conf_create_empty();
    }
    free(path);

    inited = B_TRUE;

    fdr_find(&drs.fov_h_deg, "sim/graphics/view/field_of_view_horizontal_deg");
    fdr_find(&drs.fov_h_ratio, "sim/graphics/view/field_of_view_horizontal_ratio");
    fdr_find(&drs.fov_roll, "sim/graphics/view/field_of_view_roll_deg");
    fdr_find(&drs.fov_v_deg, "sim/graphics/view/field_of_view_vertical_deg");
    if (bp_xp_ver >= 12000) { // these one only exists in Xp12
        fdr_find(&drs.fov_v_ratio, "sim/graphics/view/field_of_view_vertical_ratio");
        fdr_find(&drs.ui_scale, "sim/graphics/misc/user_interface_scale");
    }

    fetchGitVersion();
    return (B_TRUE);
}

bool_t
bp_conf_save(void) {
    char *path = mkpathname(CONF_DIRS, NULL);
    bool_t res = B_FALSE;
    FILE *fp;
    bool_t isdir;

    if ((!file_exists(path, &isdir) || !isdir) &&
        !create_directory_recursive(path)) {
        logMsg(BP_ERROR_LOG "error writing configuration: "
                            "can't create parent directory %s", path);
        free(path);
        return (B_FALSE);
    }
    free(path);

    path = mkpathname(CONF_DIRS, CONF_FILENAME, NULL);
    fp = fopen(path, "wb");

    if (fp != NULL) {
        if (conf_write(bp_conf, fp)) {
            logMsg(BP_INFO_LOG "Write config file %s", path);
            res = B_TRUE;
        } else {
            logMsg(BP_ERROR_LOG "Error writing configuration %s: %s", path,
                   strerror(errno));
        }
        fclose(fp);
    }

    free(path);

    return (res);
}

void
bp_conf_fini(void) {
    if (!inited)
        return;

    if (main_win != NULL) {
        destroy_main_window();
        tooltip_fini();
    }
    conf_free(bp_conf);
    bp_conf = NULL;

    pop_fov_values();

    inited = B_FALSE;
}

static void
gui_init(void) {
    tooltip_init();
    create_main_window();
    buttons_update();
}

void
bp_conf_set_save_enabled(bool_t flag) {
    ASSERT(inited);
    if (main_win == NULL)
        gui_init();
    XPSetWidgetProperty(buttons.save_cfg, xpProperty_Enabled, flag);
}

void
bp_conf_open(void) {
    ASSERT(inited);
    if (main_win == NULL)
        gui_init();
    else
        buttons_update(); // again here as we may change to another aircraft without relauching X-plane
    XPShowWidget(main_win);
    set_pref_widget_status(B_TRUE);
}


void key_sanity(char *key) {
    int i;
    for (i = 0; key[i] != '\0'; i++) {
        if ((key[i] == ' ') || (key[i] == '.')) {
            key[i] = '_';
        }
    }
}

bool_t
conf_get_disco_when_done(char *my_acf, bool_t *value) {
    if (my_acf == NULL) {
        return conf_get_b(bp_conf, "disco_when_done",
                          value);
    } else {
        int l;
        bool_t result;
        char *key;
        l = snprintf(NULL, 0,
                     "disco_when_done_%s", my_acf);
        key = safe_malloc(l + 1);
        snprintf(key, l + 1,
                 "disco_when_done_%s", my_acf);
        key_sanity(key);
        result = conf_get_b(bp_conf, key, value);
        free(key);
        return result;
    }
}

bool_t
conf_get_ignore_park_brake(char *my_acf, bool_t *value) {
    if (my_acf == NULL) {
        return conf_get_b(bp_conf, "ignore_park_brake",
                          value);
    } else {
        int l;
        bool_t result;
        char *key;
        l = snprintf(NULL, 0,
                     "ignore_park_brake_%s", my_acf);
        key = safe_malloc(l + 1);
        snprintf(key, l + 1,
                 "ignore_park_brake_%s", my_acf);
        key_sanity(key);
        result = conf_get_b(bp_conf, key, value);
        free(key);
        return result;
    }
}


bool_t
conf_get_b_per_acf(char *my_key,  bool_t *value) {
    char my_acf[512], my_path[512];
    XPLMGetNthAircraftModel(0, my_acf, my_path);
    if (strlen(my_acf) == 0) {
        // if not aircraft found (should never happened), try the generic key
        return conf_get_b(bp_conf, my_key, value);
    } else {
        int l;
        bool_t result;
        char *key;
        l = snprintf(NULL, 0,
                     "%s_%s", my_key, my_acf);
        key = safe_malloc(l + 1);
        snprintf(key, l + 1,
                 "%s_%s", my_key, my_acf);
        key_sanity(key);
        result = conf_get_b(bp_conf, key, value);
        if (!result) {
            // if not found per aircraft, try the generic key
            result = conf_get_b(bp_conf, my_key, value);
        }
        free(key);
        return result;
    }
}

void
conf_set_b_per_acf(char *my_key,  bool_t value) {
    char my_acf[512], my_path[512];
    XPLMGetNthAircraftModel(0, my_acf, my_path);
    if (strlen(my_acf) == 0) {
        // if not aircraft found (should never happened), try the generic key
        (void) conf_set_b(bp_conf, my_key, value);
    } else {
        int l;
        char *key;
        l = snprintf(NULL, 0,
                     "%s_%s", my_key, my_acf);
        key = safe_malloc(l + 1);
        snprintf(key, l + 1,
                 "%s_%s", my_key, my_acf);
        key_sanity(key);
        (void) conf_set_b(bp_conf, key, value);
        free(key);
    }
}

// Save the fov values ratio,angle
// they need to be changed during the planner
void
push_reset_fov_values(void) {
    if (!fov_values.planner_running) {
        fov_t new_values = {0};
        get_fov_values_impl(&fov_values);
        fov_values.planner_running = B_TRUE;
        set_fov_values_impl(&new_values);
    }

}

void
get_fov_values_impl(fov_t *values) {
    values->fov_h_deg = dr_getf(&drs.fov_h_deg);
    values->fov_h_ratio = dr_getf(&drs.fov_h_ratio);
    values->fov_roll = dr_getf(&drs.fov_roll);
    values->fov_v_deg = dr_getf(&drs.fov_v_deg);
    if (bp_xp_ver >= 12000) // this one only exists in Xp12
        values->fov_v_ratio = dr_getf(&drs.fov_v_ratio);
}

void
pop_fov_values(void) {
    if (fov_values.planner_running) {
        set_fov_values_impl(&fov_values);
        fov_values.planner_running = B_FALSE;
    }

}

void
set_fov_values_impl(fov_t *values) {
    dr_setf(&drs.fov_h_deg, values->fov_h_deg);
    dr_setf(&drs.fov_h_ratio, values->fov_h_ratio);
    dr_setf(&drs.fov_roll, values->fov_roll);
    dr_setf(&drs.fov_v_deg, values->fov_v_deg);
    if (bp_xp_ver >= 12000) // this one only exists in Xp12
        dr_setf(&drs.fov_v_ratio, values->fov_v_ratio);
}

void
ui_status_init(void) {
    ui_status.ui_scaled = 1;
    if (bp_xp_ver >= 12000) {
        ui_status.scale = dr_getf(&drs.ui_scale);
    } else {
        //  no datatef available for Xp11
        // see pixel_multiplier key in Miscellaneous.prf
        ui_status.scale = get_ui_scale_from_pref();
    }
    ui_status.ui_scaled = (ui_status.scale > 1.1) ? 2 : 1;
}

void
BPGetScreenSizeUIScaled(int *w, int *h, bool_t get_ui_scale) {
    XPLMGetScreenSize(w, h);
    if ((ui_status.ui_scaled == 0) || get_ui_scale) {
        ui_status_init();
    }
    if (ui_status.ui_scaled == 2) {
        *w = (int) ((double) *w / ui_status.scale);
        *h = (int) ((double) *h / ui_status.scale);
    }
}

float
get_ui_scale_from_pref(void) {
    char *path = mkpathname(CONF_DIRS, MISC_FILENAME, NULL);
    const char *key = "pixel_multiplier";
    float scale = 1.0;
    FILE *fp = fopen(path, "rb");
    char *line = NULL;
    char *search;
    size_t cap = 0;
    int line_num;

    UNUSED(line_num);

    if (fp != NULL) {
        for (line_num = 1; getline(&line, &cap, fp) > 0; line_num++) {
            search = strstr(line, key);
            if (search != NULL) {
                scale = atof(search + strlen(key));
                break;
            }

        }
        free(line);
        fclose(fp);
    }

    free(path);
    return (scale);
}




 
static size_t wrCallback(void *data, size_t size, size_t nmemb, void *clientp)
{
  size_t realsize = size * nmemb;
  struct curl_memory *mem = (struct curl_memory *)clientp;
 
  char *ptr = realloc(mem->response, mem->size + realsize + 1);
  if(!ptr)
    return 0;  /* out of memory! */
 
  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;
 
  return realsize;
}

void
parse_response(char * response, char *parsed)
{
    char * firstpos = NULL;
    char * lastpos = NULL;
    size_t tag_length;


    memset(parsed, '\0', MAX_VERSION_BF_SIZE);

    firstpos = strstr(response, "tag_name");
    if (firstpos == NULL) {
        goto in_error;
    }
    firstpos = strstr(firstpos, "\"") + 1;
    if (firstpos == NULL) {
        goto in_error;
    }
    firstpos = strstr(firstpos, "\"") + 1;
    if (firstpos == NULL) {
        goto in_error;
    }
    lastpos = strstr(firstpos, "\"");
    if (lastpos == NULL) {
        goto in_error;
    }

    tag_length = lastpos - firstpos;

    if (( tag_length > (MAX_VERSION_BF_SIZE -1) ) || ( tag_length <= 0)) {
        logMsg("Response len %d over buffer len size %d.. skipping", (int)tag_length, MAX_VERSION_BF_SIZE - 1);
        goto in_error;
    }

    memcpy(parsed, firstpos, tag_length);
    return ;

    in_error:
    logMsg("Unable to parse git json response;");
    return ;

}
 
 
void fetchGitVersion(void)
{
    CURL *curl_handle;
    CURLcode res;
    gitHubVersion.new_version_available = B_FALSE;
    struct curl_memory response = {0};

    curl_global_init(CURL_GLOBAL_ALL);

    /* init the curl session */
    curl_handle = curl_easy_init();

    if (curl_handle) {
        struct curl_slist *chunk = NULL;

        chunk = curl_slist_append(chunk, "Accept: application/vnd.github+json");
        chunk = curl_slist_append(chunk, "X-GitHub-Api-Version: 2022-11-28");
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, chunk);

        curl_easy_setopt(curl_handle, CURLOPT_URL, GITURL);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, wrCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&response);

        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, DL_TIMEOUT);

        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L); // avoid SSL issue on Windows

        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "curl/8.3.0");

        res = curl_easy_perform(curl_handle);

        /* check for errors */
        if(res != CURLE_OK) {
            logMsg("curl_easy_perform() failed: %s.. skipping", curl_easy_strerror(res));
        }
        else {
            parse_response(response.response, gitHubVersion.version);
            gitHubVersion.new_version_available = 	(strcmp (gitHubVersion.version, BP_PLUGIN_VERSION) != 0);
            logMsg("current version %s / new available version %s / update available %s", BP_PLUGIN_VERSION, gitHubVersion.version, gitHubVersion.new_version_available ? "true":"false");
        }

        free(response.response);

        /* cleanup curl stuff */
        curl_easy_cleanup(curl_handle);
        curl_slist_free_all(chunk);


        /* we are done with libcurl, so clean it up */
        curl_global_cleanup();
    }
 
}

char * getPluginUpdateStatus(void) {
    return ( gitHubVersion.new_version_available ? gitHubVersion.version : NULL );
}