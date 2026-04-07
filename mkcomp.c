// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 199309L
#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <poll.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <GL/gl.h>
#include <GL/glx.h>

#define MAX_WINDOWS 4096

#ifndef GLX_TEXTURE_2D_EXT
#define GLX_TEXTURE_2D_EXT              0x20dc
#define GLX_TEXTURE_FORMAT_EXT          0x20d5
#define GLX_TEXTURE_FORMAT_RGB_EXT      0x20d9
#define GLX_TEXTURE_FORMAT_RGBA_EXT     0x20da
#define GLX_TEXTURE_TARGET_EXT          0x20d6
#define GLX_FRONT_LEFT_EXT              0x20de
#define GLX_BIND_TO_TEXTURE_RGB_EXT     0x20d0
#define GLX_BIND_TO_TEXTURE_RGBA_EXT    0x20d1
#define GLX_BIND_TO_TEXTURE_TARGETS_EXT 0x20d3
#define GLX_TEXTURE_2D_BIT_EXT          0x00000002
#define GLX_Y_INVERTED_EXT              0x20d4
#endif

typedef void (*glx_bind_tex_image_func)(Display *, GLXDrawable, int, int *);
typedef void (*glx_release_tex_image_func)(Display *, GLXDrawable, int);

struct win {
	Window id;
	Pixmap pixmap;
	GLXPixmap glx_pixmap;
	uint32_t tex;
	Damage damage;
	int32_t x;
	int32_t y;
	uint32_t w;
	uint32_t h;
	uint32_t depth;
	float dim_current;
	uint8_t damaged;
	uint8_t needs_rebind;
	uint8_t no_effects;
	uint8_t fullscreen;
	uint8_t mapped;
	uint8_t urgent;
};

struct compositor {
	Display *dpy;
	int32_t screen;
	Window root;
	uint32_t root_w;
	uint32_t root_h;
	int32_t damage_event;
	Window overlay;
	Window gl_win;
	GLXContext gl_ctx;
	GLXFBConfig tex_fbconfig_rgb;
	GLXFBConfig tex_fbconfig_rgba;
	glx_bind_tex_image_func bind_tex_image;
	glx_release_tex_image_func release_tex_image;
	uint32_t bg_prog;
	int32_t bg_time_loc;
	int32_t bg_resolution_loc;
	int32_t bg_color_loc;
	int32_t bg_intensity_loc;
	int32_t bg_speed_loc;
	uint64_t start_us;
	uint32_t win_prog;
	int32_t win_pos_loc;
	int32_t win_size_loc;
	int32_t win_radius_loc;
	int32_t win_dim_loc;
	uint32_t shadow_prog;
	int32_t shadow_pos_loc;
	int32_t shadow_size_loc;
	int32_t shadow_radius_loc;
	int32_t shadow_sigma_loc;
	int32_t shadow_opacity_loc;
	uint32_t border_prog;
	int32_t border_pos_loc;
	int32_t border_size_loc;
	int32_t border_radius_loc;
	int32_t border_color_loc;
	Window active_win;
	Window fullscreen_win;
	Atom atom_active_win;
	Atom atom_wm_type;
	Atom atom_type_dock;
	Atom atom_type_desktop;
	Atom atom_type_menu;
	Atom atom_type_popup_menu;
	Atom atom_type_dropdown_menu;
	Atom atom_type_tooltip;
	Atom atom_type_combo;
	Atom atom_wm_state;
	Atom atom_state_fullscreen;
	Atom atom_state_attention;
	float bg_color[3];
	float bg_intensity;
	float bg_speed;
	float shadow_radius;
	float shadow_opacity;
	float shadow_offset_x;
	float shadow_offset_y;
	float border_color[3];
	float urgent_border_color[3];
	float border_width;
	float corner_radius;
	float dim_inactive;
	uint32_t focus_transition_ms;
	uint64_t last_render_us;
	struct win wins[MAX_WINDOWS];
	uint32_t win_count;
	int32_t inotify_fd;
	uint8_t running;
};

static struct compositor comp;
static uint8_t init_error;
static volatile uint8_t reload_config;

// [=]===^=[ init_error_handler ]============================[=]
static int init_error_handler(Display *dpy, XErrorEvent *ev) {
	(void)dpy;
	if(ev->error_code == BadAccess) {
		init_error = 1;
	}
	return 0;
}

// [=]===^=[ runtime_error_handler ]=========================[=]
static int runtime_error_handler(Display *dpy, XErrorEvent *ev) {
	(void)dpy;
	(void)ev;
	return 0;
}

// [=]===^=[ signal_handler ]================================[=]
static void signal_handler(int32_t sig) {
	if(sig == SIGHUP) {
		reload_config = 1;
		signal(SIGHUP, signal_handler);

	} else {
		comp.running = 0;
	}
}

#include "config.c"
#include "shader.c"
#include "window.c"
#include "render.c"
#include "event.c"
#include "init.c"

