#include "cache.h"
#include "color.h"

enum color_ls {
	LS_LC,			/* left, unused */
	LS_RC,			/* right, unused */
	LS_EC,			/* end color, unused */
	LS_RS,			/* reset */
	LS_NO,			/* normal */
	LS_FL,			/* file, default */
	LS_DI,			/* directory */
	LS_LN,			/* symlink */

	LS_PI,			/* pipe */
	LS_SO,			/* socket */
	LS_BD,			/* block device */
	LS_CD,			/* char device */
	LS_MI,			/* missing file */
	LS_OR,			/* orphaned symlink */
	LS_EX,			/* executable */
	LS_DO,			/* Solaris door */

	LS_SU,			/* setuid */
	LS_SG,			/* setgid */
	LS_ST,			/* sticky */
	LS_OW,			/* other-writable */
	LS_TW,			/* ow with sticky */
	LS_CA,			/* cap */
	LS_MH,			/* multi hardlink */
	LS_CL,			/* clear end of line */

	LS_SUBMODULE,

	MAX_LS
};

static char ls_colors[MAX_LS][COLOR_MAXLEN] = {
	"",
	"",
	"",
	GIT_COLOR_RESET,
	GIT_COLOR_NORMAL,
	GIT_COLOR_NORMAL,
	GIT_COLOR_BOLD_BLUE,
	GIT_COLOR_BOLD_CYAN,

	GIT_COLOR_YELLOW,
	GIT_COLOR_BOLD_MAGENTA,
	GIT_COLOR_BOLD_YELLOW,
	GIT_COLOR_BOLD_YELLOW,
	GIT_COLOR_NORMAL,
	GIT_COLOR_NORMAL,
	GIT_COLOR_BOLD_GREEN,
	GIT_COLOR_BOLD_MAGENTA,

	GIT_COLOR_WHITE_ON_RED,
	GIT_COLOR_BLACK_ON_YELLOW,
	GIT_COLOR_WHITE_ON_BLUE,
	GIT_COLOR_BLUE_ON_GREEN,
	GIT_COLOR_BLACK_ON_GREEN,
	"",
	"",
	"",
	GIT_COLOR_BOLD_BLUE
};

static const char *const indicator_name[] = {
	"lc", "rc", "ec", "rs", "no", "fi", "di", "ln",
	"pi", "so", "bd", "cd", "mi", "or", "ex", "do",
	"su", "sg", "st", "ow", "tw", "ca", "mh", "cl",
	NULL
};

static const char * const config_name[] = {
	"", "", "", "", "normal", "file", "directory", "symlink",
	"fifo", "socket", "block", "char", "missing", "orphan", "executable",
	"door", "setuid", "setgid", "sticky", "otherwritable",
	"stickyotherwritable", "cap", "multihardlink", "",
	"submodule",
	NULL
};

struct bin_str {
	size_t len;			/* Number of bytes */
	const char *string;		/* Pointer to the same */
};

struct color_ext_type {
	struct bin_str ext;		/* The extension we're looking for */
	struct bin_str seq;		/* The sequence to output when we do */
	struct color_ext_type *next;	/* Next in list */
};

static struct color_ext_type *color_ext_list;

/*
 * When true, in a color listing, color each symlink name according to the
 * type of file it points to.  Otherwise, color them according to the `ln'
 * directive in LS_COLORS.  Dangling (orphan) symlinks are treated specially,
 * regardless.  This is set when `ln=target' appears in LS_COLORS.
 */
static int color_symlink_as_referent;

/*
 * Parse a string as part of the LS_COLORS variable; this may involve
 * decoding all kinds of escape characters.  If equals_end is set an
 * unescaped equal sign ends the string, otherwise only a : or \0
 * does.  Set *OUTPUT_COUNT to the number of bytes output.  Return
 * true if successful.
 *
 * The resulting string is *not* null-terminated, but may contain
 * embedded nulls.
 *
 * Note that both dest and src are char **; on return they point to
 * the first free byte after the array and the character that ended
 * the input string, respectively.
 */
