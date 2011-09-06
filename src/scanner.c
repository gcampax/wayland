/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <expat.h>

#include "wayland-util.h"

#define WAYLAND_NS "http://wayland.freedesktop.org/protocol"
#define WAYLAND_CLIENT_NS "http://wayland.freedesktop.org/protocol/client"
#define WAYLAND_SERVER_NS "http://wayland.freedesktop.org/protocol/server"

static int
usage(int ret)
{
	fprintf(stderr, "usage: ./scanner [client-header|server-header|code]\n");
	exit(ret);
}

#define XML_BUFFER_SIZE 4096

struct protocol {
	char *name;
	char *uppercase_name;
	struct wl_list interface_list;
	int type_index;
	int null_run_length;
	char *copyright;
};

struct interface {
	char *name;
	char *uppercase_name;
	int version;
	int client_custom;
	int client_global;
	struct wl_list request_list;
	struct wl_list event_list;
	struct wl_list enumeration_list;
	struct wl_list property_list;
	struct wl_list link;
};

struct message {
	char *name;
	char *uppercase_name;
	struct wl_list arg_list;
	struct wl_list link;
	struct property *property;
	int arg_count;
	int type_index;
	int all_null;
	int destructor;
	int client_custom;
};

enum arg_type {
	INVALID,
	NEW_ID,
	INT,
	UNSIGNED,
	STRING,
	OBJECT,
	ARRAY,
	FLAGS,
	FD,
	DOUBLE
};

struct property {
	char *name;
	char *uppercase_name;
	enum arg_type type;
	int writable;
	int change_notify;
	struct wl_list link;
};

struct arg {
	char *name;
	enum arg_type type;
	char *interface_name;
	struct wl_list link;
};

struct enumeration {
	char *name;
	char *uppercase_name;
	struct wl_list entry_list;
	struct wl_list link;
};

struct entry {
	char *name;
	char *uppercase_name;
	char *value;
	struct wl_list link;
};

struct parse_context {
	const char *filename;
	XML_Parser parser;
	struct protocol *protocol;
	struct interface *interface;
	struct message *message;
	struct enumeration *enumeration;
	char character_data[8192];
	int character_data_length;
};

static char *
uppercase_dup(const char *src)
{
	char *u;
	int i;

	u = strdup(src);
	for (i = 0; u[i]; i++)
		u[i] = toupper(u[i]);
	u[i] = '\0';

	return u;
}

static void fail(struct parse_context *ctx, const char *msg) __attribute__((noreturn));

static void
fail(struct parse_context *ctx, const char *msg)
{
	fprintf(stderr, "%s:%ld: %s\n",
		ctx->filename, XML_GetCurrentLineNumber(ctx->parser), msg);
	exit(EXIT_FAILURE);
}

static enum arg_type
type_from_string(const char *type) {
	if (strcmp(type, "int") == 0)
		return INT;
	else if (strcmp(type, "uint") == 0)
		return UNSIGNED;
	else if (strcmp(type, "string") == 0)
		return STRING;
	else if (strcmp(type, "array") == 0)
		return ARRAY;
	else if (strcmp(type, "fd") == 0)
		return FD;
	else if (strcmp(type, "double") == 0)
		return DOUBLE;
	else
		return INVALID;
}

static struct arg*
make_arg(struct parse_context *ctx, const char *name, const char *type, const char *interface_name)
{
	struct arg* arg = malloc(sizeof *arg);
	arg->name = strdup(name);

	arg->type = type_from_string(type);
	if (arg->type != INVALID) {
		return arg;
	} else if (strcmp(type, "new_id") == 0) {
		if (interface_name == NULL)
			fail(ctx, "no interface name given");
		arg->type = NEW_ID;
		arg->interface_name = strdup(interface_name);
	} else if (strcmp(type, "object") == 0) {
		if (interface_name == NULL)
			fail(ctx, "no interface name given");
		arg->type = OBJECT;
		arg->interface_name = strdup(interface_name);
	} else {
		fail(ctx, "unknown type");
	}

	return arg;
}

