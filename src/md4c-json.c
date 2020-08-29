/*
 * MD4C: Markdown parser for C
 * (http://github.com/mity/md4c)
 *
 * Copyright (c) 2016-2019 Martin Mitas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "md4c-json.h"


#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199409L
    /* C89/90 or old compilers in general may not understand "inline". */
    #if defined __GNUC__
        #define inline __inline__
    #elif defined _MSC_VER
        #define inline __inline
    #else
        #define inline
    #endif
#endif

#ifdef _WIN32
    #define snprintf _snprintf
#endif

typedef char MD_JSON_LEVEL;

typedef struct MD_JSON_tag MD_JSON;
struct MD_JSON_tag {
    void (*process_output)(const MD_CHAR*, MD_SIZE, void*);
    void* userdata;
    unsigned flags;
	char escape_map[256];
	
	// following slots are private
	
	int nesting_level;   // for pretty printing, how deep is the tree (how much of levels is used
	// grows via push_level(r)
	// shrinks via push_level(r)
	int allocated_level; // how big is levels allocated
	MD_JSON_LEVEL* levels; // for rendering objects and arrays, determins of "," is needed.
	// this is a dynamically allocated stack. nesting_level knows how many objects are
};

static void
mark_comma_required(MD_JSON* r)
{
	r->levels[r->nesting_level] = 1;
}

static void
unmark_comma_required(MD_JSON* r)
{
	r->levels[r->nesting_level] = 0;
}

static void
push_level(MD_JSON* r)
{
	if ((r)->nesting_level >= (r)->allocated_level)
	{
		(r)->levels = realloc((r)->levels, (r)->allocated_level * 1.5 * sizeof(MD_JSON_LEVEL));
	}
	(r)->nesting_level ++;
	(r)->levels[(r)->nesting_level] = 0;
}

static void
pull_level(MD_JSON* r)
{
	r->nesting_level --;
}


static inline void
render_verbatim(MD_JSON* r, const MD_CHAR* text, MD_SIZE size)
{
    r->process_output(text, size, r->userdata);
}

/* Keep this as a macro. Most compiler should then be smart enough to replace
 * the strlen() call with a compile-time constant if the string is a C literal. */
#define RENDER_VERBATIM(r, verbatim)                                    \
        render_verbatim((r), (verbatim), (MD_SIZE) (strlen(verbatim)))

/* Macro to render an indent
 *
 */
#define RENDER_INDENT(r)                                \
        if ( (r)->flags & MD_JSON_FLAG_PRETTY_PRINT)    \
        {                                               \
            RENDER_VERBATIM(r, "\n");                    \
            for (int _i = 0; _i < (r)->nesting_level; _i++) \
                RENDER_VERBATIM(r,"\t");                \
        }

static void
render_json_escaped(MD_JSON* r, const MD_CHAR* data, MD_SIZE size)
{
    MD_OFFSET beg = 0;
    MD_OFFSET off = 0;

    /* Some characters need to be escaped in JSON text. */
    #define NEED_JSON_ESC(ch)   (r->escape_map[(unsigned char)(ch)])

    while(1) {
        /* Optimization: Use some loop unrolling. */
        while(off + 3 < size  &&  !NEED_JSON_ESC(data[off+0])  &&  !NEED_JSON_ESC(data[off+1])
                              &&  !NEED_JSON_ESC(data[off+2])  &&  !NEED_JSON_ESC(data[off+3]))
            off += 4;
        while(off < size  &&  !NEED_JSON_ESC(data[off]))
            off++;

        if(off > beg)
            render_verbatim(r, data + beg, off - beg);

		// from json.org the definition of what needs to be escaped
        if(off < size) {
            switch(data[off]) {
				case '"':    RENDER_VERBATIM(r, "\\\""); break;
                case '\\':   RENDER_VERBATIM(r, "\\\\"); break;
                case '/':    RENDER_VERBATIM(r, "\\/"); break;
                case '\b':   RENDER_VERBATIM(r, "\\b"); break;
                case '\f':   RENDER_VERBATIM(r, "\\f"); break;
                case '\n':   RENDER_VERBATIM(r, "\\n"); break;
                case '\r':   RENDER_VERBATIM(r, "\\r"); break;
                case '\t':   RENDER_VERBATIM(r, "\\t"); break;
            }
            off++;
        } else {
            break;
        }
        beg = off;
    }
}

