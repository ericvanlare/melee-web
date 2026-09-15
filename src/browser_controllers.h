#ifndef MELEE_WEB_BROWSER_CONTROLLERS_H
#define MELEE_WEB_BROWSER_CONTROLLERS_H

/* -1 means this legacy entry has no browser controller owner. Otherwise bit
 * 0..3 identifies connected physical ports, including devices awaiting setup. */
int melee_web_controllers_poll();
void melee_web_controllers_shutdown();

#endif
