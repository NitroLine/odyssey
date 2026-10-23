/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

#include <kiwi/kiwi.h>

#include <limits.h>

static int kiwi_var_cas(kiwi_var_t *var, kiwi_var_t *server_var, char **query,
			int pos)
{
	if (var->type == KIWI_VAR_UNDEF || kiwi_var_compare(var, server_var)) {
		return pos;
	}

	const char *prefix = "SET ";
	const char *suffix = ";";
	int name_len = var->name_len - 1;
	if (var->type == KIWI_VAR_SEARCH_PATH) {
		prefix = "SELECT pg_catalog.set_config('search_path',";
		suffix = ",false);";
		name_len = 0;
	}
	int prefix_len = strlen(prefix);
	int suffix_len = strlen(suffix);
	if (var->value_len <= 0) {
		return -1;
	}

	/* Each byte may need escaping; kiwi_enquote needs four spare bytes. */
	uint64_t size = (uint64_t)pos + prefix_len + name_len +
			(name_len != 0) + 2 * (uint64_t)(var->value_len - 1) +
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

	memcpy(buffer + pos, prefix, prefix_len);
	pos += prefix_len;
	if (name_len != 0) {
		memcpy(buffer + pos, var->name, name_len);
		pos += name_len;
		buffer[pos++] = '=';
	}
	int quote_len = kiwi_enquote(var->value, buffer + pos,
				     (int)size - pos - suffix_len);
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