static void
render_utf8_codepoint(MD_JSON* r, unsigned codepoint,
                      void (*fn_append)(MD_JSON*, const MD_CHAR*, MD_SIZE))
{
    static const MD_CHAR utf8_replacement_char[] = { 0xef, 0xbf, 0xbd };

    unsigned char utf8[4];
    MD_SIZE n;

    if(codepoint <= 0x7f) {
        n = 1;
        utf8[0] = codepoint;
    } else if(codepoint <= 0x7ff) {
        n = 2;
        utf8[0] = 0xc0 | ((codepoint >>  6) & 0x1f);
        utf8[1] = 0x80 + ((codepoint >>  0) & 0x3f);
    } else if(codepoint <= 0xffff) {
        n = 3;
        utf8[0] = 0xe0 | ((codepoint >> 12) & 0xf);
        utf8[1] = 0x80 + ((codepoint >>  6) & 0x3f);
        utf8[2] = 0x80 + ((codepoint >>  0) & 0x3f);
    } else {
        n = 4;
        utf8[0] = 0xf0 | ((codepoint >> 18) & 0x7);
        utf8[1] = 0x80 + ((codepoint >> 12) & 0x3f);
        utf8[2] = 0x80 + ((codepoint >>  6) & 0x3f);
        utf8[3] = 0x80 + ((codepoint >>  0) & 0x3f);
    }

    if(0 < codepoint  &&  codepoint <= 0x10ffff)
        fn_append(r, (char*)utf8, n);
    else
        fn_append(r, utf8_replacement_char, 3);
}

static void
render_int_value(MD_JSON* r, int value)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%i", value);
	RENDER_VERBATIM(r, buf);
	mark_comma_required(r);
}

static void
render_string_value_size(MD_JSON* r, const MD_CHAR* value, MD_SIZE size)
{
	RENDER_VERBATIM(r, "\"");
	render_json_escaped(r, value, size);
	RENDER_VERBATIM(r, "\"");
	mark_comma_required(r);
}

static void
render_string_value(MD_JSON* r, const MD_CHAR* value)
{
	render_string_value_size(r, value, (MD_SIZE)strlen(value));
}

static void
render_bool_value(MD_JSON* r, int boolean)
{
	if (boolean)
	{
		RENDER_VERBATIM(r, "true");
	}
	else
	{
		RENDER_VERBATIM(r, "false");
	}
	mark_comma_required(r);
}

static void
render_comma(MD_JSON* r)
{
	if (r->levels[r->nesting_level])
	{
		RENDER_VERBATIM(r, ",");
		RENDER_INDENT(r);
		unmark_comma_required(r);
	}
}

static void
render_association_key(MD_JSON* r, MD_CHAR* key)
{
	render_comma(r);
	render_string_value(r, key);
	RENDER_VERBATIM(r, ": ");
	unmark_comma_required(r);
}

static void
render_children_start(MD_JSON* r)
{
	render_comma(r);
	render_association_key(r, "children");
	RENDER_VERBATIM(r, "[");
	push_level(r);
	RENDER_INDENT(r);
	
}

static void
render_children_end(MD_JSON* r)
{
	pull_level(r);
	RENDER_INDENT(r);
	RENDER_VERBATIM(r, "]");
}

static void
render_object_start(MD_JSON* r, const MD_CHAR* name)
{
	render_comma(r);
	push_level(r);
	RENDER_VERBATIM(r, "{");
	RENDER_INDENT(r);
	render_association_key(r, "name");
	render_string_value(r, name);
}

static void
render_object_end(MD_JSON* r)
{
	pull_level(r);
	RENDER_INDENT(r);
	RENDER_VERBATIM(r, "}");
	mark_comma_required(r);
}

static void
render_attribute(MD_JSON* r, const MD_ATTRIBUTE* attr)
{
    int i;
	render_object_start(r,"attribute");
	{
		render_children_start(r);
		{
			for(i = 0; attr->substr_offsets[i] < attr->size; i++)
			{
				MD_TEXTTYPE type = attr->substr_types[i];
				MD_OFFSET off = attr->substr_offsets[i];
				MD_SIZE size = attr->substr_offsets[i+1] - off;
				const MD_CHAR* text = attr->text + off;
				
				render_association_key(r, "type");
				render_int_value(r, type);
				
				render_association_key(r, "offset");
				render_int_value(r, off);
				
				render_association_key(r, "size");
				render_int_value(r, size);
				
				render_association_key(r, "text");
				render_string_value_size(r, text, size);
			}
		}
		render_children_end(r);
	}
	render_object_end(r);
}
static void
render_open_ol_block(MD_JSON* r, const MD_BLOCK_OL_DETAIL* det)
{
	render_object_start(r,"ol");
	render_association_key(r, "start");
	render_int_value(r, det->start);
	render_association_key(r, "tight");
	render_bool_value(r, det->is_tight);
	render_association_key(r, "mark");
	char buf[64];
    snprintf(buf, sizeof(buf), "%c", det->mark_delimiter);
	render_string_value(r, buf);

	render_children_start(r);
}