static void
start_element(void *data, const char *element_name, const char **atts)
{
	struct parse_context *ctx = data;
	struct interface *interface;
	struct message *message;
	struct arg *arg;
	struct enumeration *enumeration;
	struct entry *entry;
	struct property *property;
	const char *name, *type, *interface_name, *value;
	int i, version, client_custom, client_global;
	int change_notify, writable;

	name = NULL;
	type = NULL;
	version = 0;
	interface_name = NULL;
	value = NULL;
	client_custom = 0;
	client_global = 0;
	writable = 0;
	change_notify = 0;
	for (i = 0; atts[i]; i += 2) {
		if (strcmp(atts[i], "name") == 0)
			name = atts[i + 1];
		if (strcmp(atts[i], "version") == 0)
			version = atoi(atts[i + 1]);
		if (strcmp(atts[i], "type") == 0)
			type = atts[i + 1];
		if (strcmp(atts[i], "value") == 0)
			value = atts[i + 1];
		if (strcmp(atts[i], "interface") == 0)
			interface_name = atts[i + 1];
		if (strcmp(atts[i], "change-notify") == 0)
			change_notify = (strcmp(atts[i + 1], "yes") == 0);
		if (strcmp(atts[i], "writable") == 0)
			writable = (strcmp(atts[i + 1], "yes") == 0);
		if (strcmp(atts[i], WAYLAND_CLIENT_NS "#custom") == 0)
			client_custom = (strcmp(atts[i + 1], "yes") == 0);
		if (strcmp(atts[i], WAYLAND_CLIENT_NS "#global") == 0)
			client_global = (strcmp(atts[i + 1], "yes") == 0);
	}

	ctx->character_data_length = 0;
	if (strcmp(element_name, WAYLAND_NS "#protocol") == 0) {
		if (name == NULL)
			fail(ctx, "no protocol name given");

		ctx->protocol->name = strdup(name);
		ctx->protocol->uppercase_name = uppercase_dup(name);
	} else if (strcmp(element_name, WAYLAND_NS "#copyright") == 0) {
		
	} else if (strcmp(element_name, WAYLAND_NS "#interface") == 0) {
		if (name == NULL)
			fail(ctx, "no interface name given");

		if (version == 0)
			fail(ctx, "no interface version given");

		interface = malloc(sizeof *interface);
		interface->name = strdup(name);
		interface->uppercase_name = uppercase_dup(name);
		interface->version = version;
		interface->client_custom = client_custom;
		interface->client_global = client_global;
		wl_list_init(&interface->request_list);
		wl_list_init(&interface->event_list);
		wl_list_init(&interface->enumeration_list);
		wl_list_init(&interface->property_list);
		wl_list_insert(ctx->protocol->interface_list.prev,
			       &interface->link);
		ctx->interface = interface;
	} else if (strcmp(element_name, WAYLAND_NS "#request") == 0 ||
		   strcmp(element_name, WAYLAND_NS "#event") == 0) {
		if (name == NULL)
			fail(ctx, "no request name given");

		message = malloc(sizeof *message);
		message->name = strdup(name);
		message->uppercase_name = uppercase_dup(name);
		wl_list_init(&message->arg_list);
		message->arg_count = 0;
		message->property = NULL;
		message->client_custom = client_custom;

		if (strcmp(element_name, WAYLAND_NS "#request") == 0)
			wl_list_insert(ctx->interface->request_list.prev,
				       &message->link);
		else
			wl_list_insert(ctx->interface->event_list.prev,
				       &message->link);

		if (type != NULL && strcmp(type, "destructor") == 0)
			message->destructor = 1;
		else
			message->destructor = 0;

		if (strcmp(name, "destroy") == 0 && !message->destructor)
			fail(ctx, "destroy request should be destructor type");

		ctx->message = message;
	} else if (strcmp(element_name, WAYLAND_NS "#property") == 0) {
		if (name == NULL)
			fail(ctx, "no property name given");

		property = malloc(sizeof *property);
		property->name = strdup(name);
		property->uppercase_name = uppercase_dup(name);
		property->writable = writable;
		property->change_notify = change_notify;
		if (strcmp(type, "flags") == 0)
			property->type = FLAGS;
		else
			property->type = type_from_string(type);
		wl_list_insert(ctx->interface->property_list.prev, &property->link);

		if (writable) {
			message = malloc(sizeof *message);
			asprintf(&message->name, "set_%s", name);
			message->uppercase_name = uppercase_dup(message->name);
			message->property = property;
			wl_list_init(&message->arg_list);

			if (property->type == FLAGS) {
				/* flags use two arguments, a change mask
				   and the new value */
				message->arg_count = 2;

				arg = make_arg(ctx, "value", "uint", NULL);
				wl_list_insert(message->arg_list.prev, &arg->link);

				arg = make_arg(ctx, "change_mask", "uint", NULL);
				wl_list_insert(message->arg_list.prev, &arg->link);
			} else {
				message->arg_count = 1;

				arg = make_arg(ctx, "value", type, interface_name);
				wl_list_insert(message->arg_list.prev, &arg->link);
			}

			wl_list_insert(ctx->interface->request_list.prev, &message->link);
		}

		if (change_notify) {
			message = malloc(sizeof *message);
			asprintf(&message->name, "%s_notify", name);
			message->uppercase_name = uppercase_dup(message->name);
			message->property = property;
			wl_list_init(&message->arg_list);

			if (property->type == FLAGS) {
				message->arg_count = 2;

				arg = make_arg(ctx, "value", "uint", NULL);
				wl_list_insert(message->arg_list.prev, &arg->link);

				arg = make_arg(ctx, "change_mask", "uint", NULL);
				wl_list_insert(message->arg_list.prev, &arg->link);
			} else {
				message->arg_count = 1;

				arg = make_arg(ctx, "value", type, interface_name);
				wl_list_insert(message->arg_list.prev, &arg->link);
			}

			wl_list_insert(ctx->interface->event_list.prev, &message->link);
		}
	} else if (strcmp(element_name, WAYLAND_NS "#arg") == 0) {
		arg = make_arg(ctx, name, type, interface_name);

		wl_list_insert(ctx->message->arg_list.prev, &arg->link);
		ctx->message->arg_count++;
	} else if (strcmp(element_name, WAYLAND_NS "#enum") == 0) {
		if (name == NULL)
			fail(ctx, "no enum name given");

		enumeration = malloc(sizeof *enumeration);
		enumeration->name = strdup(name);
		enumeration->uppercase_name = uppercase_dup(name);
		wl_list_init(&enumeration->entry_list);

		wl_list_insert(ctx->interface->enumeration_list.prev,
			       &enumeration->link);

		ctx->enumeration = enumeration;
	} else if (strcmp(element_name, WAYLAND_NS "#entry") == 0) {
		entry = malloc(sizeof *entry);
		entry->name = strdup(name);
		entry->uppercase_name = uppercase_dup(name);
		entry->value = strdup(value);
		wl_list_insert(ctx->enumeration->entry_list.prev,
			       &entry->link);
	}
}