static int get_funky_string(char **dest, const char **src, int equals_end,
			    size_t *output_count)
{
	char num;			/* For numerical codes */
	size_t count;			/* Something to count with */
	enum {
		ST_GND, ST_BACKSLASH, ST_OCTAL, ST_HEX,
		ST_CARET, ST_END, ST_ERROR
	} state;
	const char *p;
	char *q;

	p = *src;			/* We don't want to double-indirect */
	q = *dest;			/* the whole darn time.  */

	count = 0;			/* No characters counted in yet.  */
	num = 0;

	state = ST_GND;		/* Start in ground state.  */
	while (state < ST_END) {
		switch (state) {
		case ST_GND:		/* Ground state (no escapes) */
			switch (*p) {
			case ':':
			case '\0':
				state = ST_END;	/* End of string */
				break;
			case '\\':
				state = ST_BACKSLASH; /* Backslash scape sequence */
				++p;
				break;
			case '^':
				state = ST_CARET; /* Caret escape */
				++p;
				break;
			case '=':
				if (equals_end) {
					state = ST_END; /* End */
					break;
				}
				/* else fall through */
			default:
				*(q++) = *(p++);
				++count;
				break;
			}
			break;

		case ST_BACKSLASH:	/* Backslash escaped character */
			switch (*p) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				state = ST_OCTAL;	/* Octal sequence */
				num = *p - '0';
				break;
			case 'x':
			case 'X':
				state = ST_HEX;	/* Hex sequence */
				num = 0;
				break;
			case 'a':		/* Bell */
				num = '\a';
				break;
			case 'b':		/* Backspace */
				num = '\b';
				break;
			case 'e':		/* Escape */
				num = 27;
				break;
			case 'f':		/* Form feed */
				num = '\f';
				break;
			case 'n':		/* Newline */
				num = '\n';
				break;
			case 'r':		/* Carriage return */
				num = '\r';
				break;
			case 't':		/* Tab */
				num = '\t';
				break;
			case 'v':		/* Vtab */
				num = '\v';
				break;
			case '?':		/* Delete */
				num = 127;
				break;
			case '_':		/* Space */
				num = ' ';
				break;
			case '\0':		/* End of string */
				state = ST_ERROR;	/* Error! */
				break;
			default:		/* Escaped character like \ ^ : = */
				num = *p;
				break;
			}
			if (state == ST_BACKSLASH) {
				*(q++) = num;
				++count;
				state = ST_GND;
			}
			++p;
			break;

		case ST_OCTAL:		/* Octal sequence */
			if (*p < '0' || *p > '7') {
				*(q++) = num;
				++count;
				state = ST_GND;
			} else
				num = (num << 3) + (*(p++) - '0');
			break;

		case ST_HEX:		/* Hex sequence */
			switch (*p) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				num = (num << 4) + (*(p++) - '0');
				break;
			case 'a':
			case 'b':
			case 'c':
			case 'd':
			case 'e':
			case 'f':
				num = (num << 4) + (*(p++) - 'a') + 10;
				break;
			case 'A':
			case 'B':
			case 'C':
			case 'D':
			case 'E':
			case 'F':
				num = (num << 4) + (*(p++) - 'A') + 10;
				break;
			default:
				*(q++) = num;
				++count;
				state = ST_GND;
				break;
			}
			break;

		case ST_CARET:		/* Caret escape */
			state = ST_GND;	/* Should be the next state... */
			if (*p >= '@' && *p <= '~') {
				*(q++) = *(p++) & 037;
				++count;
			} else if (*p == '?') {
				*(q++) = 127;
				++count;
			} else
				state = ST_ERROR;
			break;

		default:
			abort();
		}
	}

	*dest = q;
	*src = p;
	*output_count = count;

	return state != ST_ERROR;
}

static int ls_colors_config(const char *var, const char *value, void *cb)
{
	int slot;
	if (!starts_with(var, "color.ls."))
		return 0;
	var += 9;
	for (slot = 0; config_name[slot]; slot++)
		if (!strcasecmp(var, config_name[slot]))
			break;
	if (!config_name[slot])
		return 0;
	if (!value)
		return config_error_nonbool(var);
	color_parse(value, ls_colors[slot]);
	return 0;
}