static void
render_open_li_block(MD_JSON* r, const MD_BLOCK_LI_DETAIL* det)
{
	render_object_start(r, "li");
	render_association_key(r, "isTask");
	render_bool_value(r, det->is_task);
	render_association_key(r, "taskMarkOffset");
	render_int_value(r, det->task_mark_offset);
	render_association_key(r, "taskMark");
	char buf[64];
	snprintf(buf, sizeof(buf), "%c", det->task_mark);
	render_string_value(r, buf);
	
	render_children_start(r);
}

static void
render_open_code_block(MD_JSON* r, const MD_BLOCK_CODE_DETAIL* det)
{
	render_object_start(r, "code");
	
	if(det->lang.text != NULL)
	{
		render_association_key(r, "language");
		render_attribute(r, &det->lang);
	}
	
	render_children_start(r);
}

static void
render_open_td_block(MD_JSON* r, const MD_CHAR* cell_type, const MD_BLOCK_TD_DETAIL* det)
{
	render_object_start(r, cell_type);
		
	render_association_key(r, "align");
	render_int_value(r, det->align);
	
	render_children_start(r);
}

static void
render_open_a_span(MD_JSON* r, const MD_SPAN_A_DETAIL* det)
{
	render_object_start(r, "a");
	render_association_key(r, "href");
	render_attribute(r, &det->href);

    if(det->title.text != NULL) {
		render_association_key(r, "title");
        render_attribute(r, &det->title);
    }

	render_children_start(r);
}

static void
render_open_img_span(MD_JSON* r, const MD_SPAN_IMG_DETAIL* det)
{
	render_object_start(r, "img");

	render_association_key(r, "src");
	render_attribute(r, &det->src);
	
	render_children_start(r);
}

static void
render_close_img_span(MD_JSON* r, const MD_SPAN_IMG_DETAIL* det)
{
	render_children_end(r);

	if(det->title.text != NULL) {
		render_association_key(r, "title");
		render_attribute(r, &det->title);
    }

	render_object_end(r);
}

static void
render_open_wikilink_span(MD_JSON* r, const MD_SPAN_WIKILINK_DETAIL* det)
{
	render_object_start(r, "wiki-link");
	{
		render_association_key(r, "target");
		render_attribute(r, &det->target);
	}
	render_object_end(r);
}

static int
enter_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    static const MD_CHAR* head[6] = { "<h1>", "<h2>", "<h3>", "<h4>", "<h5>", "<h6>" };
    MD_JSON* r = (MD_JSON*) userdata;

    switch(type) {
        case MD_BLOCK_DOC:
			render_object_start(r, "doc");
			render_children_start(r);
			break;
		case MD_BLOCK_QUOTE:
			render_object_start(r, "blockquote");
			render_children_start(r);
			break;
        case MD_BLOCK_UL:
			render_object_start(r, "ul");
			render_children_start(r);
			break;
        case MD_BLOCK_OL:
			render_open_ol_block(r, (const MD_BLOCK_OL_DETAIL*)detail);
			break;
        case MD_BLOCK_LI:
			render_open_li_block(r, (const MD_BLOCK_LI_DETAIL*)detail);
			break;
		case MD_BLOCK_HR:
			render_object_start(r, "hr");
			render_object_end(r);
			break;
        case MD_BLOCK_H:
			render_object_start(r, head[((MD_BLOCK_H_DETAIL*)detail)->level - 1]);
			render_children_start(r);
			break;
        case MD_BLOCK_CODE:
			render_open_code_block(r, (const MD_BLOCK_CODE_DETAIL*) detail);
			break;
        case MD_BLOCK_HTML:
			render_object_start(r, "html");
			render_children_start(r);
			break;
        case MD_BLOCK_P:
			render_object_start(r, "p");
			render_children_start(r);
			break;
        case MD_BLOCK_TABLE:
			render_object_start(r, "table");
			render_children_start(r);
			break;
        case MD_BLOCK_THEAD:
			render_object_start(r,"thead");
			render_children_start(r);
			break;
        case MD_BLOCK_TBODY:
			render_object_start(r, "tbody");
			render_children_start(r);
			break;
        case MD_BLOCK_TR:
			render_object_start(r, "tr");
			render_children_start(r);
			break;
        case MD_BLOCK_TH:
			render_open_td_block(r, "th", (MD_BLOCK_TD_DETAIL*)detail);
			break;
        case MD_BLOCK_TD:
			render_open_td_block(r, "td", (MD_BLOCK_TD_DETAIL*)detail);
			break;
    }

    return 0;
}

static int
leave_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
     MD_JSON* r = (MD_JSON*) userdata;

    switch(type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_QUOTE:
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
        case MD_BLOCK_LI:
        case MD_BLOCK_HR:
        case MD_BLOCK_H:
        case MD_BLOCK_CODE:
        case MD_BLOCK_HTML:
        case MD_BLOCK_P:
        case MD_BLOCK_TABLE:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
        case MD_BLOCK_TR:
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
			render_children_end(r);
			render_object_end(r);
    }

    return 0;
}