static void
end_element(void *data, const XML_Char *name)
{
	struct parse_context *ctx = data;

	if (strcmp(name, WAYLAND_NS "#copyright") == 0) {
		ctx->protocol->copyright =
			strndup(ctx->character_data,
				ctx->character_data_length);
	}
}

static void
character_data(void *data, const XML_Char *s, int len)
{
	struct parse_context *ctx = data;

	if (ctx->character_data_length + len > sizeof (ctx->character_data)) {
		fprintf(stderr, "too much character data");
		exit(EXIT_FAILURE);
	    }

	memcpy(ctx->character_data + ctx->character_data_length, s, len);
	ctx->character_data_length += len;
}

static void
emit_opcodes(struct wl_list *message_list, struct interface *interface)
{
	struct message *m;
	int opcode;

	if (wl_list_empty(message_list))
		return;

	opcode = 0;
	wl_list_for_each(m, message_list, link)
		printf("#define %s_%s\t%d\n",
		       interface->uppercase_name, m->uppercase_name, opcode++);

	printf("\n");
}

static void
emit_simple_type(enum arg_type type, int is_const)
{
	switch (type) {
	case INVALID:
	case OBJECT:
		printf("void *");
	case INT:
	case FD:
		printf("int32_t ");
		break;
	case NEW_ID:
	case UNSIGNED:
	case FLAGS:
		printf("uint32_t ");
		break;
	case STRING:
		if (is_const)
			printf("const char *");
		else
			printf("char *");
		break;
	case DOUBLE:
		printf("double ");
		break;
	case ARRAY:
		printf("struct wl_array *");
		break;
	}
}

static void
emit_type(struct arg *a)
{
	if (a->type == OBJECT)
		printf("struct %s *", a->interface_name);
	else
		emit_simple_type(a->type, 1);
}

