// Copyright (c) 2026 vital
// SPDX-License-Identifier: MIT

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
	"uniform float u_intensity;\n"
	"uniform float u_speed;\n"
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
	"	float t = u_time * u_speed;\n"
	"	float n = 0.0;\n"
	"	n += 0.50  * noise(uv * 2.0 + vec2(t * 0.04,  t * 0.03));\n"
	"	n += 0.25  * noise(uv * 4.0 - vec2(t * 0.03, t * 0.04));\n"
	"	n += 0.125 * noise(uv * 8.0 + vec2(t * 0.02,  t * -0.03));\n"
	"	n = clamp(n * 0.7 + 0.5, 0.0, 1.0);\n"
	"	n *= u_intensity;\n"
	"	float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;\n"
	"	n += dither;\n"
	"	gl_FragColor = vec4(n * u_color, 1.0);\n"
	"}\n";

static char win_vert_src[] =
	"#version 120\n"
	"varying vec2 v_texcoord;\n"
	"varying vec2 v_pos;\n"
	"void main() {\n"
	"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"	v_texcoord = gl_MultiTexCoord0.xy;\n"
	"	v_pos = gl_Vertex.xy;\n"
	"}\n";

static char win_frag_src[] =
	"#version 120\n"
	"uniform sampler2D u_tex;\n"
	"uniform vec2 u_pos;\n"
	"uniform vec2 u_size;\n"
	"uniform float u_radius;\n"
	"uniform float u_dim;\n"
"uniform float u_opacity;\n"
	"varying vec2 v_texcoord;\n"
	"varying vec2 v_pos;\n"
	"\n"
	"float rounded_rect(vec2 p, vec2 half_size, float r) {\n"
	"	vec2 q = abs(p) - half_size + vec2(r);\n"
	"	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
	"}\n"
	"\n"
	"void main() {\n"
	"	vec4 color = texture2D(u_tex, v_texcoord);\n"
	"	vec2 half_size = u_size * 0.5;\n"
	"	float r = min(u_radius, min(half_size.x, half_size.y));\n"
	"	vec2 center = u_pos + half_size;\n"
	"	float d = rounded_rect(v_pos - center, half_size, r);\n"
	"	if(d > 0.5) discard;\n"
	"	float aa = 1.0 - smoothstep(-1.0, 0.5, d);\n"
	"	color *= aa;\n"
	"	color.rgb *= u_dim;\n"
	"	color *= u_opacity;\n"
	"	gl_FragColor = color;\n"
	"}\n";

static char shadow_frag_src[] =
	"#version 120\n"
	"uniform vec2 u_pos;\n"
	"uniform vec2 u_size;\n"
	"uniform float u_radius;\n"
	"uniform float u_sigma;\n"
	"uniform float u_opacity;\n"
	"varying vec2 v_pos;\n"
	"\n"
	"float rounded_rect(vec2 p, vec2 half_size, float r) {\n"
	"	vec2 q = abs(p) - half_size + vec2(r);\n"
	"	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
	"}\n"
	"\n"
	"void main() {\n"
	"	vec2 half_size = u_size * 0.5;\n"
	"	float r = min(u_radius, min(half_size.x, half_size.y));\n"
	"	vec2 center = u_pos + half_size;\n"
	"	float d = max(rounded_rect(v_pos - center, half_size, r), 0.0);\n"
	"	float a = u_opacity * exp(-d * d / (2.0 * u_sigma * u_sigma));\n"
	"	if(a < 0.004) discard;\n"
	"	gl_FragColor = vec4(0.0, 0.0, 0.0, a);\n"
	"}\n";

