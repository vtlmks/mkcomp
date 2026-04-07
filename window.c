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

// [=]===^=[ check_demands_attention ]======================[=]
static uint8_t check_demands_attention(Window id) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	XGetWindowProperty(comp.dpy, id, comp.atom_wm_state, 0, 32, False, XA_ATOM, &type, &format, &nitems, &bytes_after, &data);
	if(data && nitems > 0 && format == 32) {
		Atom *atoms = (Atom *)data;
		for(unsigned long i = 0; i < nitems; ++i) {
			if(atoms[i] == comp.atom_state_attention) {
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

// [=]===^=[ check_fullscreen_state ]=======================[=]
static uint8_t check_fullscreen_state(Window id) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	XGetWindowProperty(comp.dpy, id, comp.atom_wm_state,
		0, 32, False, XA_ATOM, &type, &format, &nitems, &bytes_after, &data);

	if(data && nitems > 0 && format == 32) {
		Atom *atoms = (Atom *)data;
		for(unsigned long i = 0; i < nitems; ++i) {
			if(atoms[i] == comp.atom_state_fullscreen) {
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

// [=]===^=[ check_fullscreen ]=============================[=]
static uint8_t check_fullscreen(Window id) {
	if(check_fullscreen_state(id)) {
		return 1;
	}

	Window root_ret, parent;
	Window *children = NULL;
	uint32_t nchildren = 0;
	if(XQueryTree(comp.dpy, id, &root_ret, &parent, &children, &nchildren)) {
		for(uint32_t i = 0; i < nchildren; ++i) {
			if(check_fullscreen_state(children[i])) {
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

// [=]===^=[ unredirect_fullscreen ]========================[=]
static void unredirect_fullscreen(struct win *w) {
	unbind_texture(w);
	XCompositeUnredirectWindow(comp.dpy, w->id, CompositeRedirectManual);
	XUnmapWindow(comp.dpy, comp.overlay);
	comp.fullscreen_win = w->id;
	w->fullscreen = 1;
	fprintf(stderr, "mkcomp: fullscreen bypass on 0x%lx\n", w->id);
}

// [=]===^=[ redirect_from_fullscreen ]=====================[=]
static void redirect_from_fullscreen(struct win *w) {
	XCompositeRedirectWindow(comp.dpy, w->id, CompositeRedirectManual);
	XMapWindow(comp.dpy, comp.overlay);
	comp.fullscreen_win = 0;
	w->fullscreen = 0;
	w->needs_rebind = 1;
	fprintf(stderr, "mkcomp: fullscreen bypass off 0x%lx\n", w->id);
}

// [=]===^=[ remove_win ]====================================[=]
static void remove_win(Window id) {
	for(uint32_t i = 0; i < comp.win_count; ++i) {
		if(comp.wins[i].id != id) {
			continue;
		}
		if(comp.wins[i].fullscreen) {
			XMapWindow(comp.dpy, comp.overlay);
			comp.fullscreen_win = 0;
			fprintf(stderr, "mkcomp: fullscreen window removed, restoring compositing\n");
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
			if(atoms[i] == comp.atom_type_dock || atoms[i] == comp.atom_type_desktop || atoms[i] == comp.atom_type_menu || atoms[i] == comp.atom_type_popup_menu || atoms[i] == comp.atom_type_dropdown_menu || atoms[i] == comp.atom_type_tooltip || atoms[i] == comp.atom_type_combo) {
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

	XSelectInput(comp.dpy, id, PropertyChangeMask);

	Window child_root, child_parent;
	Window *child_list = NULL;
	uint32_t child_count = 0;
	if(XQueryTree(comp.dpy, id, &child_root, &child_parent, &child_list, &child_count)) {
		for(uint32_t j = 0; j < child_count; ++j) {
			XSelectInput(comp.dpy, child_list[j], PropertyChangeMask);
		}
	}
	if(child_list) {
		XFree(child_list);
	}

	struct win *w = &comp.wins[comp.win_count];
	memset(w, 0, sizeof(*w));
	w->id = id;
	w->x = wa.x;
	w->y = wa.y;
	w->w = wa.width + 2 * wa.border_width;
	w->h = wa.height + 2 * wa.border_width;
	w->depth = wa.depth;
	w->no_effects = wa.override_redirect || check_skip_effects(id);
	w->urgent = check_demands_attention(id);
	w->dim_current = (id == comp.active_win || w->no_effects) ? 1.0f : (1.0f - comp.dim_inactive);

	w->mapped = 1;
	w->damage = XDamageCreate(comp.dpy, id, XDamageReportNonEmpty);
	bind_texture(w);

	++comp.win_count;

	if(check_fullscreen(id)) {
		unredirect_fullscreen(w);
	}
}