static void
emit_stubs(struct wl_list *message_list, struct interface *interface)
{
	struct message *m;
	struct arg *a, *ret;
	int has_destructor, has_destroy;

	if (interface->client_custom) {
		/* This interface is completely implemented by hand-written
		   code */
		return;
	}

	printf("static inline void\n"
	       "%s_set_user_data(struct %s *%s, void *user_data)\n"
	       "{\n"
	       "\twl_proxy_set_user_data((struct wl_proxy *) %s, user_data);\n"
	       "}\n\n",
	       interface->name, interface->name, interface->name,
	       interface->name);

	printf("static inline void *\n"
	       "%s_get_user_data(struct %s *%s)\n"
	       "{\n"
	       "\treturn wl_proxy_get_user_data((struct wl_proxy *) %s);\n"
	       "}\n\n",
	       interface->name, interface->name, interface->name,
	       interface->name);

	has_destructor = 0;
	has_destroy = 0;
	wl_list_for_each(m, message_list, link) {
		if (m->destructor)
			has_destructor = 1;
		if (strcmp(m->name, "destroy") == 0)
			has_destroy = 1;
	}

	if (!has_destructor && has_destroy) {
		fprintf(stderr,
			"interface %s has method named destroy but"
			"no destructor", interface->name);
		exit(EXIT_FAILURE);
	}

	if (!has_destructor) {
		printf("void %s_destroy(struct %s *%s);\n\n",
		       interface->name, interface->name, interface->name);
	} else {
		printf("void _%s_proxy_destroy(struct %s *%s);\n\n",
		       interface->name, interface->name, interface->name);
	}

	if (wl_list_empty(message_list))
		return;

	wl_list_for_each(m, message_list, link) {
		if (m->client_custom) {
			/* This method is hand written. */
			continue;
		}
		if (m->property != NULL) {
			/* Properties are handled later. */
			continue;
		}

		ret = NULL;
		wl_list_for_each(a, &m->arg_list, link) {
			if (a->type == NEW_ID)
				ret = a;
		}

		if (!m->client_custom)
			printf("static inline ");

		if (ret)
			printf("struct %s *\n",
			       ret->interface_name);
		else
			printf("void\n");

		printf("%s_%s(struct %s *%s",
		       interface->name, m->name,
		       interface->name, interface->name);

		wl_list_for_each(a, &m->arg_list, link) {
			if (a->type == NEW_ID)
				continue;
			printf(", ");
			emit_type(a);
			printf("%s", a->name);
		}

		printf(")\n"
		       "{\n");

		if (ret)
			printf("\tstruct %s *%s;\n\n"
			       "\t%s = _%s_proxy_create("
			       "wl_proxy_get_display((struct wl_proxy *) %s));\n"
			       "\tif (!%s)\n"
			       "\t\treturn NULL;\n\n",
			       ret->interface_name,
			       ret->name,
			       ret->name,
			       ret->interface_name,
			       interface->name, ret->name);

		printf("\twl_proxy_marshal((struct wl_proxy *) %s,\n"
		       "\t\t\t %s_%s",
		       interface->name,
		       interface->uppercase_name,
		       m->uppercase_name);

		wl_list_for_each(a, &m->arg_list, link) {
			printf(", ");
				printf("%s", a->name);
		}
		printf(");\n");

		if (m->destructor)
			printf("\t_%s_proxy_destroy(%s);\n", interface->name, interface->name);

		if (ret)
			printf("\n\treturn (struct %s *) %s;\n",
			       ret->interface_name, ret->name);

		printf("}\n\n");
	}
}

static const char *indent(int n)
{
	const char *whitespace[] = {
		"\t\t\t\t\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t\t\t\t ",
		"\t\t\t\t\t\t\t\t\t\t\t\t  ",
		"\t\t\t\t\t\t\t\t\t\t\t\t   ",
		"\t\t\t\t\t\t\t\t\t\t\t\t    ",
		"\t\t\t\t\t\t\t\t\t\t\t\t     ",
		"\t\t\t\t\t\t\t\t\t\t\t\t      ",
		"\t\t\t\t\t\t\t\t\t\t\t\t       "
	};

	return whitespace[n % 8] + 12 - n / 8;
}

