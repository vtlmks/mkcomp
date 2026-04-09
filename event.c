// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

// [=]===^=[ update_active_window ]=========================[=]
static void update_active_window(void) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	XGetWindowProperty(comp.dpy, comp.root, comp.atom_active_win,
		0, 1, False, XA_WINDOW, &type, &format, &nitems, &bytes_after, &data);

	if(!data) {
		comp.active_win = 0;
		return;
	}

	Window active = 0;
	if(nitems == 1 && format == 32) {
		active = *(Window *)data;
	}
	XFree(data);

	struct win *aw = find_win(active);
	if(aw) {
		comp.active_win = active;
		aw->urgent = 0;
		return;
	}

	Window current = active;
	Window root_ret, parent;
	Window *ch;
	uint32_t nch;

	while(current && current != comp.root) {
		if(!XQueryTree(comp.dpy, current, &root_ret, &parent, &ch, &nch)) {
			break;
		}
		if(ch) {
			XFree(ch);
		}
		if(parent == comp.root) {
			comp.active_win = current;
			struct win *pw = find_win(current);
			if(pw) {
				pw->urgent = 0;
			}
			return;
		}
		current = parent;
	}

	comp.active_win = 0;
}

// [=]===^=[ handle_destroy ]================================[=]
static void handle_destroy(XDestroyWindowEvent *ev) {
	remove_win(ev->window);
}

// [=]===^=[ handle_map ]====================================[=]
static void handle_map(XMapEvent *ev) {
	struct win *w = find_win(ev->window);
	if(w) {
		w->mapped = 1;
		w->fading_out = 0;
		if(comp.fade_in_ms > 0) {
			w->fade = 0.0f;
		}
		unbind_texture(w);
		w->needs_rebind = 1;

	} else {
		add_win(ev->window);
	}
}

// [=]===^=[ handle_unmap ]==================================[=]
static void handle_unmap(XUnmapEvent *ev) {
	struct win *w = find_win(ev->window);
	if(!w) {
		return;
	}
	if(comp.fade_out_ms > 0 && w->fade > 0.0f) {
		w->fading_out = 1;

	} else {
		w->mapped = 0;
	}
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
		w->resize_pending = 1;
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

// [=]===^=[ handle_property ]===============================[=]
static void handle_property(XPropertyEvent *ev) {
	if(ev->window == comp.root && ev->atom == comp.atom_active_win) {
		update_active_window();
		return;
	}

	if(ev->window == comp.root && ev->atom == comp.atom_root_pixmap) {
		bind_root_pixmap();
		return;
	}

	if(ev->atom == comp.atom_wm_state) {
		struct win *w = find_win(ev->window);
		if(!w) {
			Window root_ret, parent;
			Window *ch = NULL;
			uint32_t nch = 0;
			if(XQueryTree(comp.dpy, ev->window, &root_ret, &parent, &ch, &nch)) {
				if(ch) {
					XFree(ch);
				}
				w = find_win(parent);
			}
		}
		if(!w) {
			return;
		}
		uint8_t fs = check_fullscreen(ev->window);
		if(fs && !w->fullscreen) {
			unredirect_fullscreen(w);

		} else if(!fs && w->fullscreen) {
			redirect_from_fullscreen(w);
		}

		w->urgent = check_demands_attention(ev->window);
	}

	if(ev->atom == comp.atom_wm_opacity) {
		struct win *w = find_win(ev->window);
		if(w) {
			w->opacity = read_wm_opacity(ev->window);
		}
	}
}

// [=]===^=[ handle_event ]==================================[=]
static void handle_event(XEvent *ev) {
	if(ev->type == GenericEvent && comp.has_present) {
		XGenericEventCookie *cookie = &ev->xcookie;
		if(cookie->extension == comp.present_opcode && XGetEventData(comp.dpy, cookie)) {
			if(cookie->evtype == PresentCompleteNotify) {
				XPresentCompleteNotifyEvent *ce = cookie->data;
				uint64_t now = get_time_us();
				uint64_t delta = comp.last_vblank_us ? now - comp.last_vblank_us : 0;
				comp.last_vblank_us = now;
				comp.last_msc = ce->msc;
				comp.vblank_pending = 0;
				if(comp.vblank_calibration < 8) {
					if(delta > 500 && delta < 100000) {
						comp.vblank_interval_us += delta;
						++comp.vblank_calibration;
						if(comp.vblank_calibration == 8) {
							comp.vblank_interval_us /= 8;
							fprintf(stderr, "mkcomp: vblank interval %llu us (%.0f Hz)\n", (unsigned long long)comp.vblank_interval_us, 1000000.0 / (double)comp.vblank_interval_us);
						}
					}
					comp.vblank_ready = 1;
				} else if(comp.vblank_stalled) {
					if(delta > comp.vblank_interval_us * 2) {
						// NOTE(peter): probe response after long idle gap; delta is meaningless.
						// schedule a follow-up to measure the real current rate.
						XPresentNotifyMSC(comp.dpy, comp.root, ++comp.present_serial, comp.last_msc + 1, 0, 0);
						comp.vblank_pending = 1;
					} else if(delta >= comp.vblank_interval_us / 2) {
						fprintf(stderr, "mkcomp: display on, resuming render\n");
						comp.vblank_stalled = 0;
						comp.vblank_fast_count = 0;
						comp.dirty = 1;
						comp.vblank_ready = 1;
					}
				} else if(delta && delta < comp.vblank_interval_us / 2) {
					++comp.vblank_fast_count;
					if(comp.vblank_fast_count >= 5) {
						fprintf(stderr, "mkcomp: display off, pausing render\n");
						comp.vblank_stalled = 1;
					}
				} else {
					comp.vblank_fast_count = 0;
					comp.vblank_ready = 1;
				}
			}
			XFreeEventData(comp.dpy, cookie);
		}
		return;
	}

	uint8_t handled = 0;

	if(ev->type == DestroyNotify) {
		handle_destroy(&ev->xdestroywindow);
		handled = 1;

	} else if(ev->type == MapNotify) {
		handle_map(&ev->xmap);
		handled = 1;

	} else if(ev->type == UnmapNotify) {
		handle_unmap(&ev->xunmap);
		handled = 1;

	} else if(ev->type == ReparentNotify) {
		handle_reparent(&ev->xreparent);
		handled = 1;

	} else if(ev->type == ConfigureNotify) {
		handle_configure(&ev->xconfigure);
		handled = 1;

	} else if(ev->type == PropertyNotify) {
		handle_property(&ev->xproperty);
		handled = 1;

	} else if(ev->type == comp.damage_event + XDamageNotify) {
		handle_damage_event((XDamageNotifyEvent *)ev);
		handled = 1;
	}

	if(handled) {
		comp.dirty = 1;
	}
}
