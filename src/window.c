#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

#include "custard.h"
#include "grid.h"
#include "ewmh.h"
#include "rules.h"
#include "vector.h"
#include "window.h"
#include "workspaces.h"
#include "xcb.h"

unsigned short manage_window(xcb_window_t window_id) {
    unsigned int index = 0;

    if (window_id == screen->root || window_id == ewmh_window_id)
        return 0;

    if (get_window_from_id(window_id))
        return 0;

    xcb_atom_t atom;
    xcb_ewmh_get_atoms_reply_t window_type;

    if (xcb_ewmh_get_wm_window_type_reply(ewmh_connection,
        xcb_ewmh_get_wm_window_type(ewmh_connection, window_id), &window_type,
        NULL)) {
        index = 0;

        for (; index < window_type.atoms_len; index++) {
            atom = window_type.atoms[index];

            if (atom == ewmh_connection->_NET_WM_WINDOW_TYPE_TOOLBAR ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_MENU ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_DROPDOWN_MENU ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_POPUP_MENU ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_DND ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_DOCK ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_DESKTOP ||
                atom == ewmh_connection->_NET_WM_WINDOW_TYPE_NOTIFICATION) {

                xcb_ewmh_get_atoms_reply_wipe(&window_type);
                return 0;

            } else if (atom == ewmh_connection->_NET_WM_WINDOW_TYPE_SPLASH) {
                xcb_ewmh_get_atoms_reply_wipe(&window_type);

                xcb_get_geometry_reply_t *geometry;
                geometry = xcb_get_geometry_reply(xcb_connection,
                    xcb_get_geometry(xcb_connection, window_id), NULL);

                unsigned int data[2] = {
                    (screen->width_in_pixels -
                        (unsigned int)geometry->width) / 2,
                    (screen->height_in_pixels -
                        (unsigned int)geometry->height) / 2
                };

                xcb_configure_window(xcb_connection, window_id,
                    XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, data);

                return 0;
            }
        }

        xcb_ewmh_get_atoms_reply_wipe(&window_type);
    }

    debug_output("Window type check completed");

    xcb_get_window_attributes_reply_t *attributes;
    attributes = xcb_get_window_attributes_reply(xcb_connection,
        xcb_get_window_attributes(xcb_connection, window_id), NULL);

    if (attributes && attributes->override_redirect)
        return 0;

    debug_output("Window override redirect check completed");

    xcb_grab_button(xcb_connection, 0, window_id, XCB_EVENT_MASK_BUTTON_PRESS,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
        XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY);

    window_t *window = (window_t *)malloc(sizeof(window_t));

    window->id = window_id;
    window->parent = xcb_generate_id(xcb_connection);

    window->workspace = focused_workspace;

    // TODO: check if window name or classes are matched by any rules

    window->x = grid_window_default_x;
    window->y = grid_window_default_y;
    window->height = grid_window_default_height;
    window->width = grid_window_default_width;

    /* Test for geometry rules */

    xcb_atom_t property_atom = XCB_ATOM_WM_NAME;
    xcb_atom_t property_type = XCB_GET_PROPERTY_TYPE_ANY;

    xcb_get_property_reply_t *property = xcb_get_property_reply(xcb_connection,
        xcb_get_property(xcb_connection, 0, window_id, property_atom,
        property_type, 0, 256), NULL);

    char *window_title = (char *)xcb_get_property_value(property);

    index = 0;

    window_rule_t *rule = NULL;
    named_geometry_t *geometry = NULL;

    for (; index < window_rules->size; index++) {
        rule = get_from_vector(window_rules, index);

        if (regex_match(window_title, rule->expression)) {
            debug_output("%s",
                "Window title matches rule expression, setting geometry");

            index = 0;

            for (; index < named_geometries->size; index++) {
                geometry = get_from_vector(named_geometries, index);

                if (!strcmp(geometry->name, rule->named_geometry)) {
                    window->x = geometry->x;
                    window->y = geometry->y;
                    window->height = geometry->height;
                    window->width = geometry->width;

                    break;
                }
            }

            break;
        }
    }

    // TODO: test

    // regex_match

    /* Create parent window */

    unsigned int data[] = { 0x00000000, 0x00000000, 1, colormap };

    xcb_create_window(xcb_connection, 32, window->parent, screen->root,
        window->x, window->y, window->width, window->height, border_total_size,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, visual->visual_id,
        XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
        XCB_CW_COLORMAP, data);

    data[0] = XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

    xcb_reparent_window(xcb_connection, window_id, window->parent, 0, 0);
    xcb_change_window_attributes(xcb_connection, window->parent,
        XCB_CW_EVENT_MASK, data);

    push_to_vector(managed_windows, window);

    change_window_geometry(window_id, window->x, window->y,
        window->height, window->width);

    debug_output("Window created and added to vector");

    return 1;
}