static void
emit_enumerations(struct interface *interface)
{
	struct enumeration *e;
	struct entry *entry;

	wl_list_for_each(e, &interface->enumeration_list, link) {
		printf("#ifndef %s_%s_ENUM\n",
		       interface->uppercase_name, e->uppercase_name);
		printf("#define %s_%s_ENUM\n",
		       interface->uppercase_name, e->uppercase_name);
		printf("enum %s_%s {\n", interface->name, e->name);
		wl_list_for_each(entry, &e->entry_list, link)
			printf("\t%s_%s_%s = %s,\n",
			       interface->uppercase_name,
			       e->uppercase_name,
			       entry->uppercase_name, entry->value);
		printf("};\n");
		printf("#endif /* %s_%s_ENUM */\n\n",
		       interface->uppercase_name, e->uppercase_name);
	}
}

static void
emit_object_struct(struct interface *interface)
{
	struct property *p;

	if (interface->client_custom)
		return;

	printf("struct %s {\n"
	       "\tstruct wl_proxy parent;\n",
	       interface->name);

	wl_list_for_each(p, &interface->property_list, link) {
		printf("\t");
		if (p->type == FLAGS)
			emit_simple_type(UNSIGNED, 0);
		else
			emit_simple_type(p->type, 0);
		printf("%s;\n", p->name);
	}
	printf("};\n\n");
}

static void
emit_structs(struct wl_list *message_list, struct interface *interface)
{
	struct message *m;
	struct arg *a;
	struct property *p;
	int is_interface, n;

	if (wl_list_empty(message_list))
		return;

	is_interface = message_list == &interface->request_list;
	printf("struct %s_%s {\n", interface->name,
	       is_interface ? "interface" : "listener");

	wl_list_for_each(m, message_list, link) {
		printf("\tvoid (*%s)(", m->name);

		n = strlen(m->name) + 17;
		if (is_interface) {
			printf("struct wl_client *client,\n"
			       "%sstruct wl_resource *resource",
			       indent(n));
		} else {
			printf("void *data,\n"),
			printf("%sstruct %s *%s",
			       indent(n), interface->name, interface->name);
		}

		wl_list_for_each(a, &m->arg_list, link) {
			printf(",\n%s", indent(n));

			if (is_interface && a->type == OBJECT)
				printf("struct wl_resource *");
			else
				emit_type(a);

			printf("%s", a->name);
		}

		printf(");\n");
	}

	printf("};\n\n");

	if (!is_interface) {
	    printf("static inline int\n"
		   "%s_add_listener(struct %s *%s,\n"
		   "%sconst struct %s_listener *listener, void *data)\n"
		   "{\n"
		   "\treturn wl_proxy_add_listener((struct wl_proxy *) %s,\n"
		   "%s(void (**)(void)) listener, data);\n"
		   "}\n\n",
		   interface->name, interface->name, interface->name,
		   indent(17 + strlen(interface->name)),
		   interface->name,
		   interface->name,
		   indent(37));

	    wl_list_for_each(p, &interface->property_list, link) {
		    emit_simple_type(p->type, 1);
		    printf("%s_get_%s(struct %s* %s);\n",
			   interface->name, p->name, interface->name, interface->name);

		    if (p->writable) {
			    printf("void %s_set_%s(struct %s* %s, ",
				   interface->name, p->name, interface->name, interface->name);
			    emit_simple_type(p->type, 1);
			    printf("value);\n");
		    }
		    printf("\n");
	    }
	}
}

static void
format_copyright(const char *copyright)
{
	int bol = 1, start = 0, i;

	for (i = 0; copyright[i]; i++) {
		if (bol && (copyright[i] == ' ' || copyright[i] == '\t')) {
			continue;
		} else if (bol) {
			bol = 0;
			start = i;
		}

		if (copyright[i] == '\n' || copyright[i] == '\0') {
			printf("%s %.*s\n",
			       i == 0 ? "/*" : " *",
			       i - start, copyright + start);
			bol = 1;
		}
	}
	printf(" */\n\n");
}

