#include <kiwi/kiwi.h>
#include <tests/odyssey_test.h>

#include <limits.h>

static void check_search_path(const char *value, int smart,
			      const char *expected)
{
	kiwi_vars_t client, server;
	kiwi_vars_init(&client);
	kiwi_vars_init(&server);
	test(kiwi_vars_set(&client, KIWI_VAR_SEARCH_PATH, value,
			   strlen(value) + 1) == 0);
	char *query;
	int size = kiwi_vars_cas(&client, &server, &query, smart);
	test(size == (int)strlen(expected));
	test(strcmp(query, expected) == 0);
	free(query);
	kiwi_vars_free(&client);
	kiwi_vars_free(&server);
}

void kiwi_test_vars_cas(void)
{
	check_search_path("a,b,c", 1, "SET search_path=a,b,c;");
	check_search_path("\"my schema\", \"a\"\"b\", public", 1,
			  "SET search_path=\"my schema\", \"a\"\"b\", public;");
	check_search_path("\"$user\", public", 1,
			  "SET search_path=\"$user\", public;");
	check_search_path("\"$$\", \"$a$\", public", 1,
			  "SET search_path=\"$$\", \"$a$\", public;");
	check_search_path("PUBLIC, \"MixedCase\", public", 1,
			  "SET search_path=PUBLIC, \"MixedCase\", public;");
	check_search_path(
		"a,b,c", 0,
		"SELECT pg_catalog.set_config('search_path',E'a,b,c',false);");
	for (int smart = 0; smart <= 1; smart++) {
		check_search_path(
			"\"my-schema\", public", smart,
			"SELECT pg_catalog.set_config('search_path',E'\"my-schema\", public',false);");
		check_search_path(
			"\"O'Reilly\", \"a\\b\", public", smart,
			"SELECT pg_catalog.set_config('search_path',E'\"O''Reilly\", \"a\\\\b\", public',false);");
		check_search_path(
			"", smart,
			"SELECT pg_catalog.set_config('search_path',E'',false);");
		check_search_path(
			"public$; SELECT 1 AS x$", smart,
			"SELECT pg_catalog.set_config('search_path',E'public$; SELECT 1 AS x$',false);");
		check_search_path(
			"default", smart,
			"SELECT pg_catalog.set_config('search_path',E'default',false);");
	}

	char identifier[65];
	char expected_identifier[160];
	memset(identifier, 'a', 63);
	identifier[63] = 0;
	snprintf(expected_identifier, sizeof(expected_identifier),
		 "SET search_path=%s;", identifier);
	check_search_path(identifier, 1, expected_identifier);
	identifier[63] = 'a';
	identifier[64] = 0;
	snprintf(expected_identifier, sizeof(expected_identifier),
		 "SELECT pg_catalog.set_config('search_path',E'%s',false);",
		 identifier);
	check_search_path(identifier, 1, expected_identifier);

	char path[2048] = "public";
	for (int i = 0; i < 200; i++) {
		strcat(path, ", public");
	}
	char long_query[2200];
	snprintf(long_query, sizeof(long_query), "SET search_path=%s;", path);
	check_search_path(path, 1, long_query);

	kiwi_vars_t client, server;
	kiwi_vars_init(&client);
	kiwi_vars_init(&server);
	char *query;
	test(kiwi_vars_cas(&client, &server, &query, 1) == 0);
	test(query == NULL);
	test(kiwi_vars_set(&client, KIWI_VAR_COMPRESSION, "on", sizeof("on")) ==
	     0);
	test(kiwi_vars_set(&client, KIWI_VAR_ODYSSEY_TARGET_SESSION_ATTRS,
			   "read-write", sizeof("read-write")) == 0);
	test(kiwi_vars_cas(&client, &server, &query, 1) == 0);
	test(query == NULL);
	test(kiwi_vars_set(&client, KIWI_VAR_SEARCH_PATH,
			   "\"my-schema\", public",
			   sizeof("\"my-schema\", public")) == 0);
	test(kiwi_vars_set(&client, KIWI_VAR_TRANSACTION_ISOLATION,
			   "serializable", sizeof("serializable")) == 0);
	const char *expected =
		"SET transaction_isolation=E'serializable';"
		"SELECT pg_catalog.set_config('search_path',E'\"my-schema\", public',false);";
	test(kiwi_vars_cas(&client, &server, &query, 1) ==
	     (int)strlen(expected));
	test(strcmp(query, expected) == 0);
	free(query);
	test(kiwi_vars_override(&server, &client) == 0);
	test(kiwi_vars_cas(&client, &server, &query, 1) == 0);
	test(query == NULL);
	test(kiwi_vars_set(&client, KIWI_VAR_APPLICATION_NAME, "test",
			   sizeof("test")) == 0);
	kiwi_var_t *var = kiwi_vars_of(&client, KIWI_VAR_WORK_MEM);
	test(kiwi_var_set(var, KIWI_VAR_WORK_MEM, "1MB", sizeof("1MB")) == 0);
	var->value_len = INT_MAX;
	test(kiwi_vars_cas(&client, &server, &query, 1) == -1);
	test(query == NULL);
	kiwi_vars_free(&client);
	kiwi_vars_free(&server);
}
