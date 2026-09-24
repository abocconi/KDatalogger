#pragma once

#include "lvgl.h"

/**
 * @file kdl_text.h
 * @brief Every user-visible string of the GUI, in Italian.
 *
 * Kept in one place so the wording can be reviewed and changed without
 * touching page code, and so a second language would only need a second
 * table. The file is UTF-8: beyond ASCII the text fonts carry only the
 * Italian accented vowels, ° · • Δ — and the LV_SYMBOL_* glyphs listed in
 * tools/gen_fonts.py (see kdl_theme.h) -- any other character renders as
 * nothing. Readings use the numeric fonts, which carry far less.
 *
 * Space budget: key labels about 60 px in 14 px (8-9 characters), status bar
 * title in 10 px, body texts within their boxes (~360 px in 12 px). These
 * were measured in Montserrat, which is wider than the Barlow faces now in
 * use, so they hold with margin.
 *
 * Channel names are not here: they live in data_model_channels.c, shared
 * with the log file.
 */

/* -- Status bar ------------------------------------------------------------ */
#define KDL_TXT_STATUS_REC          "REC"
#define KDL_TXT_STATUS_STOPPED      "FERMO"
/** Last session failed to start or lost data (e.g. volume full). */
#define KDL_TXT_STATUS_LOG_ERROR    "ERRORE LOG"

/* -- Keys shared by several pages ----------------------------------------- */
#define KDL_TXT_KEY_USB             "USB"
#define KDL_TXT_KEY_BACK            "Indietro"
#define KDL_TXT_KEY_OK              "OK"
#define KDL_TXT_KEY_CANCEL          "Annulla"
#define KDL_TXT_KEY_PLUS            "+"
#define KDL_TXT_KEY_MINUS           "-"

/* -- Main page ------------------------------------------------------------ */
#define KDL_TXT_MAIN_TITLE          "LIVE"
#define KDL_TXT_KEY_GRAPH           "Grafico"
#define KDL_TXT_KEY_SETTINGS        "Impost."
/** Cylinder bank heading; the degree sign is U+00B0. */
#define KDL_TXT_MAIN_EXHAUST        "SCARICO \xC2\xB0" "C"
/** U+0394 GREEK CAPITAL DELTA: spread between hottest and coolest cylinder. */
#define KDL_TXT_MAIN_SPREAD         "\xCE\x94"
/** Reading of an unplugged probe: U+2014 EM DASH, present in the num fonts. */
#define KDL_TXT_VALUE_OPEN          "\xE2\x80\x94"
#define KDL_TXT_PROBE_OPEN          "sonda scollegata"
/** printf format, two %d: session minimum and maximum. U+00B7 MIDDLE DOT. */
#define KDL_TXT_EXTREMES_FMT        "min %d \xC2\xB7 max %d"
#define KDL_TXT_EXTREMES_NONE       "min -- \xC2\xB7 max --"

/* -- Graph page ------------------------------------------------------------ */
#define KDL_TXT_GRAPH_TITLE         "GRAFICO"
#define KDL_TXT_GRAPH_TITLE_HOLD    "PAUSA"
#define KDL_TXT_KEY_PREV_CHANNEL    "Can. -"
#define KDL_TXT_KEY_NEXT_CHANNEL    "Can. +"
#define KDL_TXT_KEY_HOLD            "Pausa"
#define KDL_TXT_KEY_RUN             "Avvia"

/* -- Settings page --------------------------------------------------------- */
#define KDL_TXT_SETTINGS_TITLE      "IMPOSTAZIONI"
#define KDL_TXT_SETTINGS_PERIOD     "Periodo campionamento"
#define KDL_TXT_SETTINGS_DATETIME   "Data / ora"
#define KDL_TXT_SETTINGS_BRIGHTNESS "Luminosità display"
#define KDL_TXT_SETTINGS_NOT_SET    "non impostata"
#define KDL_TXT_KEY_UP              LV_SYMBOL_UP
#define KDL_TXT_KEY_DOWN            LV_SYMBOL_DOWN
#define KDL_TXT_KEY_EDIT            "Cambia"
#define KDL_TXT_KEY_DONE            "Fatto"

/* -- Date/time page -------------------------------------------------------- */
#define KDL_TXT_DATETIME_TITLE      "DATA / ORA"
#define KDL_TXT_KEY_FIELD           "Campo"
#define KDL_TXT_KEY_SKIP            "Salta"
#define KDL_TXT_DATETIME_HINT       "Orologio senza batteria: l'ora si perde allo spegnimento."
/** printf format, one %u: seconds left before the boot prompt gives up. */
#define KDL_TXT_DATETIME_COUNTDOWN  "Si prosegue senza impostare l'ora tra %u s"

/* -- USB page -------------------------------------------------------------- */
#define KDL_TXT_USB_TITLE           "USB"
#define KDL_TXT_USB_HEADER          "COLLEGATO AL COMPUTER"
#define KDL_TXT_USB_SUBTITLE        "Acquisizione ferma. Log accessibili dal computer."
#define KDL_TXT_USB_STAT_FILE       "File sessione"
#define KDL_TXT_USB_STAT_FREE       "Spazio libero"
#define KDL_TXT_USB_STAT_SAMPLES    "Campioni"
#define KDL_TXT_USB_NO_FILE         "nessuno"
#define KDL_TXT_USB_ALERT_EJECT     "Espelli il disco dal computer per uscire."
#define KDL_TXT_USB_ALERT_FORCE     "Disco non espulso! Premi di nuovo per uscire."
#define KDL_TXT_KEY_EXIT            "Esci"