static void
emit_header(struct protocol *protocol, int server)
{
	struct interface *i;
	const char *s = server ? "SERVER" : "CLIENT";

	if (protocol->copyright)
		format_copyright(protocol->copyright);

	printf("#ifndef %s_%s_PROTOCOL_H\n"
	       "#define %s_%s_PROTOCOL_H\n"
	       "\n"
	       "#ifdef  __cplusplus\n"
	       "extern \"C\" {\n"
	       "#endif\n"
	       "\n"
	       "#include <stdint.h>\n"
	       "#include <stddef.h>\n"
	       "#include \"wayland-util.h\"\n\n"
	       "struct wl_client;\n"
	       "struct wl_resource;\n\n",
	       protocol->uppercase_name, s,
	       protocol->uppercase_name, s);

	wl_list_for_each(i, &protocol->interface_list, link) {
		printf("struct %s;\n", i->name);
	}
	printf("\n");

	wl_list_for_each(i, &protocol->interface_list, link) {
		printf("extern const struct wl_interface "
		       "%s_interface;\n",
		       i->name);
	}
	printf("\n");

	if (!server) {
		/* emit constructors */
		wl_list_for_each(i, &protocol->interface_list, link) {
			if (i->client_global) {
				int n = 2 * strlen(i->name) + strlen("struct  *_bind(");
				printf("struct %s *%s_bind(struct wl_display *display,\n"
				       "%suint32_t name);\n",
				       i->name, i->name, indent(n));
			} else {
				printf("struct %s *_%s_proxy_create(struct wl_display *display);\n",
				       i->name, i->name);
			}
		}
	}
	printf("\n");

	wl_list_for_each(i, &protocol->interface_list, link) {
		emit_enumerations(i);

		if (server) {
			emit_structs(&i->request_list, i);
			emit_opcodes(&i->event_list, i);
		} else {
			emit_structs(&i->event_list, i);
			emit_opcodes(&i->request_list, i);
			emit_stubs(&i->request_list, i);
		}
	}

	printf("#ifdef  __cplusplus\n"
	       "}\n"
	       "#endif\n"
	       "\n"
	       "#endif\n");
}

static void
emit_constructor(struct interface *interface) {
	printf("WL_EXPORT struct %s *\n",
	       interface->name);

	if (interface->client_global) {
		printf("%s_bind(struct wl_display *display, uint32_t name)\n"
		       "{\n"
		       "\tstruct %s *proxy;\n"
		       "\tproxy = (struct %s*) wl_display_bind(display, name, &%s_interface, sizeof(struct %s));\n",
		       interface->name, interface->name,
		       interface->name, interface->name, interface->name);
	} else {
		printf("_%s_proxy_create(struct wl_display *display)\n"
		       "{\n"
		       "\tstruct %s *proxy;\n"
		       "\tproxy = (struct %s*) wl_proxy_create(display, &%s_interface, sizeof(struct %s));\n",
		       interface->name, interface->name,
		       interface->name, interface->name, interface->name);
	}

	if (!wl_list_empty(&interface->property_list)) {
		printf("\t%s_add_listener(proxy, &%s_property_listener, NULL);\n",
		       interface->name, interface->name);
	}

	printf("\treturn proxy;\n"
	       "}\n\n");
}

static void
emit_property_listeners(struct interface *interface) {
	struct message *m;
	struct property *p;

	if (wl_list_empty(&interface->property_list))
	    return;

	wl_list_for_each(p, &interface->property_list, link) {
		if (!p->change_notify)
			continue;

		printf("static void\n"
		       "%s_handle_%s_notify(void *data, struct %s* %s, ",
		       interface->name, p->name, interface->name, interface->name);

		if (p->type == FLAGS) {
			printf("uint32_t value, uint32_t change_mask)\n"
			       "{\n"
			       "\t%s->%s = (%s->%s & ~change_mask) | (value & change_mask);\n",
			       interface->name, p->name, interface->name, p->name);
		} else if (p->type == STRING) {
			printf("const char *value)\n"
			       "{\n"
			       "\tfree(%s->%s);\n"
			       "\t%s->%s = strdup(value);\n",
			       interface->name, p->name, interface->name, p->name);
		} else {
			emit_simple_type(p->type, 1);
			printf("value)\n"
			       "{\n"
			       "\t%s->%s = value;\n",
			       interface->name, p->name);
		}
		printf("}\n\n");
	}

	printf("static const struct %s_listener %s_property_listener = {\n",
	       interface->name, interface->name);

	wl_list_for_each(m, &interface->event_list, link) {
		if (m->property == NULL) {
			printf("\tNULL,\n");
		} else {
			printf("\t%s_handle_%s,\n",
			       interface->name, m->name);
		}
	}

	printf("};\n\n");
}

