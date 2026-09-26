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
/** printf format, one %u: seconds spanned by the plot. */
#define KDL_TXT_GRAPH_WINDOW_FMT    "finestra %u s"

/* -- Settings page --------------------------------------------------------- */
#define KDL_TXT_SETTINGS_TITLE      "IMPOSTAZIONI"
#define KDL_TXT_SETTINGS_PERIOD     "Periodo campionamento"
#define KDL_TXT_SETTINGS_GRAPH_WINDOW "Finestra grafico"
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

/* -- Firmware update page -------------------------------------------------- */
#define KDL_TXT_FW_TITLE            "AGGIORNAMENTO"
#define KDL_TXT_FW_WRITING          "Aggiornamento firmware"
/** printf format, one %s: version being installed. */
#define KDL_TXT_FW_WRITING_FMT      "Installazione della versione %s. Non spegnere il datalogger."
#define KDL_TXT_FW_RESTARTING       "Aggiornamento completato"
#define KDL_TXT_FW_RESTARTING_MSG   "Riavvio in corso..."
#define KDL_TXT_FW_INSTALLED        "Firmware aggiornato"
/** printf format, one %s: version now running. */
#define KDL_TXT_FW_INSTALLED_FMT    "Versione in uso: %s."
#define KDL_TXT_FW_ROLLED_BACK      "Aggiornamento non riuscito"
/** printf format, one %s: version that failed to start. */
#define KDL_TXT_FW_ROLLED_BACK_FMT  "La versione %s non si è avviata correttamente: è tornata in uso la versione precedente."
#define KDL_TXT_FW_INVALID          "File di aggiornamento danneggiato"
#define KDL_TXT_FW_INVALID_MSG      "Il file è stato rinominato in .bad. Copiarlo di nuovo dal computer e poi espellere il disco."
#define KDL_TXT_FW_MULTIPLE         "Troppi file di aggiornamento"
#define KDL_TXT_FW_MULTIPLE_MSG     "Sul disco ci sono più file di aggiornamento: lasciarne uno solo."
#define KDL_TXT_FW_ALREADY          "Versione già installata"
/** printf format, one %s: version already running. */
#define KDL_TXT_FW_ALREADY_FMT      "La versione %s è già in uso. Il file è stato rimosso."
#define KDL_TXT_FW_WRITE_FAILED     "Errore di aggiornamento"
#define KDL_TXT_FW_WRITE_FAILED_MSG "Scrittura non riuscita, firmware invariato. Il file resta sul disco e verrà riprovato."
/** printf format, one %u: percentage written. */
#define KDL_TXT_FW_PROGRESS_FMT     "%u %%"