void unmanage_window(xcb_window_t window_id) {
    if (focused_window == window_id)
        focused_window = XCB_WINDOW_NONE;

    unsigned int index = 0;

    workspace_t *workspace = NULL;
    for (; index < workspaces->size; index++) {
        workspace = get_from_vector(workspaces, index);
        if (workspace->focused_window == window_id)
            workspace->focused_window = XCB_WINDOW_NONE;
    }

    index = 0;

    window_t *window = NULL; 
    for (; index < managed_windows->size; index++) {
        window = get_from_vector(managed_windows, index);
        if (window->id == window_id) {
            pull_from_vector(managed_windows, index);

            xcb_destroy_window(xcb_connection, window->parent);
            free(window);

            return;
        }
    }
}

window_t *get_window_from_id(xcb_window_t window_id) {
    window_t *window = NULL;

    for (unsigned int index = 0; index < managed_windows->size; index++) {
        window = get_from_vector(managed_windows, index);
        if (window->id == window_id)
            return window;
    }

    return NULL;
}

xcb_window_t get_focused_window() {
    xcb_window_t window_id = focused_window;

    if (window_id == screen->root || window_id == ewmh_window_id ||
        window_id == XCB_WINDOW_NONE)
        return 0;

    return window_id;
}

void focus_on_window(xcb_window_t window_id) {
    debug_output("Called");

    /* Unfocus the window */

    xcb_window_t previously_focused_window = get_focused_window();

    debug_output("Testing last focused window");

    if (previously_focused_window) {
        if (previously_focused_window == window_id)
            return;

        focused_window = XCB_WINDOW_NONE;

        xcb_grab_button(xcb_connection, 0, previously_focused_window,
            XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_BUTTON_INDEX_ANY,
            XCB_MOD_MASK_ANY);

        border_update(previously_focused_window);
    }

    /* Test if it's a parent window and redirect to the child if so */

    window_t *window = NULL;
    for (unsigned int index = 0; index < managed_windows->size; index++) {
        window = get_from_vector(managed_windows, index);

        if (window->parent == window_id) {
            debug_output("Parent targeted for focus, redirecting to child");
            window_id = window->id;
            break;
        }
    }

    /* Focus on new window */

    unsigned int data[2] = { XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE };

    xcb_change_property(xcb_connection, XCB_PROP_MODE_REPLACE, window_id,
        ewmh_connection->_NET_WM_STATE, ewmh_connection->_NET_WM_STATE,
        32, 2, data);

    xcb_set_input_focus(xcb_connection, XCB_INPUT_FOCUS_POINTER_ROOT,
        window_id, XCB_CURRENT_TIME);
    xcb_ewmh_set_active_window(ewmh_connection, 0, window_id);

    focused_window = window_id;

    debug_output("Focus set for manager");

    workspace_t *workspace = get_from_vector(workspaces,
        focused_workspace - 1);
    if (workspace->focused_window != window_id)
        workspace->focused_window = window_id;

    debug_output("Focus set for workspace");

    xcb_ungrab_button(xcb_connection, XCB_BUTTON_INDEX_ANY, window_id,
        XCB_MOD_MASK_ANY);

    debug_output("Released mouse keys for window");

    border_update(window_id);

}

void close_window(xcb_window_t window_id) {
    debug_output("Called");

    if (!get_focused_window())
        return;

    xcb_icccm_get_wm_protocols_reply_t protocols;
    xcb_icccm_get_wm_protocols_reply(xcb_connection,
        xcb_icccm_get_wm_protocols(xcb_connection, window_id,
            ewmh_connection->WM_PROTOCOLS), &protocols, NULL);

    xcb_atom_t delete_atom;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(xcb_connection,
        xcb_intern_atom(xcb_connection, 0, 16, "WM_DELETE_WINDOW"), NULL);

    if (reply) {
        delete_atom = reply->atom;

        debug_output("Attempting to use wm protocols to close window");
        debug_output("%d protocol atoms found", protocols.atoms_len);

        for (unsigned int index = 0; index < protocols.atoms_len; index++) {
            if (protocols.atoms[index] == delete_atom) {
                debug_output("Protocol atom found");

                xcb_client_message_event_t event = {
                    .response_type = XCB_CLIENT_MESSAGE,
                    .format = 32,
                    .sequence = 0,
                    .window = window_id,
                    .type = ewmh_connection->WM_PROTOCOLS,
                    .data.data32 = {
                        delete_atom,
                        XCB_CURRENT_TIME
                    }
                };
                debug_output("event created");

                xcb_send_event(xcb_connection, 0, window_id,
                    XCB_EVENT_MASK_NO_EVENT, (char *)&event);
                debug_output("Event sent");

                xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
                unmanage_window(window_id);
                return;
            }
        }
    }

    xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
    xcb_kill_client(xcb_connection, window_id);

    unmanage_window(window_id);
    debug_output("Window closed via xcb_kill_client");
}