static void
emit_property_get_set(struct interface *interface) {
	struct property *p;

	if (wl_list_empty(&interface->property_list))
	    return;

	wl_list_for_each(p, &interface->property_list, link) {
		printf("WL_EXPORT ");
		emit_simple_type(p->type, 1);

		printf("\n"
		       "%s_get_%s(struct %s* %s)\n"
		       "{\n"
		       "\treturn %s->%s;\n"
		       "}\n\n",
		       interface->name, p->name,
		       interface->name, interface->name,
		       interface->name, p->name);

		if (!p->writable)
			continue;

		printf("WL_EXPORT void\n"
		       "%s_set_%s(struct %s* %s, ",
		       interface->name, p->name,
		       interface->name, interface->name);
		emit_simple_type(p->type, 1);

		printf("value)\n"
		       "{\n"
		       "\twl_proxy_marshal((struct wl_proxy*) %s,\n"
		       "\t\t\t %s_SET_%s, ",
		       interface->name,
		       interface->uppercase_name,
		       p->uppercase_name);

		if (p->type == FLAGS) {
			printf("value, value ^ %s->%s",
			       interface->name, p->name);
		} else {
			printf("value");
		}

		printf(");\n\n");

		if (p->type == STRING) {
			printf("\tfree(%s->%s);\n"
			       "\t%s->%s = strdup(value);\n",
			       interface->name, p->name,
			       interface->name, p->name);
		} else {
			printf("\t%s->%s = value;\n",
			       interface->name, p->name);
		}
		printf ("}\n\n");
	}
}

static void
emit_destructor(struct interface *interface)
{
	struct property *p;
	struct message *m;
	int has_destructor = 0;

	wl_list_for_each(m, &interface->request_list, link) {
		if (m->destructor) {
			has_destructor = 1;
			break;
		}
	}

	printf("WL_EXPORT void\n");
	if (has_destructor)
		printf("_%s_proxy_destroy", interface->name);
	else
		printf("%s_destroy", interface->name);
	printf("(struct %s* %s)\n"
	       "{\n",
	       interface->name, interface->name);

	wl_list_for_each(p, &interface->property_list, link) {
		if (p->type == STRING) {
			printf("\tfree(%s->%s);\n"
			       "\t%s->%s = NULL;\n\n",
			       interface->name, p->name,
			       interface->name, p->name);
		}
	}

	printf ("\twl_proxy_destroy("
		"(struct wl_proxy *) %s);\n",
		interface->name);

	printf ("}\n\n");
}

static void
emit_client_code(struct protocol *protocol) {
	struct interface *i;

	if (protocol->copyright)
		format_copyright(protocol->copyright);

	printf("#include <wayland-client-private.h>\n\n");

	wl_list_for_each(i, &protocol->interface_list, link) {
		if (i->client_custom)
			continue;

		emit_object_struct(i);
	}
	printf("\n");

	wl_list_for_each(i, &protocol->interface_list, link) {
		if (i->client_custom)
			continue;

		emit_property_listeners(i);
		emit_constructor(i);
		emit_destructor(i);
		emit_property_get_set(i);
	}
}

static void
emit_types_forward_declarations(struct protocol *protocol,
				struct wl_list *message_list)
{
	struct message *m;
	struct arg *a;
	int length;

	wl_list_for_each(m, message_list, link) {
		length = 0;
		m->all_null = 1;
		wl_list_for_each(a, &m->arg_list, link) {
			length++;
			switch (a->type) {
			case NEW_ID:
			case OBJECT:
				m->all_null = 0;
				printf("extern const struct wl_interface %s_interface;\n",
				       a->interface_name);
				break;
			default:
				break;
			}
		}

		if (m->all_null && length > protocol->null_run_length)
			protocol->null_run_length = length;
	}
}

static void
emit_null_run(struct protocol *protocol)
{
	int i;

	for (i = 0; i < protocol->null_run_length; i++)
		printf("\tNULL,\n");
}

static void
emit_types(struct protocol *protocol, struct wl_list *message_list)
{
	struct message *m;
	struct arg *a;

	wl_list_for_each(m, message_list, link) {
		if (m->all_null) {
			m->type_index = 0;
			continue;
		}

		m->type_index =
			protocol->null_run_length + protocol->type_index;
		protocol->type_index += m->arg_count;

		wl_list_for_each(a, &m->arg_list, link) {
			switch (a->type) {
			case NEW_ID:
			case OBJECT:
				if (strcmp(a->interface_name,
					   "wl_object") != 0)
					printf("\t&%s_interface,\n",
					       a->interface_name);
				else
					printf("\tNULL,\n");
				break;
			default:
				printf("\tNULL,\n");
				break;
			}
		}
	}
}

