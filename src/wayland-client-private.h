/* 
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2010-2011 Intel Corporation
 * 
 * Permission to use, copy, modify, distribute, and sell this
 * software and its documentation for any purpose is hereby granted
 * without fee, provided that\n the above copyright notice appear in
 * all copies and that both that copyright notice and this permission
 * notice appear in supporting documentation, and that the name of
 * the copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 */

#ifndef _WAYLAND_CLIENT_PRIVATE_H
#define _WAYLAND_CLIENT_PRIVATE_H

#include <wayland-client.h>

/**
 * This file holds structures and routines that are private to
 * libwayland-client. It is provided for use by libraries that wish
 * to extend the protocol; it should not be used by applications or
 * toolkits, since it's not API or ABI stable.
 */

struct wl_listener {
	void (**callbacks)(void);
	void *user_data;
	struct wl_list link;
};

struct wl_proxy {
	/*< private >*/
	struct wl_object object;
	struct wl_list listener_list;
	struct wl_display *display;
	void *user_data;
};

struct wl_proxy *wl_proxy_create(struct wl_display *display,
				 const struct wl_interface *interface,
				 size_t proxy_size);
void wl_proxy_destroy(struct wl_proxy *proxy);

void *wl_display_bind(struct wl_display *display,
		      uint32_t name, const struct wl_interface *interface,
		      size_t proxy_size);

#endif /* _WAYLAND_CLIENT_PRIVATE_H */
