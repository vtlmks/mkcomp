// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

// [=]===^=[ get_time_us ]===================================[=]
static uint64_t get_time_us(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// [=]===^=[ render ]========================================[=]
static void render(void) {
	if(comp.fullscreen_win) {
		return;
	}

	uint64_t now = get_time_us();
	float dt = comp.last_render_us ? (float)(now - comp.last_render_us) / 1000000.0f : 0.0f;
	comp.last_render_us = now;

	for(uint32_t i = 0; i < comp.win_count; ++i) {
		struct win *w = &comp.wins[i];
		if(!w->mapped && !w->fading_out) {
			continue;
		}
		if(w->needs_rebind) {
			bind_texture(w);
			w->needs_rebind = 0;
		}
		w->damaged = 0;
	}

	glXWaitX();

	if(comp.bg_prog) {
		float t = (float)(get_time_us() - comp.start_us) / 1000000.0f;
		glUseProgram(comp.bg_prog);
		glUniform1f(comp.bg_time_loc, t);
		glUniform2f(comp.bg_resolution_loc, (float)comp.root_w, (float)comp.root_h);
		glUniform3f(comp.bg_color_loc, comp.bg_color[0], comp.bg_color[1], comp.bg_color[2]);
		glUniform1f(comp.bg_intensity_loc, comp.bg_intensity);
		glUniform1f(comp.bg_speed_loc, comp.bg_speed);

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
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
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

	for(uint32_t i = 0; i < nchildren; ++i) {
		struct win *w = find_win(children[i]);
		if(!w || (!w->mapped && !w->fading_out) || !w->tex) {
			continue;
		}

		// fade animation
		if(w->fading_out) {
			if(comp.fade_out_ms > 0 && dt > 0.0f) {
				w->fade -= dt / ((float)comp.fade_out_ms / 1000.0f);
				if(w->fade <= 0.0f) {
					w->fade = 0.0f;
					w->mapped = 0;
					w->fading_out = 0;
					continue;
				}
			} else {
				w->fade = 0.0f;
				w->mapped = 0;
				w->fading_out = 0;
				continue;
			}
			comp.dirty = 1;

		} else if(w->fade < 1.0f) {
			if(comp.fade_in_ms > 0 && dt > 0.0f) {
				w->fade += dt / ((float)comp.fade_in_ms / 1000.0f);
				if(w->fade > 1.0f) {
					w->fade = 1.0f;
				}
			} else {
				w->fade = 1.0f;
			}
			if(w->fade < 1.0f) {
				comp.dirty = 1;
			}
		}

		float wx = (float)w->x;
		float wy = (float)w->y;
		float ww = (float)w->w;
		float wh = (float)w->h;
		uint8_t active = (w->id == comp.active_win);

		float effective_opacity = w->fade * (w->rule_opacity >= 0.0f ? w->rule_opacity : w->opacity);
		float radius = w->no_effects ? 0.0f : (w->rule_corner_radius >= 0.0f ? w->rule_corner_radius : comp.corner_radius);
		uint8_t show_shadow = !w->no_effects && (w->rule_shadow < 0 || w->rule_shadow == 1);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		if(comp.shadow_prog && comp.shadow_radius > 0.0f && show_shadow) {
			float ox = comp.shadow_offset_x;
			float oy = comp.shadow_offset_y;
			float pad = comp.shadow_radius * 2.0f;

			glUseProgram(comp.shadow_prog);
			glUniform2f(comp.shadow_pos_loc, wx + ox, wy + oy);
			glUniform2f(comp.shadow_size_loc, ww, wh);
			glUniform1f(comp.shadow_radius_loc, radius);
			glUniform1f(comp.shadow_sigma_loc, comp.shadow_radius * 0.4f);
			glUniform1f(comp.shadow_opacity_loc, comp.shadow_opacity * effective_opacity);

			glBegin(GL_QUADS);
			glVertex2f(wx + ox - pad, wy + oy - pad);
			glVertex2f(wx + ww + ox + pad, wy + oy - pad);
			glVertex2f(wx + ww + ox + pad, wy + wh + oy + pad);
			glVertex2f(wx + ox - pad, wy + wh + oy + pad);
			glEnd();
		}

		if(comp.border_prog && comp.border_width > 0.0f && (active || w->urgent) && !w->no_effects) {
			float bw = comp.border_width;
			float *bc = w->urgent ? comp.urgent_border_color : comp.border_color;

			glUseProgram(comp.border_prog);
			glUniform2f(comp.border_pos_loc, wx - bw, wy - bw);
			glUniform2f(comp.border_size_loc, ww + 2.0f * bw, wh + 2.0f * bw);
			glUniform1f(comp.border_radius_loc, radius + bw);
			glUniform1f(comp.border_width_loc, bw);
			glUniform3f(comp.border_color_loc, bc[0], bc[1], bc[2]);
			glUniform1f(comp.border_opacity_loc, effective_opacity);

			glBegin(GL_QUADS);
			glVertex2f(wx - bw, wy - bw);
			glVertex2f(wx + ww + bw, wy - bw);
			glVertex2f(wx + ww + bw, wy + wh + bw);
			glVertex2f(wx - bw, wy + wh + bw);
			glEnd();
		}

		float target_dim = (active || w->no_effects) ? 1.0f : comp.inactive_brightness;
		if(comp.focus_transition_ms > 0 && dt > 0.0f && comp.inactive_brightness < 1.0f) {
			float range = 1.0f - comp.inactive_brightness;
			float rate = range / ((float)comp.focus_transition_ms / 1000.0f);
			float step = rate * dt;
			if(w->dim_current < target_dim) {
				w->dim_current += step;
				if(w->dim_current > target_dim) {
					w->dim_current = target_dim;
				}

			} else if(w->dim_current > target_dim) {
				w->dim_current -= step;
				if(w->dim_current < target_dim) {
					w->dim_current = target_dim;
				}
			}

		} else {
			w->dim_current = target_dim;
		}
		if(w->dim_current != target_dim) {
			comp.dirty = 1;
		}

		glUseProgram(comp.win_prog);
		if(comp.win_prog) {
			glUniform2f(comp.win_pos_loc, wx, wy);
			glUniform2f(comp.win_size_loc, ww, wh);
			glUniform1f(comp.win_radius_loc, radius);
			glUniform1f(comp.win_dim_loc, w->dim_current);
			glUniform1f(comp.win_opacity_loc, effective_opacity);
		}

		uint8_t needs_blend = (w->depth == 32 || radius > 0.0f || effective_opacity < 1.0f);
		if(!needs_blend) {
			glDisable(GL_BLEND);
		}

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, w->tex);

		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f);
		glVertex2f(wx, wy);
		glTexCoord2f(1.0f, 0.0f);
		glVertex2f(wx + ww, wy);
		glTexCoord2f(1.0f, 1.0f);
		glVertex2f(wx + ww, wy + wh);
		glTexCoord2f(0.0f, 1.0f);
		glVertex2f(wx, wy + wh);
		glEnd();

		glDisable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);
	}

	glUseProgram(0);

	if(children) {
		XFree(children);
	}

	glXSwapBuffers(comp.dpy, comp.gl_win);
}
