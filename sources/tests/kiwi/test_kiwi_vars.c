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

static void check_compare_search_paths(const char *client_path,
				       const char *server_path, int equal)
{
	kiwi_vars_t client, server;
	kiwi_vars_init(&client);
	kiwi_vars_init(&server);
	test(kiwi_vars_set(&client, KIWI_VAR_SEARCH_PATH, client_path,
			   strlen(client_path) + 1) == 0);
	test(kiwi_vars_set(&server, KIWI_VAR_SEARCH_PATH, server_path,
			   strlen(server_path) + 1) == 0);
	char *query;
	int size = kiwi_vars_cas(&client, &server, &query);
	if (equal) {
		test(size == 0);
		test(query == NULL);
	} else {
		test(size > 0);
		test(query != NULL);
		free(query);
	}
	kiwi_vars_free(&client);
	kiwi_vars_free(&server);
}

void kiwi_test_vars_cas(void)
{
	check_compare_search_paths("a,b", "a, b", 1);
	check_compare_search_paths("PUBLIC", "public", 1);
	check_compare_search_paths("\"public\", \"$user\"", "public, \"$user\"",
				   1);
	check_compare_search_paths("default", "\"default\"", 1);
	check_compare_search_paths("a,b", "b,a", 0);
	check_compare_search_paths("a,b", "a,b,c", 0);
	check_compare_search_paths("\"A\"", "a", 0);
	check_compare_search_paths("a", "\"A\"", 0);
	check_compare_search_paths("a,,b", "a,b", 0);
	check_search_path(
		"schema01, schema02, schema03, schema04, schema05, schema06, schema07, public",
		"SET search_path=E'schema01',E'schema02',E'schema03',E'schema04',E'schema05',E'schema06',E'schema07',E'public';");
	check_search_path(
		"", "SELECT pg_catalog.set_config('search_path',E'',false);");
	check_search_path(
		"\"my-schema\", public",
		"SELECT pg_catalog.set_config('search_path',E'\"my-schema\", public',false);");
	check_search_path(
		"\"O'Reilly\", \"a\\b\", public",
		"SELECT pg_catalog.set_config('search_path',E'\"O''Reilly\", \"a\\\\b\", public',false);");
	check_search_path("\"$user\", public",
			  "SET search_path=E'$user',E'public';");
	check_search_path("a,b,c", "SET search_path=E'a',E'b',E'c';");
	check_search_path("PUBLIC, \"MixedCase\", public",
			  "SET search_path=E'public',E'MixedCase',E'public';");
	check_search_path("default, true, all",
			  "SET search_path=E'default',E'true',E'all';");
	check_search_path(
		"public$; SELECT 1 AS x$",
		"SELECT pg_catalog.set_config('search_path',E'public$; SELECT 1 AS x$',false);");
	check_search_path(
		"\"a\"\"b\", public",
		"SELECT pg_catalog.set_config('search_path',E'\"a\"\"b\", public',false);");
	check_search_path(" a , b ", "SET search_path=E'a',E'b';");
	check_search_path(
		"a,,b",
		"SELECT pg_catalog.set_config('search_path',E'a,,b',false);");
	check_search_path(
		"a,",
		"SELECT pg_catalog.set_config('search_path',E'a,',false);");
	check_search_path(
		"\"\"",
		"SELECT pg_catalog.set_config('search_path',E'\"\"',false);");
	char identifier[65];
	char expected_identifier[160];
	memset(identifier, 'a', 63);
	identifier[63] = 0;
	snprintf(expected_identifier, sizeof(expected_identifier),
		 "SET search_path=E'%s';", identifier);
	check_search_path(identifier, expected_identifier);
	identifier[63] = 'a';
	identifier[64] = 0;
	snprintf(expected_identifier, sizeof(expected_identifier),
		 "SELECT pg_catalog.set_config('search_path',E'%s',false);",
		 identifier);
	check_search_path(identifier, expected_identifier);
	char path[2048] = "public";
	for (int i = 0; i < 200; i++) {
		strcat(path, ", public");
	}
	char long_query[2600] = "SET search_path=E'public'";
	for (int i = 0; i < 200; i++) {
		strcat(long_query, ",E'public'");
	}
	strcat(long_query, ";");
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
	test(kiwi_vars_set(&client, KIWI_VAR_SEARCH_PATH,
			   "\"my-schema\", public",
			   sizeof("\"my-schema\", public")) == 0);
	test(kiwi_vars_set(&client, KIWI_VAR_TRANSACTION_ISOLATION,
			   "serializable", sizeof("serializable")) == 0);
	/* A SELECT before SET transaction_isolation would take a snapshot. */
	const char *expected =
		"SET transaction_isolation=E'serializable';"
		"SELECT pg_catalog.set_config('search_path',E'\"my-schema\", public',false);";
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
