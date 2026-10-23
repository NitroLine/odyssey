#include <kiwi/kiwi.h>
#include <tests/odyssey_test.h>

#include <limits.h>

static void check_search_path(const char *value, const char *expected)
{
	kiwi_vars_t client, server;
	kiwi_vars_init(&client);
	kiwi_vars_init(&server);
	test(kiwi_vars_set(&client, KIWI_VAR_SEARCH_PATH, value,
			   strlen(value) + 1) == 0);
	char *query;
	int size = kiwi_vars_cas(&client, &server, &query);
	test(size > 0);
	test(size == (int)strlen(expected));
	test(strcmp(query, expected) == 0);
	free(query);
	kiwi_vars_free(&client);
	kiwi_vars_free(&server);
}

void kiwi_test_vars_cas(void)
{
	check_search_path(
		"schema01, schema02, schema03, schema04, schema05, schema06, schema07, public",
		"SELECT pg_catalog.set_config('search_path',E'schema01, schema02, schema03, schema04, schema05, schema06, schema07, public',false);");
	check_search_path(
		"", "SELECT pg_catalog.set_config('search_path',E'',false);");
	check_search_path(
		"\"my-schema\", public",
		"SELECT pg_catalog.set_config('search_path',E'\"my-schema\", public',false);");
	check_search_path(
		"\"O'Reilly\", \"a\\b\", public",
		"SELECT pg_catalog.set_config('search_path',E'\"O''Reilly\", \"a\\\\b\", public',false);");
	check_search_path(
		"\"$user\", public",
		"SELECT pg_catalog.set_config('search_path',E'\"$user\", public',false);");
	char path[2048] = "public";
	for (int i = 0; i < 200; i++) {
		strcat(path, ", public");
	}
	char long_query[2200];
	snprintf(long_query, sizeof(long_query),
		 "SELECT pg_catalog.set_config('search_path',E'%s',false);",
		 path);
	check_search_path(path, long_query);

	kiwi_vars_t client, server;
	kiwi_vars_init(&client);
	kiwi_vars_init(&server);
	char *query;
	test(kiwi_vars_cas(&client, &server, &query) == 0);
	test(query == NULL);
	/* Compression and Odyssey's own options must never be sent as SET. */
	test(kiwi_vars_set(&client, KIWI_VAR_COMPRESSION, "on", sizeof("on")) ==
	     0);
	test(kiwi_vars_set(&client, KIWI_VAR_ODYSSEY_TARGET_SESSION_ATTRS,
			   "read-write", sizeof("read-write")) == 0);
	test(kiwi_vars_cas(&client, &server, &query) == 0);
	test(query == NULL);
	test(kiwi_vars_set(&client, KIWI_VAR_SEARCH_PATH, "public",
			   sizeof("public")) == 0);
	test(kiwi_vars_set(&client, KIWI_VAR_TRANSACTION_ISOLATION,
			   "serializable", sizeof("serializable")) == 0);
	/* A SELECT before SET transaction_isolation would take a snapshot. */
	const char *expected =
		"SET transaction_isolation=E'serializable';"
		"SELECT pg_catalog.set_config('search_path',E'public',false);";
	test(kiwi_vars_cas(&client, &server, &query) == (int)strlen(expected));
	test(strcmp(query, expected) == 0);
	free(query);
	test(kiwi_vars_override(&server, &client) == 0);
	test(kiwi_vars_cas(&client, &server, &query) == 0);
	test(query == NULL);
	/* Reject oversized input and free an earlier parameter's allocation. */
	test(kiwi_vars_set(&client, KIWI_VAR_APPLICATION_NAME, "test",
			   sizeof("test")) == 0);
	kiwi_var_t *var = kiwi_vars_of(&client, KIWI_VAR_WORK_MEM);
	test(kiwi_var_set(var, KIWI_VAR_WORK_MEM, "1MB", sizeof("1MB")) == 0);
	var->value_len = INT_MAX;
	test(kiwi_vars_cas(&client, &server, &query) == -1);
	test(query == NULL);
	kiwi_vars_free(&client);
	kiwi_vars_free(&server);
}
