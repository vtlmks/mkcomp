// [=]===^=[ load_config ]===================================[=]
static void load_config(void) {
	comp.bg_color[0] = 1.0f;
	comp.bg_color[1] = 1.0f;
	comp.bg_color[2] = 1.0f;
	comp.bg_intensity = 0.15f;
	comp.bg_speed = 1.0f;
	comp.shadow_radius = 20.0f;
	comp.shadow_opacity = 0.6f;
	comp.shadow_offset_x = 5.0f;
	comp.shadow_offset_y = 5.0f;
	comp.border_color[0] = 0.4f;
	comp.border_color[1] = 0.7f;
	comp.border_color[2] = 1.0f;
	comp.border_width = 3.0f;
	comp.corner_radius = 12.0f;
	comp.dim_inactive = 0.2f;

	char *home = getenv("HOME");
	if(!home) {
		return;
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/.config/mkcomp/config", home);

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

		if(strcmp(key, "bg_color") == 0) {
			sscanf(val, "%f %f %f", &comp.bg_color[0], &comp.bg_color[1], &comp.bg_color[2]);

		} else if(strcmp(key, "bg_intensity") == 0) {
			comp.bg_intensity = strtof(val, NULL);

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

		} else if(strcmp(key, "border_width") == 0) {
			comp.border_width = strtof(val, NULL);

		} else if(strcmp(key, "corner_radius") == 0) {
			comp.corner_radius = strtof(val, NULL);

		} else if(strcmp(key, "dim_inactive") == 0) {
			comp.dim_inactive = strtof(val, NULL);
		}
	}

	fclose(fp);
}
