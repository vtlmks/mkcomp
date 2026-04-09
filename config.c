// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

// [=]===^=[ load_config ]===================================[=]
static void load_config(void) {
	comp.bg_color[0] = 1.0f;
	comp.bg_color[1] = 1.0f;
	comp.bg_color[2] = 1.0f;
	comp.bg_intensity = 0.0f;
	comp.bg_speed = 1.0f;
	comp.bg_color2[0] = 0.2f;
	comp.bg_color2[1] = 0.1f;
	comp.bg_color2[2] = 0.05f;
	comp.bg_shader_type = 0;
	comp.shadow_radius = 20.0f;
	comp.shadow_opacity = 0.6f;
	comp.shadow_offset_x = 5.0f;
	comp.shadow_offset_y = 5.0f;
	comp.border_color[0] = 0.4f;
	comp.border_color[1] = 0.7f;
	comp.border_color[2] = 1.0f;
	comp.urgent_border_color[0] = 0.8f;
	comp.urgent_border_color[1] = 0.35f;
	comp.urgent_border_color[2] = 0.55f;
	comp.border_width = 3.0f;
	comp.corner_radius = 12.0f;
	comp.default_opacity = 1.0f;
	comp.inactive_brightness = 1.0f;
	comp.focus_transition_ms = 0;
	comp.fade_in_ms = 0;
	comp.fade_out_ms = 0;
	comp.blur_strength = 0;
	comp.blur_spread = 1.0f;
	comp.rule_count = 0;

	char *home = getenv("HOME");
	if(!home) {
		return;
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/.config/mkcomp/mkcomp.conf", home);

	FILE *fp = fopen(path, "r");
	if(!fp) {
		return;
	}

	char line[256];
	while(fgets(line, sizeof(line), fp)) {
		char *p = line;
		while(*p == ' ' || *p == '\t') {
			++p;
		}
		if(*p == '#' || *p == '\n' || *p == '\0') {
			continue;
		}

		char *eq = strchr(p, '=');
		if(!eq) {
			continue;
		}

		*eq = '\0';
		char *key = p;
		char *val = eq + 1;

		char *ke = eq - 1;
		while(ke > key && (*ke == ' ' || *ke == '\t')) {
			--ke;
		}
		*(ke + 1) = '\0';

		while(*val == ' ' || *val == '\t') {
			++val;
		}
		char *ve = val + strlen(val) - 1;
		while(ve > val && (*ve == ' ' || *ve == '\t' || *ve == '\n' || *ve == '\r')) {
			--ve;
		}
		*(ve + 1) = '\0';

		if(strcmp(key, "bg_color") == 0) {
			sscanf(val, "%f %f %f", &comp.bg_color[0], &comp.bg_color[1], &comp.bg_color[2]);

		} else if(strcmp(key, "bg_intensity") == 0) {
			comp.bg_intensity = strtof(val, NULL);

		} else if(strcmp(key, "bg_shader") == 0) {
			if(strcmp(val, "warp") == 0) {
				comp.bg_shader_type = 1;
			} else {
				comp.bg_shader_type = 0;
			}

		} else if(strcmp(key, "bg_color2") == 0) {
			sscanf(val, "%f %f %f", &comp.bg_color2[0], &comp.bg_color2[1], &comp.bg_color2[2]);

		} else if(strcmp(key, "bg_speed") == 0) {
			comp.bg_speed = strtof(val, NULL);

		} else if(strcmp(key, "shadow_radius") == 0) {
			comp.shadow_radius = strtof(val, NULL);

		} else if(strcmp(key, "shadow_opacity") == 0) {
			comp.shadow_opacity = strtof(val, NULL);

		} else if(strcmp(key, "shadow_offset_x") == 0) {
			comp.shadow_offset_x = strtof(val, NULL);

		} else if(strcmp(key, "shadow_offset_y") == 0) {
			comp.shadow_offset_y = strtof(val, NULL);

		} else if(strcmp(key, "border_color") == 0) {
			sscanf(val, "%f %f %f", &comp.border_color[0], &comp.border_color[1], &comp.border_color[2]);

		} else if(strcmp(key, "urgent_border_color") == 0) {
			sscanf(val, "%f %f %f", &comp.urgent_border_color[0], &comp.urgent_border_color[1], &comp.urgent_border_color[2]);

		} else if(strcmp(key, "border_width") == 0) {
			comp.border_width = strtof(val, NULL);

		} else if(strcmp(key, "corner_radius") == 0) {
			comp.corner_radius = strtof(val, NULL);

		} else if(strcmp(key, "opacity") == 0) {
			comp.default_opacity = strtof(val, NULL);

		} else if(strcmp(key, "inactive_brightness") == 0) {
			comp.inactive_brightness = strtof(val, NULL);

		} else if(strcmp(key, "focus_transition_ms") == 0) {
			comp.focus_transition_ms = (uint32_t)strtoul(val, NULL, 10);

		} else if(strcmp(key, "fade_in_ms") == 0) {
			comp.fade_in_ms = (uint32_t)strtoul(val, NULL, 10);

		} else if(strcmp(key, "fade_out_ms") == 0) {
			comp.fade_out_ms = (uint32_t)strtoul(val, NULL, 10);

		} else if(strcmp(key, "blur_strength") == 0) {
			comp.blur_strength = (uint32_t)strtoul(val, NULL, 10);
			if(comp.blur_strength > BLUR_MAX_LEVELS - 1) {
				comp.blur_strength = BLUR_MAX_LEVELS - 1;
			}

		} else if(strcmp(key, "blur_spread") == 0) {
			comp.blur_spread = strtof(val, NULL);
			if(comp.blur_spread < 0.5f) {
				comp.blur_spread = 0.5f;
			}
			if(comp.blur_spread > 10.0f) {
				comp.blur_spread = 10.0f;
			}

		} else if(strcmp(key, "blur_desaturate") == 0) {
			comp.blur_desaturate = strtof(val, NULL);
			if(comp.blur_desaturate < 0.0f) {
				comp.blur_desaturate = 0.0f;
			}
			if(comp.blur_desaturate > 1.0f) {
				comp.blur_desaturate = 1.0f;
			}

		} else if(strcmp(key, "blur_darken") == 0) {
			comp.blur_darken = strtof(val, NULL);
			if(comp.blur_darken < 0.0f) {
				comp.blur_darken = 0.0f;
			}
			if(comp.blur_darken > 1.0f) {
				comp.blur_darken = 1.0f;
			}

		} else if(strcmp(key, "rule") == 0) {
			if(comp.rule_count >= MAX_RULES) {
				continue;
			}
			if(strncmp(val, "class:", 6) != 0) {
				continue;
			}
			val += 6;
			char *space = strchr(val, ' ');
			if(!space) {
				continue;
			}
			struct rule *r = &comp.rules[comp.rule_count];
			r->opacity = -1.0f;
			r->corner_radius = -1.0f;
			r->shadow = -1;
			r->blur = -1;
			r->border = -1;
			size_t class_len = (size_t)(space - val);
			if(class_len >= sizeof(r->wm_class)) {
				class_len = sizeof(r->wm_class) - 1;
			}
			memcpy(r->wm_class, val, class_len);
			r->wm_class[class_len] = '\0';
			val = space + 1;
			while(*val) {
				while(*val == ' ' || *val == '\t') {
					++val;
				}
				if(!*val) {
					break;
				}
				if(strncmp(val, "opacity=", 8) == 0) {
					r->opacity = strtof(val + 8, &val);

				} else if(strncmp(val, "shadow=", 7) == 0) {
					val += 7;
					if(strncmp(val, "off", 3) == 0) {
						r->shadow = 0;
						val += 3;

					} else if(strncmp(val, "on", 2) == 0) {
						r->shadow = 1;
						val += 2;
					}

				} else if(strncmp(val, "corner_radius=", 14) == 0) {
					r->corner_radius = strtof(val + 14, &val);

				} else if(strncmp(val, "blur=", 5) == 0) {
					val += 5;
					if(strncmp(val, "off", 3) == 0) {
						r->blur = 0;
						val += 3;

					} else if(strncmp(val, "on", 2) == 0) {
						r->blur = 1;
						val += 2;
					}

				} else if(strncmp(val, "border=", 7) == 0) {
					val += 7;
					if(strncmp(val, "off", 3) == 0) {
						r->border = 0;
						val += 3;

					} else if(strncmp(val, "on", 2) == 0) {
						r->border = 1;
						val += 2;
					}

				} else {
					while(*val && *val != ' ' && *val != '\t') {
						++val;
					}
				}
			}
			++comp.rule_count;
		}
	}

	fclose(fp);
}