void parse_ls_color(void)
{
	const char *p;			/* Pointer to character being parsed */
	char *buf;			/* color_buf buffer pointer */
	int state;			/* State of parser */
	int ind_no;			/* Indicator number */
	char label[3];			/* Indicator label */
	struct color_ext_type *ext;	/* Extension we are working on */
	static char *color_buf;
	char *start;
	size_t len;

	if ((p = getenv("LS_COLORS")) == NULL || *p == '\0') {
		git_config(ls_colors_config, NULL);
		return;
	}

	ext = NULL;
	strcpy(label, "??");

	/*
	 * This is an overly conservative estimate, but any possible
	 * LS_COLORS string will *not* generate a color_buf longer
	 * than itself, so it is a safe way of allocating a buffer in
	 * advance.
	 */
	buf = color_buf = xstrdup(p);

	state = 1;
	while (state > 0) {
		switch (state) {
		case 1:		/* First label character */
			switch (*p) {
			case ':':
				++p;
				break;

			case '*':
				/*
				 * Allocate new extension block and add to head of
				 * linked list (this way a later definition will
				 * override an earlier one, which can be useful for
				 * having terminal-specific defs override global).
				 */

				ext = xmalloc(sizeof(*ext));
				ext->next = color_ext_list;
				color_ext_list = ext;

				++p;
				ext->ext.string = buf;

				state = (get_funky_string(&buf, &p, 1, &ext->ext.len)
					 ? 4 : -1);
				break;

			case '\0':
				state = 0;	/* Done! */
				break;

			default:	/* Assume it is file type label */
				label[0] = *(p++);
				state = 2;
				break;
			}
			break;

		case 2:		/* Second label character */
			if (*p) {
				label[1] = *(p++);
				state = 3;
			} else
				state = -1;	/* Error */
			break;

		case 3:		/* Equal sign after indicator label */
			state = -1;	/* Assume failure...  */
			if (*(p++) != '=')
				break;
			for (ind_no = 0; indicator_name[ind_no] != NULL; ++ind_no) {
				if (!strcmp(label, indicator_name[ind_no])) {
					start = buf;
					if (get_funky_string(&buf, &p, 0, &len))
						state = 1;
					else
						state = -1;
					break;
				}
			}
			if (state == -1)
				error(_("unrecognized prefix: %s"), label);
			else if (ind_no == LS_LN && len == 6 &&
				 starts_with(start, "target"))
				color_symlink_as_referent = 1;
			else
				sprintf(ls_colors[ind_no], "\033[%.*sm",
				       (int)len, start);
			break;

		case 4:		/* Equal sign after *.ext */
			if (*(p++) == '=') {
				ext->seq.string = buf;
				state = (get_funky_string(&buf, &p, 0, &ext->seq.len)
					 ? 1 : -1);
			} else
				state = -1;
			break;
		}
	}

	if (!strcmp(ls_colors[LS_LN], "target"))
		color_symlink_as_referent = 1;
	git_config(ls_colors_config, NULL);
}

void color_filename(struct strbuf *sb, const char *name,
		    const char *display_name, mode_t mode, int linkok)
{
	int type;
	struct color_ext_type *ext;	/* Color extension */

	if (S_ISREG(mode)) {
		type = LS_FL;
		if ((mode & S_ISUID) != 0)
			type = LS_SU;
		else if ((mode & S_ISGID) != 0)
			type = LS_SG;
		else if ((mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)
			type = LS_EX;
	} else if (S_ISDIR(mode)) {
		if ((mode & S_ISVTX) && (mode & S_IWOTH))
			type = LS_TW;
		else if ((mode & S_IWOTH) != 0)
			type = LS_OW;
		else if ((mode & S_ISVTX) != 0)
			type = LS_ST;
		else
			type = LS_DI;
	} else if (S_ISLNK(mode))
		type = (!linkok && *ls_colors[LS_OR]) ? LS_OR : LS_LN;
	else if (S_ISGITLINK(mode))
		type = LS_SUBMODULE;
	else if (S_ISFIFO(mode))
		type = LS_PI;
	else if (S_ISSOCK(mode))
		type = LS_SO;
	else if (S_ISBLK(mode))
		type = LS_BD;
	else if (S_ISCHR(mode))
		type = LS_CD;
#ifdef S_ISDOOR
	else if (S_ISDOOR(mode))
		type = LS_DO;
#endif
	else
		/* Classify a file of some other type as C_ORPHAN.  */
		type = LS_OR;

	/* Check the file's suffix only if still classified as C_FILE.  */
	ext = NULL;
	if (type == LS_FL) {
		/* Test if NAME has a recognized suffix.  */
		size_t len = strlen(name);
		const char *p = name + len;		/* Pointer to final \0.  */
		for (ext = color_ext_list; ext != NULL; ext = ext->next) {
			if (ext->ext.len <= len &&
			    !strncmp(p - ext->ext.len, ext->ext.string, ext->ext.len))
				break;
		}
	}

	if (display_name)
		name = display_name;
	if (ext)
		strbuf_addf(sb, "\033[%.*sm%s%s",
			    (int)ext->seq.len, ext->seq.string,
			    name, GIT_COLOR_RESET);
	else if (*ls_colors[type])
		strbuf_addf(sb, "%s%s%s", ls_colors[type], name, GIT_COLOR_RESET);
	else
		strbuf_addstr(sb, name);
}
