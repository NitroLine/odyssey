/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

#include <kiwi/kiwi.h>

#include <limits.h>

static inline int kiwi_search_path_ident_char(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '$';
}

typedef struct {
	const char *value;
	int len;
	int quoted;
} kiwi_search_path_ident_t;

/* Only simple ASCII identifiers; complex forms use set_config instead. */
static int kiwi_search_path_next(const char **cursor,
				 kiwi_search_path_ident_t *name)
{
	const char *value = *cursor;
	if (value == NULL) {
		return 0;
	}
	while (*value == ' ') {
		value++;
	}
	name->quoted = *value == '"';
	if (name->quoted) {
		value++;
	} else if (!((*value >= 'a' && *value <= 'z') ||
		     (*value >= 'A' && *value <= 'Z') || *value == '_')) {
		return -1;
	}
	name->value = value;
	while (kiwi_search_path_ident_char((unsigned char)*value)) {
		if (value - name->value == 63) {
			return -1;
		}
		value++;
	}
	name->len = value - name->value;
	if (name->len == 0 || (name->quoted && *value != '"')) {
		return -1;
	}
	if (name->quoted) {
		value++;
	}
	while (*value == ' ') {
		value++;
	}
	if (*value != 0 && *value != ',') {
		return -1;
	}
	*cursor = *value == ',' ? value + 1 : NULL;
	return 1;
}

static inline char kiwi_search_path_ident_byte(kiwi_search_path_ident_t *name,
					       int i)
{
	char c = name->value[i];
	if (!name->quoted && c >= 'A' && c <= 'Z') {
		c += 'a' - 'A';
	}
	return c;
}

static int kiwi_search_path_equal(const char *left, const char *right)
{
	for (;;) {
		kiwi_search_path_ident_t a, b;
		int a_rc = kiwi_search_path_next(&left, &a);
		int b_rc = kiwi_search_path_next(&right, &b);
		if (a_rc == -1 || b_rc == -1 || a_rc != b_rc) {
			return 0;
		}
		if (a_rc == 0) {
			return 1;
		}
		if (a.len != b.len) {
			return 0;
		}
		for (int i = 0; i < a.len; i++) {
			if (kiwi_search_path_ident_byte(&a, i) !=
			    kiwi_search_path_ident_byte(&b, i)) {
				return 0;
			}
		}
	}
}

/* Encode names as separate SQL string literals; NULL only checks syntax. */
static int kiwi_search_path_to_sql(const char *value, char *query)
{
	int pos = 0;
	int rc;
	kiwi_search_path_ident_t name;
	while ((rc = kiwi_search_path_next(&value, &name)) > 0) {
		if (pos != 0) {
			if (query) {
				query[pos] = ',';
			}
			pos++;
		}
		if (query) {
			memcpy(query + pos, "E'", 2);
		}
		pos += 2;
		for (int i = 0; i < name.len; i++) {
			if (query) {
				query[pos] =
					kiwi_search_path_ident_byte(&name, i);
			}
			pos++;
		}
		if (query) {
			query[pos] = '\'';
		}
		pos++;
	}
	return rc == -1 ? -1 : pos;
}

static int kiwi_var_cas(kiwi_var_t *var, kiwi_var_t *server_var, char **query,
			int pos)
{
	if (var->type == KIWI_VAR_UNDEF || kiwi_var_compare(var, server_var)) {
		return pos;
	}

	if (var->value_len <= 0) {
		return -1;
	}
	if (var->type == KIWI_VAR_SEARCH_PATH &&
	    server_var->type == KIWI_VAR_SEARCH_PATH &&
	    server_var->value_len > 0 &&
	    kiwi_search_path_equal(var->value, server_var->value)) {
		return pos;
	}

	const char *prefix = "SET ";
	const char *suffix = ";";
	int name_len = var->name_len - 1;
	int search_path = var->type == KIWI_VAR_SEARCH_PATH;
	if (search_path) {
		prefix = "SELECT pg_catalog.set_config('search_path',";
		suffix = ",false);";
		name_len = 0;
	}
	int prefix_len = strlen(prefix);
	int suffix_len = strlen(suffix);

	/* SET may add E'' per name; kiwi_enquote needs four spare bytes. */
	uint64_t size = (uint64_t)pos + prefix_len + name_len +
			(name_len != 0) +
			(search_path ? 3 : 2) * (uint64_t)(var->value_len - 1) +
			2 + 4 + suffix_len;
	/* kiwi_fe_write_query also adds a terminating NUL and Query header. */
	if (size > INT_MAX - sizeof(kiwi_header_t) - 1) {
		return -1;
	}
	char *buffer = realloc(*query, size);
	if (buffer == NULL) {
		return -1;
	}
	*query = buffer;

	int simple_len =
		search_path ? kiwi_search_path_to_sql(var->value, NULL) : -1;
	if (simple_len >= 0) {
		prefix = "SET ";
		suffix = ";";
		prefix_len = strlen(prefix);
		suffix_len = strlen(suffix);
		name_len = var->name_len - 1;
	}

	memcpy(buffer + pos, prefix, prefix_len);
	pos += prefix_len;
	if (name_len != 0) {
		memcpy(buffer + pos, var->name, name_len);
		pos += name_len;
		buffer[pos++] = '=';
	}
	int quote_len;
	if (simple_len >= 0) {
		quote_len = kiwi_search_path_to_sql(var->value, buffer + pos);
	} else {
		quote_len = kiwi_enquote(var->value, buffer + pos,
					 (int)size - pos - suffix_len);
	}
	if (quote_len == -1) {
		return -1;
	}
	pos += quote_len;
	memcpy(buffer + pos, suffix, suffix_len);
	pos += suffix_len;
	buffer[pos] = 0;
	return pos;
}

int kiwi_vars_cas(kiwi_vars_t *client, kiwi_vars_t *server, char **query)
{
	*query = NULL;
	int pos = 0;
	for (kiwi_var_type_t type = KIWI_VAR_CLIENT_ENCODING;
	     type < KIWI_VAR_MAX; type++) {
		if (type == KIWI_VAR_SEARCH_PATH ||
		    type == KIWI_VAR_COMPRESSION ||
		    type == KIWI_VAR_ODYSSEY_TARGET_SESSION_ATTRS) {
			continue;
		}
		pos = kiwi_var_cas(kiwi_vars_of(client, type),
				   kiwi_vars_of(server, type), query, pos);
		if (pos == -1) {
			goto error;
		}
	}

	/* SELECT takes a snapshot: apply transaction settings before it. */
	pos = kiwi_var_cas(kiwi_vars_of(client, KIWI_VAR_SEARCH_PATH),
			   kiwi_vars_of(server, KIWI_VAR_SEARCH_PATH), query,
			   pos);
	if (pos == -1) {
		goto error;
	}
	return pos;

error:
	free(*query);
	*query = NULL;
	return -1;
}
