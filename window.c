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

// [=]===^=[ check_wm_type ]=================================[=]
static uint8_t check_wm_type(Window id) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	XGetWindowProperty(comp.dpy, id, comp.atom_wm_type,
		0, 32, False, XA_ATOM, &type, &format, &nitems, &bytes_after, &data);
	if(data && nitems > 0 && format == 32) {
		Atom *atoms = (Atom *)data;
		for(unsigned long i = 0; i < nitems; ++i) {
			if(atoms[i] == comp.atom_type_dock || atoms[i] == comp.atom_type_desktop) {
				XFree(data);
				return 1;
			}
		}
	}
	if(data) {
		XFree(data);
	}
	return 0;
}

// [=]===^=[ check_skip_effects ]============================[=]
static uint8_t check_skip_effects(Window id) {
	if(check_wm_type(id)) {
		return 1;
	}

	Window root_ret, parent;
	Window *children = NULL;
	uint32_t nchildren = 0;
	if(XQueryTree(comp.dpy, id, &root_ret, &parent, &children, &nchildren)) {
		for(uint32_t i = 0; i < nchildren; ++i) {
			if(check_wm_type(children[i])) {
				XFree(children);
				return 1;
			}
		}
	}
	if(children) {
		XFree(children);
	}

	return 0;
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
	w->no_effects = check_skip_effects(id);

	w->damage = XDamageCreate(comp.dpy, id, XDamageReportNonEmpty);
	bind_texture(w);

	++comp.win_count;
}
