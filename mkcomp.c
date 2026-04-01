// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

// ./mkcomp                    # white/grey noise (default)
// ./mkcomp 0.4 0.6 1.0       # blueish
// ./mkcomp 0.3 1.0 0.5       # greenish
// ./mkcomp 1.0 0.4 0.3       # warm/reddish

#define _POSIX_C_SOURCE 199309L
#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
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
	uint8_t damaged;
	uint8_t needs_rebind;
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
	uint32_t bg_program;
	int32_t bg_time_loc;
	int32_t bg_resolution_loc;
	int32_t bg_color_loc;
	float bg_color[3];
	uint64_t start_us;
	struct win wins[MAX_WINDOWS];
	uint32_t win_count;
	uint8_t running;
};

static struct compositor comp;
static uint8_t init_error;

static char bg_vert_src[] =
	"#version 120\n"
	"void main() {\n"
	"	gl_Position = gl_Vertex;\n"
	"}\n";

static char bg_frag_src[] =
	"#version 120\n"
	"uniform float u_time;\n"
	"uniform vec2 u_resolution;\n"
	"uniform vec3 u_color;\n"
	"\n"
	"vec2 hash(vec2 p) {\n"
	"	vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
	"	q += dot(q, q.yzx + 33.33);\n"
	"	float a = fract((q.x + q.y) * q.z) * 6.2831853;\n"
	"	return vec2(cos(a), sin(a));\n"
	"}\n"
	"\n"
	"float noise(vec2 p) {\n"
	"	vec2 i = floor(p);\n"
	"	vec2 f = fract(p);\n"
	"	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"	return mix(\n"
	"		mix(dot(hash(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),\n"
	"		    dot(hash(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),\n"
	"		mix(dot(hash(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),\n"
	"		    dot(hash(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x),\n"
	"		u.y);\n"
	"}\n"
	"\n"
	"void main() {\n"
	"	vec2 uv = gl_FragCoord.xy / u_resolution.y;\n"
	"	float n = 0.0;\n"
	"	n += 0.50  * noise(uv * 2.0 + vec2(u_time * 0.02,  u_time * 0.01));\n"
	"	n += 0.25  * noise(uv * 4.0 - vec2(u_time * 0.015, u_time * 0.02));\n"
	"	n += 0.125 * noise(uv * 8.0 + vec2(u_time * 0.01,  u_time * -0.015));\n"
	"//	n = n * 0.5 + 0.5;\n"
	"  n = clamp(n * 0.7 + 0.5, 0.0, 1.0);  // stretches range, hits 0 at the low end\n"
	"	n *= 0.15;\n"
	"	float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;\n"
	"	n += dither;\n"
	"	gl_FragColor = vec4(n * u_color, 1.0);\n"
	"}\n";

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
	(void)sig;
	comp.running = 0;
}

// [=]===^=[ find_win ]======================================[=]
static struct win *find_win(Window id) {
	for(uint32_t i = 0; i < comp.win_count; ++i) {
		if(comp.wins[i].id == id) {
			return &comp.wins[i];
		}
	}
	return NULL;
}

// [=]===^=[ unbind_texture ]================================[=]
static void unbind_texture(struct win *w) {
	if(w->glx_pixmap) {
		glBindTexture(GL_TEXTURE_2D, w->tex);
		comp.release_tex_image(comp.dpy, w->glx_pixmap, GLX_FRONT_LEFT_EXT);
		glBindTexture(GL_TEXTURE_2D, 0);
		glXDestroyPixmap(comp.dpy, w->glx_pixmap);
		w->glx_pixmap = 0;
	}
	if(w->pixmap) {
		XFreePixmap(comp.dpy, w->pixmap);
		w->pixmap = 0;
	}
}

