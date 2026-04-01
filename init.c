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

	init_shaders();

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