void map_window(xcb_window_t window_id) {

    window_t *window = get_window_from_id(window_id);

    xcb_map_window(xcb_connection, window_id);
    if (window)
        xcb_map_window(xcb_connection, window->parent);
}

void unmap_window(xcb_window_t window_id) {
    window_t *window = get_window_from_id(window_id);

    xcb_unmap_window(xcb_connection, window_id);
    if (window)
        xcb_unmap_window(xcb_connection, window->parent);
}

void raise_window(xcb_window_t window_id) {
    window_t *window = get_window_from_id(window_id);

    if (window)
        window_id = window->parent;

    unsigned int data[1] = { XCB_STACK_MODE_ABOVE };

    xcb_configure_window(xcb_connection, window_id,
        XCB_CONFIG_WINDOW_STACK_MODE, data);
}

void lower_window(xcb_window_t window_id) {
    window_t *window = get_window_from_id(window_id);

    if (window)
        window_id = window->parent;

    unsigned int data[1] = { XCB_STACK_MODE_BELOW };

    xcb_configure_window(xcb_connection, window_id,
        XCB_CONFIG_WINDOW_STACK_MODE, data);
}

void change_window_geometry(xcb_window_t window_id, unsigned int x,
    unsigned int y, unsigned int height, unsigned int width) {

    unsigned int x_in_pixels = grid_get_x_offset(x) + grid_offset_left;
    unsigned int y_in_pixels = grid_get_y_offset(y) + grid_offset_top;

    unsigned int height_in_pixels = grid_get_span_y(height);
    unsigned int width_in_pixels = grid_get_span_x(width);

    debug_output("Window geometry change %d %d %dx%d",
        x, y, height, width);

    window_t *window = get_window_from_id(window_id);

    unsigned int data[4];

    /* TODO: create more efficient geometry method
        (only change window attributes as needed, not all of them
        regardless of whether or not they are needed)
        */

    if (window) {
        window->x = x;
        window->y = y;
        window->height = height;
        window->width = width;

        data[0] = width_in_pixels;
        data[1] = height_in_pixels;

        xcb_configure_window(xcb_connection, window_id,
            XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_WIDTH, data);

        window_id = window->parent;
    }

    data[0] = x_in_pixels;
    data[1] = y_in_pixels;
    data[2] = width_in_pixels;
    data[3] = height_in_pixels;

    xcb_configure_window(xcb_connection, window_id, XCB_CONFIG_WINDOW_X |
        XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT |
        XCB_CONFIG_WINDOW_WIDTH, data);
}