static void
emit_messages(struct wl_list *message_list,
	      struct interface *interface, const char *suffix)
{
	struct message *m;
	struct arg *a;

	if (wl_list_empty(message_list))
		return;

	printf("static const struct wl_message "
	       "%s_%s[] = {\n",
	       interface->name, suffix);

	wl_list_for_each(m, message_list, link) {
		printf("\t{ \"%s\", \"", m->name);
		wl_list_for_each(a, &m->arg_list, link) {
			switch (a->type) {
			default:
			case INT:
				printf("i");
				break;
			case NEW_ID:
				printf("n");
				break;
			case UNSIGNED:
				printf("u");
				break;
			case STRING:
				printf("s");
				break;
			case OBJECT:
				printf("o");
				break;
			case ARRAY:
				printf("a");
				break;
			case FD:
				printf("h");
				break;
			case DOUBLE:
				printf("d");
				break;
			}
		}
		printf("\", types + %d },\n", m->type_index);
	}

	printf("};\n\n");
}

static void
emit_code(struct protocol *protocol)
{
	struct interface *i;

	if (protocol->copyright)
		format_copyright(protocol->copyright);

	printf("#include <stdlib.h>\n"
	       "#include <stdint.h>\n"
	       "#include \"wayland-util.h\"\n\n");

	wl_list_for_each(i, &protocol->interface_list, link) {
		emit_types_forward_declarations(protocol, &i->request_list);
		emit_types_forward_declarations(protocol, &i->event_list);
	}
	printf("\n");

	printf("static const struct wl_interface *types[] = {\n");
	emit_null_run(protocol);
	wl_list_for_each(i, &protocol->interface_list, link) {
		emit_types(protocol, &i->request_list);
		emit_types(protocol, &i->event_list);
	}
	printf("};\n\n");

	wl_list_for_each(i, &protocol->interface_list, link) {

		emit_messages(&i->request_list, i, "requests");
		emit_messages(&i->event_list, i, "events");

		printf("WL_EXPORT const struct wl_interface "
		       "%s_interface = {\n"
		       "\t\"%s\", %d,\n",
		       i->name, i->name, i->version);

		if (!wl_list_empty(&i->request_list))
			printf("\tARRAY_LENGTH(%s_requests), %s_requests,\n",
			       i->name, i->name);
		else
			printf("\t0, NULL,\n");

		if (!wl_list_empty(&i->event_list))
			printf("\tARRAY_LENGTH(%s_events), %s_events,\n",
			       i->name, i->name);
		else
			printf("\t0, NULL,\n");

		printf("};\n\n");
	}
}

int main(int argc, char *argv[])
{
	struct parse_context ctx;
	struct protocol protocol;
	int len;
	void *buf;

	if (argc != 2)
		usage(EXIT_FAILURE);

	if (strcmp(argv[1], "--help") == 0)
		usage(EXIT_SUCCESS);

	wl_list_init(&protocol.interface_list);
	protocol.type_index = 0;
	protocol.null_run_length = 0;
	protocol.copyright = NULL;
	ctx.protocol = &protocol;

	ctx.filename = "<stdin>";
	ctx.parser = XML_ParserCreateNS(NULL, '#');
	XML_SetUserData(ctx.parser, &ctx);
	if (ctx.parser == NULL) {
		fprintf(stderr, "failed to create parser\n");
		exit(EXIT_FAILURE);
	}

	XML_SetElementHandler(ctx.parser, start_element, end_element);
	XML_SetCharacterDataHandler(ctx.parser, character_data);

	do {
		buf = XML_GetBuffer(ctx.parser, XML_BUFFER_SIZE);
		len = fread(buf, 1, XML_BUFFER_SIZE, stdin);
		if (len < 0) {
			fprintf(stderr, "fread: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		XML_ParseBuffer(ctx.parser, len, len == 0);

	} while (len > 0);

	XML_ParserFree(ctx.parser);

	if (strcmp(argv[1], "client-header") == 0) {
		emit_header(&protocol, 0);
	} else if (strcmp(argv[1], "server-header") == 0) {
		emit_header(&protocol, 1);
	} else if (strcmp(argv[1], "code") == 0) {
		emit_code(&protocol);
	} else if (strcmp(argv[1], "client-code") == 0) {
		emit_client_code(&protocol);
	}

	return 0;
}
