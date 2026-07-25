#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

static inline void console_flush_input(void) {}
static inline void console_feed_input(char ch) { (void)ch; }

#endif