// [=]===^=[ bind_texture ]==================================[=]
static void bind_texture(struct win *w) {
	w->pixmap = XCompositeNameWindowPixmap(comp.dpy, w->id);
	if(!w->pixmap) {
		return;
	}

	GLXFBConfig fbconfig;
	int32_t tex_format;
	if(w->depth == 32) {
		fbconfig = comp.tex_fbconfig_rgba;
		tex_format = GLX_TEXTURE_FORMAT_RGBA_EXT;
	} else {
		fbconfig = comp.tex_fbconfig_rgb;
		tex_format = GLX_TEXTURE_FORMAT_RGB_EXT;
	}
	if(!fbconfig) {
		XFreePixmap(comp.dpy, w->pixmap);
		w->pixmap = 0;
		return;
	}

	int32_t pixmap_attrs[] = {
		GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
		GLX_TEXTURE_FORMAT_EXT, tex_format,
		None
	};
	w->glx_pixmap = glXCreatePixmap(comp.dpy, fbconfig, w->pixmap, pixmap_attrs);
	if(!w->glx_pixmap) {
		XFreePixmap(comp.dpy, w->pixmap);
		w->pixmap = 0;
		return;
	}

	if(!w->tex) {
		glGenTextures(1, &w->tex);
	}
	glBindTexture(GL_TEXTURE_2D, w->tex);
	comp.bind_tex_image(comp.dpy, w->glx_pixmap, GLX_FRONT_LEFT_EXT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	w->damaged = 0;
}

// [=]===^=[ remove_win ]====================================[=]
static void remove_win(Window id) {
	for(uint32_t i = 0; i < comp.win_count; ++i) {
		if(comp.wins[i].id != id) {
			continue;
		}
		unbind_texture(&comp.wins[i]);
		if(comp.wins[i].damage) {
			XDamageDestroy(comp.dpy, comp.wins[i].damage);
		}
		if(comp.wins[i].tex) {
			glDeleteTextures(1, &comp.wins[i].tex);
		}
		comp.wins[i] = comp.wins[comp.win_count - 1];
		--comp.win_count;
		return;
	}
}

// [=]===^=[ add_win ]=======================================[=]
static void add_win(Window id) {
	if(comp.win_count >= MAX_WINDOWS) {
		return;
	}
	if(find_win(id)) {
		return;
	}
	if(id == comp.overlay || id == comp.gl_win) {
		return;
	}

	XWindowAttributes wa;
	if(!XGetWindowAttributes(comp.dpy, id, &wa)) {
		return;
	}
	if(wa.class == InputOnly) {
		return;
	}
	if(wa.map_state != IsViewable) {
		return;
	}

	struct win *w = &comp.wins[comp.win_count];
	memset(w, 0, sizeof(*w));
	w->id = id;
	w->x = wa.x;
	w->y = wa.y;
	w->w = wa.width + 2 * wa.border_width;
	w->h = wa.height + 2 * wa.border_width;
	w->depth = wa.depth;

	w->damage = XDamageCreate(comp.dpy, id, XDamageReportNonEmpty);
	bind_texture(w);

	++comp.win_count;
}

// [=]===^=[ get_time_us ]===================================[=]
static uint64_t get_time_us(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// [=]===^=[ render ]========================================[=]
static void render(void) {
	for(uint32_t i = 0; i < comp.win_count; ++i) {
		struct win *w = &comp.wins[i];
		if(w->needs_rebind) {
			bind_texture(w);
			w->needs_rebind = 0;
		}
		w->damaged = 0;
	}

	glXWaitX();

	if(comp.bg_program) {
		float t = (float)(get_time_us() - comp.start_us) / 100000.0f;
		glUseProgram(comp.bg_program);
		glUniform1f(comp.bg_time_loc, t);
		glUniform2f(comp.bg_resolution_loc, (float)comp.root_w, (float)comp.root_h);
		glUniform3f(comp.bg_color_loc, comp.bg_color[0], comp.bg_color[1], comp.bg_color[2]);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glBegin(GL_QUADS);
		glVertex2f(-1.0f, -1.0f);
		glVertex2f(1.0f, -1.0f);
		glVertex2f(1.0f, 1.0f);
		glVertex2f(-1.0f, 1.0f);
		glEnd();

		glUseProgram(0);
	} else {
		glClearColor(0.f, 0.f, 0.f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, comp.root_w, comp.root_h, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	Window root_ret, parent_ret;
	Window *children = NULL;
	uint32_t nchildren = 0;
	XQueryTree(comp.dpy, comp.root, &root_ret, &parent_ret, &children, &nchildren);

	glEnable(GL_TEXTURE_2D);

	for(uint32_t i = 0; i < nchildren; ++i) {
		struct win *w = find_win(children[i]);
		if(!w || !w->tex) {
			continue;
		}

		int32_t x1 = w->x;
		int32_t y1 = w->y;
		int32_t x2 = w->x + (int32_t)w->w;
		int32_t y2 = w->y + (int32_t)w->h;

		glBindTexture(GL_TEXTURE_2D, w->tex);

		if(w->depth == 32) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}

		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f);
		glVertex2i(x1, y1);
		glTexCoord2f(1.0f, 0.0f);
		glVertex2i(x2, y1);
		glTexCoord2f(1.0f, 1.0f);
		glVertex2i(x2, y2);
		glTexCoord2f(0.0f, 1.0f);
		glVertex2i(x1, y2);
		glEnd();

		if(w->depth == 32) {
			glDisable(GL_BLEND);
		}
	}

	glDisable(GL_TEXTURE_2D);

	if(children) {
		XFree(children);
	}

	glXSwapBuffers(comp.dpy, comp.gl_win);
}

// [=]===^=[ handle_destroy ]================================[=]
static void handle_destroy(XDestroyWindowEvent *ev) {
	remove_win(ev->window);
}

// [=]===^=[ handle_map ]====================================[=]
static void handle_map(XMapEvent *ev) {
	add_win(ev->window);
}

// [=]===^=[ handle_unmap ]==================================[=]
static void handle_unmap(XUnmapEvent *ev) {
	remove_win(ev->window);
}

// [=]===^=[ handle_reparent ]===============================[=]
static void handle_reparent(XReparentEvent *ev) {
	if(ev->parent == comp.root) {
		add_win(ev->window);
	} else {
		remove_win(ev->window);
	}
}

// [=]===^=[ handle_configure ]==============================[=]
static void handle_configure(XConfigureEvent *ev) {
	struct win *w = find_win(ev->window);
	if(!w) {
		return;
	}

	uint32_t new_w = ev->width + 2 * ev->border_width;
	uint32_t new_h = ev->height + 2 * ev->border_width;
	uint8_t resized = (w->w != new_w || w->h != new_h);

	w->x = ev->x;
	w->y = ev->y;
	w->w = new_w;
	w->h = new_h;

	if(resized) {
		unbind_texture(w);
		w->needs_rebind = 1;
	}
}

// [=]===^=[ handle_damage_event ]===========================[=]
static void handle_damage_event(XDamageNotifyEvent *ev) {
	struct win *w = find_win(ev->drawable);
	if(!w) {
		return;
	}
	w->damaged = 1;
	XDamageSubtract(comp.dpy, w->damage, None, None);
}

// [=]===^=[ handle_event ]==================================[=]
static void handle_event(XEvent *ev) {
	if(ev->type == DestroyNotify) {
		handle_destroy(&ev->xdestroywindow);

	} else if(ev->type == MapNotify) {
		handle_map(&ev->xmap);

	} else if(ev->type == UnmapNotify) {
		handle_unmap(&ev->xunmap);

	} else if(ev->type == ReparentNotify) {
		handle_reparent(&ev->xreparent);

	} else if(ev->type == ConfigureNotify) {
		handle_configure(&ev->xconfigure);

	} else if(ev->type == comp.damage_event + XDamageNotify) {
		handle_damage_event((XDamageNotifyEvent *)ev);
	}
}

// [=]===^=[ find_tex_fbconfig ]=============================[=]
static GLXFBConfig find_tex_fbconfig(uint32_t depth, uint8_t rgba) {
	int32_t nconfigs;
	GLXFBConfig *configs = glXGetFBConfigs(comp.dpy, comp.screen, &nconfigs);
	if(!configs) {
		return NULL;
	}

	GLXFBConfig result = NULL;

	for(int32_t i = 0; i < nconfigs; ++i) {
		int32_t val;

		glXGetFBConfigAttrib(comp.dpy, configs[i], GLX_DRAWABLE_TYPE, &val);
		if(!(val & GLX_PIXMAP_BIT)) {
			continue;
		}

		glXGetFBConfigAttrib(comp.dpy, configs[i], GLX_BIND_TO_TEXTURE_TARGETS_EXT, &val);
		if(!(val & GLX_TEXTURE_2D_BIT_EXT)) {
			continue;
		}

		if(rgba) {
			glXGetFBConfigAttrib(comp.dpy, configs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &val);
		} else {
			glXGetFBConfigAttrib(comp.dpy, configs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &val);
		}
		if(!val) {
			continue;
		}

		XVisualInfo *vi = glXGetVisualFromFBConfig(comp.dpy, configs[i]);
		if(!vi) {
			continue;
		}
		uint32_t d = vi->depth;
		XFree(vi);

		if(d != depth) {
			continue;
		}

		result = configs[i];
		break;
	}

	XFree(configs);
	return result;
}

// [=]===^=[ init_extensions ]===============================[=]
static uint8_t init_extensions(void) {
	int32_t event_base, error_base;
	int32_t major, minor;

	if(!XCompositeQueryExtension(comp.dpy, &event_base, &error_base)) {
		fprintf(stderr, "mkcomp: XComposite extension not available\n");
		return 0;
	}
	major = 0;
	minor = 0;
	XCompositeQueryVersion(comp.dpy, &major, &minor);
	if(major == 0 && minor < 2) {
		fprintf(stderr, "mkcomp: XComposite 0.2+ required (have %d.%d)\n", major, minor);
		return 0;
	}

	if(!XDamageQueryExtension(comp.dpy, &comp.damage_event, &error_base)) {
		fprintf(stderr, "mkcomp: XDamage extension not available\n");
		return 0;
	}

	int32_t fixes_event;
	if(!XFixesQueryExtension(comp.dpy, &fixes_event, &error_base)) {
		fprintf(stderr, "mkcomp: XFixes extension not available\n");
		return 0;
	}

	return 1;
}

// [=]===^=[ setup_overlay ]=================================[=]
static uint8_t setup_overlay(void) {
	XSetErrorHandler(init_error_handler);
	init_error = 0;
	XCompositeRedirectSubwindows(comp.dpy, comp.root, CompositeRedirectManual);
	XSync(comp.dpy, False);
	if(init_error) {
		fprintf(stderr, "mkcomp: another compositor is already running\n");
		return 0;
	}

	comp.overlay = XCompositeGetOverlayWindow(comp.dpy, comp.root);
	if(!comp.overlay) {
		fprintf(stderr, "mkcomp: failed to get composite overlay window\n");
		return 0;
	}

	XserverRegion region = XFixesCreateRegion(comp.dpy, NULL, 0);
	XFixesSetWindowShapeRegion(comp.dpy, comp.overlay, ShapeInput, 0, 0, region);
	XFixesDestroyRegion(comp.dpy, region);

	return 1;
}

// [=]===^=[ compile_shader ]================================[=]
static uint32_t compile_shader(uint32_t type, char *src) {
	uint32_t s = glCreateShader(type);
	glShaderSource(s, 1, (void *)&src, NULL);
	glCompileShader(s);

	int32_t ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if(!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "mkcomp: shader error: %s\n", log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

// [=]===^=[ init_bg_shader ]================================[=]
static void init_bg_shader(void) {
	uint32_t vs = compile_shader(GL_VERTEX_SHADER, bg_vert_src);
	uint32_t fs = compile_shader(GL_FRAGMENT_SHADER, bg_frag_src);
	if(!vs || !fs) {
		if(vs) {
			glDeleteShader(vs);
		}
		if(fs) {
			glDeleteShader(fs);
		}
		fprintf(stderr, "mkcomp: background shader failed, using solid color\n");
		return;
	}

	comp.bg_program = glCreateProgram();
	glAttachShader(comp.bg_program, vs);
	glAttachShader(comp.bg_program, fs);
	glLinkProgram(comp.bg_program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	int32_t ok;
	glGetProgramiv(comp.bg_program, GL_LINK_STATUS, &ok);
	if(!ok) {
		char log[512];
		glGetProgramInfoLog(comp.bg_program, sizeof(log), NULL, log);
		fprintf(stderr, "mkcomp: shader link error: %s\n", log);
		glDeleteProgram(comp.bg_program);
		comp.bg_program = 0;
		return;
	}

	comp.bg_time_loc = glGetUniformLocation(comp.bg_program, "u_time");
	comp.bg_resolution_loc = glGetUniformLocation(comp.bg_program, "u_resolution");
	comp.bg_color_loc = glGetUniformLocation(comp.bg_program, "u_color");
}

// [=]===^=[ init_glx ]======================================[=]
static uint8_t init_glx(void) {
	char *exts = (char *)glXQueryExtensionsString(comp.dpy, comp.screen);
	if(!exts || !strstr(exts, "GLX_EXT_texture_from_pixmap")) {
		fprintf(stderr, "mkcomp: GLX_EXT_texture_from_pixmap not supported\n");
		return 0;
	}

	comp.bind_tex_image = (glx_bind_tex_image_func)glXGetProcAddress((GLubyte *)"glXBindTexImageEXT");
	comp.release_tex_image = (glx_release_tex_image_func)glXGetProcAddress((GLubyte *)"glXReleaseTexImageEXT");
	if(!comp.bind_tex_image || !comp.release_tex_image) {
		fprintf(stderr, "mkcomp: failed to load texture_from_pixmap functions\n");
		return 0;
	}

	comp.tex_fbconfig_rgb = find_tex_fbconfig(24, 0);
	comp.tex_fbconfig_rgba = find_tex_fbconfig(32, 1);
	if(!comp.tex_fbconfig_rgb) {
		fprintf(stderr, "mkcomp: no FBConfig for 24-bit texture binding\n");
		return 0;
	}

	int32_t render_attrs[] = {
		GLX_DOUBLEBUFFER, True,
		GLX_RED_SIZE, 8,
		GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8,
		GLX_ALPHA_SIZE, 0,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
		None
	};
	int32_t nconfigs;
	GLXFBConfig *configs = glXChooseFBConfig(comp.dpy, comp.screen, render_attrs, &nconfigs);
	if(!configs || nconfigs == 0) {
		fprintf(stderr, "mkcomp: no suitable FBConfig for rendering\n");
		return 0;
	}
	GLXFBConfig render_fbconfig = configs[0];
	XFree(configs);

	comp.gl_ctx = glXCreateNewContext(comp.dpy, render_fbconfig, GLX_RGBA_TYPE, NULL, True);
	if(!comp.gl_ctx) {
		fprintf(stderr, "mkcomp: failed to create GL context\n");
		return 0;
	}

	XVisualInfo *vi = glXGetVisualFromFBConfig(comp.dpy, render_fbconfig);
	if(!vi) {
		fprintf(stderr, "mkcomp: failed to get visual from FBConfig\n");
		return 0;
	}

	Colormap cmap = XCreateColormap(comp.dpy, comp.overlay, vi->visual, AllocNone);
	XSetWindowAttributes swa;
	memset(&swa, 0, sizeof(swa));
	swa.colormap = cmap;
	swa.border_pixel = 0;

	comp.gl_win = XCreateWindow(comp.dpy, comp.overlay, 0, 0, comp.root_w, comp.root_h, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWBorderPixel, &swa);
	XFree(vi);

	if(!comp.gl_win) {
		fprintf(stderr, "mkcomp: failed to create GL window\n");
		return 0;
	}

	XMapWindow(comp.dpy, comp.gl_win);

	XserverRegion region = XFixesCreateRegion(comp.dpy, NULL, 0);
	XFixesSetWindowShapeRegion(comp.dpy, comp.gl_win, ShapeInput, 0, 0, region);
	XFixesDestroyRegion(comp.dpy, region);

	if(!glXMakeCurrent(comp.dpy, comp.gl_win, comp.gl_ctx)) {
		fprintf(stderr, "mkcomp: failed to activate GL context\n");
		return 0;
	}

	init_bg_shader();

	return 1;
}

// [=]===^=[ register_windows ]==============================[=]
static void register_windows(void) {
	Window root_ret, parent_ret;
	Window *children = NULL;
	uint32_t nchildren = 0;

	XQueryTree(comp.dpy, comp.root, &root_ret, &parent_ret, &children, &nchildren);

	for(uint32_t i = 0; i < nchildren; ++i) {
		add_win(children[i]);
	}

	if(children) {
		XFree(children);
	}
}

// [=]===^=[ run ]===========================================[=]
static void run(void) {
	comp.running = 1;

	while(comp.running) {
		while(XPending(comp.dpy)) {
			XEvent ev;
			XNextEvent(comp.dpy, &ev);
			handle_event(&ev);
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

	if(comp.bg_program) {
		glDeleteProgram(comp.bg_program);
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

	XCompositeUnredirectSubwindows(comp.dpy, comp.root, CompositeRedirectManual);
	XSync(comp.dpy, False);
	XCloseDisplay(comp.dpy);
}

// [=]===^=[ main ]==========================================[=]
int main(int32_t argc, char **argv) {
	comp.bg_color[0] = 1.0f;
	comp.bg_color[1] = 1.0f;
	comp.bg_color[2] = 1.0f;
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

	XSelectInput(comp.dpy, comp.root, SubstructureNotifyMask);
	XSetErrorHandler(runtime_error_handler);

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	register_windows();
	run();
	cleanup();

	return 0;
}
