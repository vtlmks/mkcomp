// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L
#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
#include <X11/extensions/Xpresent.h>
#include <GL/gl.h>
#include <GL/glx.h>

#define MAX_WINDOWS 4096
#define MAX_RULES 64
#define BLUR_MAX_LEVELS 6

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
typedef void (*glx_swap_interval_ext_func)(Display *, GLXDrawable, int);

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
	float fade;
	float opacity;
	float rule_opacity;
	float rule_corner_radius;
	int8_t rule_shadow;
	int8_t rule_blur;
	int8_t rule_border;
	uint8_t damaged;
	uint8_t needs_rebind;
	uint8_t no_effects;
	uint8_t fullscreen;
	uint8_t mapped;
	uint8_t urgent;
	uint8_t fading_out;
	char wm_class[128];
	char wm_instance[128];
};

struct rule {
	char wm_class[128];
	float opacity;
	float corner_radius;
	int8_t shadow;
	int8_t blur;
	int8_t border;
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
	uint32_t bg_warp_prog;
	int32_t bg_warp_time_loc;
	int32_t bg_warp_resolution_loc;
	int32_t bg_warp_color_loc;
	int32_t bg_warp_color2_loc;
	int32_t bg_warp_intensity_loc;
	int32_t bg_warp_speed_loc;
	uint64_t start_us;
	uint32_t win_prog;
	int32_t win_pos_loc;
	int32_t win_size_loc;
	int32_t win_radius_loc;
	int32_t win_dim_loc;
	int32_t win_opacity_loc;
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
	int32_t border_width_loc;
	int32_t border_color_loc;
	int32_t border_opacity_loc;
	uint32_t blur_down_prog;
	int32_t blur_down_halfpixel_loc;
	uint32_t blur_up_prog;
	int32_t blur_up_halfpixel_loc;
	uint32_t blur_composite_prog;
	int32_t blur_composite_pos_loc;
	int32_t blur_composite_size_loc;
	int32_t blur_composite_screen_size_loc;
	int32_t blur_composite_radius_loc;
	int32_t blur_composite_opacity_loc;
	int32_t blur_composite_desaturate_loc;
	int32_t blur_composite_darken_loc;
	uint32_t blur_fbo[BLUR_MAX_LEVELS];
	uint32_t blur_tex[BLUR_MAX_LEVELS];
	uint32_t blur_w[BLUR_MAX_LEVELS];
	uint32_t blur_h[BLUR_MAX_LEVELS];
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
	Atom atom_wm_opacity;
	Atom atom_root_pixmap;
	Pixmap root_bg_pixmap;
	GLXPixmap root_bg_glx_pixmap;
	uint32_t root_bg_tex;
	float bg_color[3];
	float bg_color2[3];
	uint8_t bg_shader_type;
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
	float default_opacity;
	float inactive_brightness;
	uint32_t focus_transition_ms;
	uint32_t fade_in_ms;
	uint32_t fade_out_ms;
	uint32_t blur_strength;
	float blur_desaturate;
	float blur_darken;
	uint64_t last_render_us;
	struct rule rules[MAX_RULES];
	uint32_t rule_count;
	struct win wins[MAX_WINDOWS];
	uint32_t win_count;
	int32_t present_opcode;
	XID present_eid;
	uint32_t present_serial;
	uint64_t last_msc;
	int32_t inotify_fd;
	uint8_t running;
	uint8_t dirty;
	uint8_t has_present;
	uint8_t vblank_pending;
	uint8_t vblank_ready;
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

// [=]===^=[ schedule_vblank ]===============================[=]
static void schedule_vblank(void) {
	if(comp.vblank_pending) {
		return;
	}
	if(comp.last_msc) {
		XPresentNotifyMSC(comp.dpy, comp.root, ++comp.present_serial, comp.last_msc + 1, 0, 0);
	} else {
		XPresentNotifyMSC(comp.dpy, comp.root, ++comp.present_serial, 0, 1, 0);
	}
	comp.vblank_pending = 1;
}

// [=]===^=[ run ]===========================================[=]
static void run(void) {
	comp.running = 1;
	comp.dirty = 1;
	int32_t xfd = ConnectionNumber(comp.dpy);

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

	while(comp.running) {
		uint8_t bg_animated = (!comp.fullscreen_win && (comp.bg_prog || comp.bg_warp_prog) && comp.bg_intensity > 0.0f && comp.bg_speed > 0.0f);
		uint8_t need_frame = (comp.dirty || bg_animated) && !comp.fullscreen_win;

		if(comp.has_present && need_frame) {
			schedule_vblank();
		}

		if(!XPending(comp.dpy)) {
			if(comp.has_present || !need_frame) {
				XFlush(comp.dpy);
				poll(fds, nfds, -1);
			}
		}

		while(XPending(comp.dpy)) {
			XEvent ev;
			XNextEvent(comp.dpy, &ev);
			handle_event(&ev);
		}

		if(reload_config) {
			load_config();
			for(uint32_t i = 0; i < comp.win_count; ++i) {
				apply_rules(&comp.wins[i]);
			}
			reload_config = 0;
			comp.dirty = 1;
			fprintf(stderr, "mkcomp: config reloaded (signal)\n");
		}

		if(comp.inotify_fd >= 0) {
			char inbuf[256];
			if(read(comp.inotify_fd, inbuf, sizeof(inbuf)) > 0) {
				load_config();
				for(uint32_t i = 0; i < comp.win_count; ++i) {
					apply_rules(&comp.wins[i]);
				}
				comp.dirty = 1;
				fprintf(stderr, "mkcomp: config reloaded\n");
			}
		}

		XFlush(comp.dpy);

		bg_animated = (!comp.fullscreen_win && comp.bg_prog && comp.bg_intensity > 0.0f && comp.bg_speed > 0.0f);
		need_frame = (comp.dirty || bg_animated) && !comp.fullscreen_win;

		if(comp.has_present) {
			if(need_frame && comp.vblank_ready) {
				comp.dirty = 0;
				render();
				comp.vblank_ready = 0;
			}

		} else {
			if(need_frame) {
				comp.dirty = 0;
				render();
			}
		}
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

	unbind_root_pixmap();
	if(comp.root_bg_tex) {
		glDeleteTextures(1, &comp.root_bg_tex);
	}

	if(comp.has_present) {
		XPresentFreeInput(comp.dpy, comp.root, comp.present_eid);
	}

	if(comp.bg_prog) {
		glDeleteProgram(comp.bg_prog);
	}
	if(comp.bg_warp_prog) {
		glDeleteProgram(comp.bg_warp_prog);
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
	if(comp.blur_down_prog) {
		glDeleteProgram(comp.blur_down_prog);
	}
	if(comp.blur_up_prog) {
		glDeleteProgram(comp.blur_up_prog);
	}
	if(comp.blur_composite_prog) {
		glDeleteProgram(comp.blur_composite_prog);
	}
	for(uint32_t i = 0; i < BLUR_MAX_LEVELS; ++i) {
		if(comp.blur_fbo[i]) {
			glDeleteFramebuffers(1, &comp.blur_fbo[i]);
		}
		if(comp.blur_tex[i]) {
			glDeleteTextures(1, &comp.blur_tex[i]);
		}
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
	comp.atom_wm_opacity = XInternAtom(comp.dpy, "_NET_WM_WINDOW_OPACITY", False);
	comp.atom_root_pixmap = XInternAtom(comp.dpy, "_XROOTPMAP_ID", False);

	char cm_name[32];
	snprintf(cm_name, sizeof(cm_name), "_NET_WM_CM_S%d", comp.screen);
	Atom cm_atom = XInternAtom(comp.dpy, cm_name, False);
	if(!XGetSelectionOwner(comp.dpy, cm_atom)) {
		XSetSelectionOwner(comp.dpy, cm_atom, comp.gl_win, CurrentTime);
	}

	XSelectInput(comp.dpy, comp.root, SubstructureNotifyMask | PropertyChangeMask);
	XSetErrorHandler(runtime_error_handler);

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);

	register_windows();
	bind_root_pixmap();
	update_active_window();
	run();
	cleanup();

	return 0;
}