static char border_frag_src[] =
	"#version 120\n"
	"uniform vec2 u_pos;\n"
	"uniform vec2 u_size;\n"
	"uniform float u_radius;\n"
	"uniform float u_width;\n"
	"uniform vec3 u_color;\n"
	"uniform float u_opacity;\n"
	"varying vec2 v_pos;\n"
	"\n"
	"float rounded_rect(vec2 p, vec2 half_size, float r) {\n"
	"	vec2 q = abs(p) - half_size + vec2(r);\n"
	"	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
	"}\n"
	"\n"
	"void main() {\n"
	"	vec2 half_size = u_size * 0.5;\n"
	"	float r = min(u_radius, min(half_size.x, half_size.y));\n"
	"	vec2 center = u_pos + half_size;\n"
	"	float d_outer = rounded_rect(v_pos - center, half_size, r);\n"
	"	if(d_outer > 0.5) discard;\n"
	"	vec2 inner_half = half_size - vec2(u_width) - vec2(1.5);\n"
	"	float ir = max(r - u_width - 1.5, 0.0);\n"
	"	float d_inner = rounded_rect(v_pos - center, inner_half, ir);\n"
	"	if(d_inner < 0.0) discard;\n"
	"	float aa_outer = 1.0 - smoothstep(-1.0, 0.5, d_outer);\n"
	"	float a = aa_outer;\n"
	"	gl_FragColor = vec4(u_color * a, a) * u_opacity;\n"
	"}\n";

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

// [=]===^=[ create_program ]================================[=]
static uint32_t create_program(char *vert_src, char *frag_src) {
	uint32_t vs = compile_shader(GL_VERTEX_SHADER, vert_src);
	uint32_t fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if(!vs || !fs) {
		if(vs) {
			glDeleteShader(vs);
		}
		if(fs) {
			glDeleteShader(fs);
		}
		return 0;
	}

	uint32_t prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);

	int32_t ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if(!ok) {
		char log[512];
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		fprintf(stderr, "mkcomp: link error: %s\n", log);
		glDeleteProgram(prog);
		return 0;
	}

	return prog;
}

// [=]===^=[ init_shaders ]==================================[=]
static void init_shaders(void) {
	comp.bg_prog = create_program(bg_vert_src, bg_frag_src);
	if(comp.bg_prog) {
		comp.bg_time_loc = glGetUniformLocation(comp.bg_prog, "u_time");
		comp.bg_resolution_loc = glGetUniformLocation(comp.bg_prog, "u_resolution");
		comp.bg_color_loc = glGetUniformLocation(comp.bg_prog, "u_color");
		comp.bg_intensity_loc = glGetUniformLocation(comp.bg_prog, "u_intensity");
		comp.bg_speed_loc = glGetUniformLocation(comp.bg_prog, "u_speed");
	}

	comp.win_prog = create_program(win_vert_src, win_frag_src);
	if(comp.win_prog) {
		glUseProgram(comp.win_prog);
		glUniform1i(glGetUniformLocation(comp.win_prog, "u_tex"), 0);
		glUseProgram(0);
		comp.win_pos_loc = glGetUniformLocation(comp.win_prog, "u_pos");
		comp.win_size_loc = glGetUniformLocation(comp.win_prog, "u_size");
		comp.win_radius_loc = glGetUniformLocation(comp.win_prog, "u_radius");
		comp.win_dim_loc = glGetUniformLocation(comp.win_prog, "u_dim");
		comp.win_opacity_loc = glGetUniformLocation(comp.win_prog, "u_opacity");
	}

	comp.shadow_prog = create_program(win_vert_src, shadow_frag_src);
	if(comp.shadow_prog) {
		comp.shadow_pos_loc = glGetUniformLocation(comp.shadow_prog, "u_pos");
		comp.shadow_size_loc = glGetUniformLocation(comp.shadow_prog, "u_size");
		comp.shadow_radius_loc = glGetUniformLocation(comp.shadow_prog, "u_radius");
		comp.shadow_sigma_loc = glGetUniformLocation(comp.shadow_prog, "u_sigma");
		comp.shadow_opacity_loc = glGetUniformLocation(comp.shadow_prog, "u_opacity");
	}

	comp.border_prog = create_program(win_vert_src, border_frag_src);
	if(comp.border_prog) {
		comp.border_pos_loc = glGetUniformLocation(comp.border_prog, "u_pos");
		comp.border_size_loc = glGetUniformLocation(comp.border_prog, "u_size");
		comp.border_radius_loc = glGetUniformLocation(comp.border_prog, "u_radius");
		comp.border_width_loc = glGetUniformLocation(comp.border_prog, "u_width");
		comp.border_color_loc = glGetUniformLocation(comp.border_prog, "u_color");
		comp.border_opacity_loc = glGetUniformLocation(comp.border_prog, "u_opacity");
	}
}
