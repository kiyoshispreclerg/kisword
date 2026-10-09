/* window.h - kisword's main window. */
#ifndef KS_WINDOW_H
#define KS_WINDOW_H

#include "config.h"

/* Builds the window, runs the main loop and saves cfg on exit. files: extra
 * module files from the command line (the first one is opened); ref: where
 * to open (e.g. "Jo 3:16"), or NULL for the last position. */
void ks_window_run(KsConfig *cfg, char **files, const char *ref);

#endif /* KS_WINDOW_H */