void border_update(xcb_window_t window_id) {
    window_t *window = get_window_from_id(window_id);

    if (!window || border_type == 0)
        return;

    debug_output("Border update issued");

    xcb_get_geometry_reply_t *geometry;
    geometry = xcb_get_geometry_reply(xcb_connection,
        xcb_get_geometry(xcb_connection, window->parent), NULL);

    if (!geometry)
        return;

    debug_output("Geometry check passed");

    unsigned int data[1] = { border_total_size };

    xcb_configure_window(xcb_connection, window->parent,
        XCB_CONFIG_WINDOW_BORDER_WIDTH, data);

    unsigned int colors[2] = {
        border_unfocused_color,
        border_background_color
    };

    unsigned short focused = 0;

    if (get_focused_window() == window_id)
        focused = 1;

    if (focused) {
        if (border_invert_colors) {
            colors[1] = border_focused_color;
            colors[0] = border_background_color;
        } else
            colors[0] = border_focused_color;
    } else {
        if (border_invert_colors) {
            colors[1] = border_unfocused_color;
            colors[0] = border_background_color;
        }
    }

    debug_output("Selecting update method");

    if (border_type == 1) {
        debug_output("Border method XCB_CW_BORDER_PIXEL");
        data[0] = colors[0];

        xcb_change_window_attributes(xcb_connection, window->parent,
            XCB_CW_BORDER_PIXEL, data);
    } else {
        debug_output("Border method pixmap + graphics_context");
        unsigned short height = geometry->height + (border_total_size * 2);
        unsigned short width = geometry->width + (border_total_size * 2);

        xcb_rectangle_t outer_border[4] = { { 0, 0, width, height } };

        xcb_pixmap_t pixmap = xcb_generate_id(xcb_connection);
        xcb_gcontext_t graphics_context = xcb_generate_id(xcb_connection);

        xcb_create_pixmap(xcb_connection, 32, pixmap, screen->root, width,
            height);
        xcb_create_gc(xcb_connection, graphics_context, pixmap, 0, NULL);

        data[0] = colors[1];
        xcb_change_gc(xcb_connection, graphics_context, XCB_GC_FOREGROUND,
            data);

        xcb_poly_fill_rectangle(xcb_connection, pixmap, graphics_context,
            4, outer_border);

        data[0] = colors[0];
        xcb_change_gc(xcb_connection, graphics_context, XCB_GC_FOREGROUND,
            data);

        if (border_type == 2) {
            xcb_rectangle_t double_border[5] = {

                {
                    (short)geometry->width, 0,
                    (short unsigned)border_inner_size,
                    (short unsigned)(geometry->height + border_inner_size)
                },

                {
                    0, (short)geometry->height,
                    (short unsigned)(geometry->width + border_inner_size),
                    (short unsigned)border_inner_size
                },

                {
                    (short)(geometry->width + border_total_size +
                        border_outer_size), 0,
                    (short unsigned)border_inner_size,
                    (short unsigned)(geometry->height + border_inner_size)
                },

                {
                    0, (short)(geometry->height + border_total_size +
                        border_outer_size),
                    (short unsigned)(geometry->width + border_inner_size),
                    (short unsigned)border_inner_size
                },

                {
                    (short)(geometry->width + border_total_size +
                        border_outer_size), (short)(geometry->height +
                        border_total_size +
                        border_outer_size),
                    (short unsigned)border_inner_size,
                    (short unsigned)border_inner_size
                }
            };

            xcb_poly_fill_rectangle(xcb_connection, pixmap, graphics_context,
                5, double_border);
        } else if (border_type == 3) {
            xcb_rectangle_t triple_border[8] = {

                {
                    (short)(geometry->width + border_outer_size), 0,
                    (short unsigned)border_inner_size,
                    (short unsigned)(geometry->height + border_outer_size +
                        border_inner_size)
                }, /* Right */

                {
                    0, (short)(geometry->height + border_outer_size),
                    (short unsigned)(geometry->width + border_outer_size),
                    (short unsigned)border_inner_size
                }, /* Bottom */

                {
                    (short)(geometry->width + border_total_size +
                        border_outer_size), 0,
                    (short unsigned)border_inner_size,
                    (short unsigned)(geometry->height + border_outer_size)
                }, /* Left */

                {
                    0, (short)(geometry->height + border_total_size +
                        border_outer_size),
                    (short unsigned)(geometry->width + border_outer_size),
                    (short unsigned)border_inner_size
                }, /* Top */

                {
                    (short)(geometry->width + border_total_size +
                        border_outer_size), (short)(geometry->height +
                        border_total_size +
                        border_outer_size),
                    (short unsigned)border_inner_size,
                    (short unsigned)(border_outer_size +
                        border_inner_size)
                }, /* Top-left, first fragment */

                {
                    (short)(geometry->width + border_total_size +
                        border_outer_size), (short)(geometry->height +
                        border_total_size +
                        border_outer_size),
                    (short unsigned)(border_outer_size +
                        border_inner_size),
                    (short unsigned)border_inner_size,
                }, /* Top-left, second fragment */

                {
                    (short)(geometry->width + border_outer_size), (short)(
                        geometry->height + border_total_size +
                        border_outer_size),
                    (short unsigned)border_inner_size,
                    (short unsigned)(border_inner_size +
                        border_outer_size)
                }, /* Top-right */

                {
                    (short)(geometry->width + border_total_size +
                        border_outer_size), (short)(geometry->height +
                        border_outer_size),
                    (short unsigned)(border_inner_size +
                        border_outer_size),
                    (short unsigned)border_inner_size
                } /* Bottom-left */
            };

            xcb_poly_fill_rectangle(xcb_connection, pixmap, graphics_context,
                8, triple_border);
        }

        data[0] = pixmap;
        xcb_change_window_attributes(xcb_connection, window->parent,
            XCB_CW_BORDER_PIXMAP, data);

        xcb_free_pixmap(xcb_connection, pixmap);
        xcb_free_gc(xcb_connection, graphics_context);
    }
}
