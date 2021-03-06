#ifndef WINDOW_H
#define WINDOW_H

typedef struct {
    xcb_window_t id;
    xcb_window_t parent;

    unsigned int x;
    unsigned int y;
    unsigned int height;
    unsigned int width;

    unsigned short fullscreen;

    unsigned int workspace;
} window_t;

unsigned short manage_window(xcb_window_t);
void unmanage_window(xcb_window_t);

window_t *get_window_from_id(xcb_window_t);
xcb_window_t get_focused_window(void);

void focus_on_window(xcb_window_t);

void close_window(xcb_window_t);

void map_window(xcb_window_t);
void unmap_window(xcb_window_t);

void raise_window(xcb_window_t);
void lower_window(xcb_window_t);

void change_window_geometry(xcb_window_t, unsigned int, unsigned int,
    unsigned int, unsigned int);

void border_update(xcb_window_t);

#endif /* WINDOW_H */