// [=]===^=[ run ]===========================================[=]
static void run(void) {
	comp.running = 1;
	int32_t xfd = ConnectionNumber(comp.dpy);

	while(comp.running) {
		if(comp.fullscreen_win && !XPending(comp.dpy)) {
			struct pollfd fds[2];
			uint32_t nfds = 0;
			fds[nfds].fd = xfd;
			fds[nfds].events = POLLIN;
			++nfds;
			if(comp.inotify_fd >= 0) {
				fds[nfds].fd = comp.inotify_fd;
				fds[nfds].events = POLLIN;
				++nfds;
			}
			poll(fds, nfds, -1);
		}

		while(XPending(comp.dpy)) {
			XEvent ev;
			XNextEvent(comp.dpy, &ev);
			handle_event(&ev);
		}

		if(reload_config) {
			load_config();
			reload_config = 0;
			fprintf(stderr, "mkcomp: config reloaded (signal)\n");
		}

		if(comp.inotify_fd >= 0) {
			char inbuf[256];
			if(read(comp.inotify_fd, inbuf, sizeof(inbuf)) > 0) {
				load_config();
				fprintf(stderr, "mkcomp: config reloaded\n");
			}
		}

		XFlush(comp.dpy);
		render();
	}
}

// [=]===^=[ cleanup ]=======================================[=]
static void cleanup(void) {
	for(uint32_t i = 0; i < comp.win_count; ++i) {
		unbind_texture(&comp.wins[i]);
		if(comp.wins[i].damage) {
			XDamageDestroy(comp.dpy, comp.wins[i].damage);
		}
		if(comp.wins[i].tex) {
			glDeleteTextures(1, &comp.wins[i].tex);
		}
	}
	comp.win_count = 0;

	if(comp.bg_prog) {
		glDeleteProgram(comp.bg_prog);
	}
	if(comp.win_prog) {
		glDeleteProgram(comp.win_prog);
	}
	if(comp.shadow_prog) {
		glDeleteProgram(comp.shadow_prog);
	}
	if(comp.border_prog) {
		glDeleteProgram(comp.border_prog);
	}

	if(comp.gl_ctx) {
		glXMakeCurrent(comp.dpy, None, NULL);
		glXDestroyContext(comp.dpy, comp.gl_ctx);
	}
	if(comp.gl_win) {
		XDestroyWindow(comp.dpy, comp.gl_win);
	}
	if(comp.overlay) {
		XCompositeReleaseOverlayWindow(comp.dpy, comp.overlay);
	}

	if(comp.inotify_fd >= 0) {
		close(comp.inotify_fd);
	}

	XCompositeUnredirectSubwindows(comp.dpy, comp.root, CompositeRedirectManual);
	XSync(comp.dpy, False);
	XCloseDisplay(comp.dpy);
}

// [=]===^=[ main ]==========================================[=]
int main(int32_t argc, char **argv) {
	load_config();

	comp.inotify_fd = inotify_init1(IN_NONBLOCK);
	if(comp.inotify_fd >= 0) {
		char *home = getenv("HOME");
		if(home) {
			char dir[512];
			snprintf(dir, sizeof(dir), "%s/.config/mkcomp", home);
			inotify_add_watch(comp.inotify_fd, dir, IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO);
		}
	}

	if(argc >= 4) {
		comp.bg_color[0] = strtof(argv[1], NULL);
		comp.bg_color[1] = strtof(argv[2], NULL);
		comp.bg_color[2] = strtof(argv[3], NULL);
	}

	comp.dpy = XOpenDisplay(NULL);
	if(!comp.dpy) {
		fprintf(stderr, "mkcomp: cannot open display\n");
		return 1;
	}

	comp.screen = DefaultScreen(comp.dpy);
	comp.root = RootWindow(comp.dpy, comp.screen);
	comp.root_w = DisplayWidth(comp.dpy, comp.screen);
	comp.root_h = DisplayHeight(comp.dpy, comp.screen);

	fprintf(stderr, "mkcomp: compositing %ux%u screen\n", comp.root_w, comp.root_h);

	if(!init_extensions()) {
		return 1;
	}
	if(!setup_overlay()) {
		return 1;
	}
	if(!init_glx()) {
		return 1;
	}

	comp.start_us = get_time_us();
	comp.atom_active_win = XInternAtom(comp.dpy, "_NET_ACTIVE_WINDOW", False);
	comp.atom_wm_type = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE", False);
	comp.atom_type_dock = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	comp.atom_type_desktop = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	comp.atom_type_menu = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
	comp.atom_type_popup_menu = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
	comp.atom_type_dropdown_menu = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
	comp.atom_type_tooltip = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
	comp.atom_type_combo = XInternAtom(comp.dpy, "_NET_WM_WINDOW_TYPE_COMBO", False);
	comp.atom_wm_state = XInternAtom(comp.dpy, "_NET_WM_STATE", False);
	comp.atom_state_fullscreen = XInternAtom(comp.dpy, "_NET_WM_STATE_FULLSCREEN", False);
	comp.atom_state_attention = XInternAtom(comp.dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);

	XSelectInput(comp.dpy, comp.root, SubstructureNotifyMask | PropertyChangeMask);
	XSetErrorHandler(runtime_error_handler);

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);

	register_windows();
	update_active_window();
	run();
	cleanup();

	return 0;
}