static int
enter_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
    MD_JSON* r = (MD_JSON*) userdata;

    switch(type) {
        case MD_SPAN_EM:
			render_object_start(r, "em");
			render_children_start(r);
			break;
		case MD_SPAN_STRONG:
			render_object_start(r, "strong");
			render_children_start(r);
			break;
		case MD_SPAN_U:
			render_object_start(r, "u");
			render_children_start(r);
			break;
        case MD_SPAN_A:
			render_open_a_span(r, (MD_SPAN_A_DETAIL*) detail);
			break;
        case MD_SPAN_IMG:
			render_open_img_span(r, (MD_SPAN_IMG_DETAIL*) detail);
			break;
        case MD_SPAN_CODE:
			render_object_start(r, "code");
			render_children_start(r);
			break;
        case MD_SPAN_DEL:
			render_object_start(r, "del");
			render_children_start(r);
			break;
        case MD_SPAN_LATEXMATH:
			render_object_start(r, "latex");
			render_children_start(r);
			break;
        case MD_SPAN_LATEXMATH_DISPLAY:
			render_object_start(r, "latex-display");
			render_children_start(r);
			break;
        case MD_SPAN_WIKILINK:
			render_open_wikilink_span(r, (MD_SPAN_WIKILINK_DETAIL*) detail);
			break;
    }

    return 0;
}

static int
leave_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
    MD_JSON* r = (MD_JSON*) userdata;

    switch(type) {
        case MD_SPAN_IMG:
			render_close_img_span(r, (MD_SPAN_IMG_DETAIL*)detail);
			break;
        case MD_SPAN_EM:
        case MD_SPAN_STRONG:
        case MD_SPAN_U:
        case MD_SPAN_A:
        case MD_SPAN_CODE:
        case MD_SPAN_DEL:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
        case MD_SPAN_WIKILINK:
			render_children_end(r);
			render_object_end(r);
    }

    return 0;
}

static int
text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    MD_JSON* r = (MD_JSON*) userdata;
	
	render_object_start(r, "text");
	{
		render_association_key(r, "type");
		
		switch(type) {
			case MD_TEXT_NULLCHAR:  render_string_value(r, "null"); break;
			case MD_TEXT_BR:        render_string_value(r, "br"); break;
			case MD_TEXT_SOFTBR:    render_string_value(r, "soft-br"); break;
			case MD_TEXT_HTML:
				render_object_start(r, "html");
				render_association_key(r, "source");
				render_string_value_size(r, text, size);
				render_object_end(r);
				break;
			case MD_TEXT_ENTITY:
				render_object_start(r, "entity");
				render_association_key(r, "source");
				render_string_value_size(r, text, size);
				render_object_end(r);
				break;
			default:
				render_object_start(r, "default");
				render_association_key(r, "source");
				render_string_value_size(r, text, size);
				render_object_end(r);
				break;
		}
	}
	render_object_end(r);
	return 0;
}

static void
debug_log_callback(const char* msg, void* userdata)
{
    MD_JSON* r = (MD_JSON*) userdata;
    if(r->flags & MD_JSON_FLAG_DEBUG)
        fprintf(stderr, "MD4C-JSON: %s\n", msg);
}

int
md_json(const MD_CHAR* input, MD_SIZE input_size,
        void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
        void* userdata, unsigned parser_flags, unsigned renderer_flags)
{
	MD_JSON render = { process_output, userdata, renderer_flags, 0, 0 };
    int i;
	render.levels = calloc(10,sizeof(MD_JSON_LEVEL));
	render.allocated_level = 10;
	
    MD_PARSER parser = {
        0,
        parser_flags,
        enter_block_callback,
        leave_block_callback,
        enter_span_callback,
        leave_span_callback,
        text_callback,
        debug_log_callback,
        NULL
    };

    /* Build map of characters which need escaping. */
    for(i = 0; i < 256; i++) {
        unsigned char ch = (unsigned char) i;
		
        if(strchr("\"\\/\b\f\n\r\t", ch) != NULL)
            render.escape_map[i] = 1;
    }

    /* Consider skipping UTF-8 byte order mark (BOM). */
    if(renderer_flags & MD_JSON_FLAG_SKIP_UTF8_BOM  &&  sizeof(MD_CHAR) == 1) {
        static const MD_CHAR bom[3] = { 0xef, 0xbb, 0xbf };
        if(input_size >= sizeof(bom)  &&  memcmp(input, bom, sizeof(bom)) == 0) {
            input += sizeof(bom);
            input_size -= sizeof(bom);
        }
    }

    return md_parse(input, input_size, &parser, (void*) &render);
}

