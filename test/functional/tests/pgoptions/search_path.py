import json
import os
import shlex
import subprocess


PSQL = ["psql", "-X", "-qAt", "-v", "ON_ERROR_STOP=1"]
STATE = "SELECT json_build_array(current_setting('search_path'), current_schemas(false))"


def execute(sql, env):
    result = subprocess.run(PSQL, input=sql, env=env, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(f"psql failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout.splitlines()


def literal(value):
    return "'" + value.replace("'", "''") + "'"


def set_path(path):
    return f"SELECT pg_catalog.set_config('search_path', {literal(path)}, false);"


def check_path(path, runtime=False, transaction_isolation=False):
    expected = json.loads(execute(set_path(path) + "\n" + STATE + ";\n", backend)[-1])
    env = client.copy()
    startup = "public" if runtime else path
    env["PGOPTIONS"] = "-c search_path=" + startup.replace("\\", "\\\\").replace(" ", "\\ ")

    if transaction_isolation:
        env["PGOPTIONS"] += " -c transaction_isolation=serializable"

    # A second client changes the only pooled backend while the first is idle.
    other_client = shlex.join(
        ["env", "PGOPTIONS=-c search_path=pg_catalog"]
        + PSQL
        + ["-c", "SELECT pg_backend_pid()"]
    )
    change = set_path(path) if runtime else ""
    result = execute(
        f"{change}\nSELECT pg_backend_pid();\n{STATE};\n"
        f"\\! {other_client}\nSELECT pg_backend_pid();\n{STATE};\n",
        env,
    )
    if runtime:
        result = result[1:]  # The value returned by set_config().
    assert len(result) == 5, result
    assert result[0] == result[2] == result[3], ("backend was not reused", result)
    assert json.loads(result[1]) == expected, (path, "initial", result, expected)
    assert json.loads(result[4]) == expected, (path, "restored", result, expected)


client = os.environ.copy()
client.setdefault("PGHOST", "127.0.0.1")
client.setdefault("PGPORT", "6432")
client.setdefault("PGUSER", "useropt")
client.setdefault("PGDATABASE", "postgres")
client.pop("PGOPTIONS", None)
backend = client.copy()
backend["PGPORT"] = os.environ.get("ODYSSEY_TEST_POSTGRES_PORT", "5432")
backend["PGUSER"] = "postgres"

paths = [
    '"$user",public,popis',
    "schema01, schema02, schema03, schema04, schema05, schema06, schema07, public",
    ", ".join(["public"] * 150),
    '"my-schema", public',
    '"O\'Reilly", "a\\b", public',
    '"кириллица", public',
    '"$user", public',
    '"$$", "$a$", public',
    '"public;create/**/table/**/zzz()", public',
    "",
]
for path in paths:
    check_path(path)

# Restoring search_path must not take a snapshot before SET transaction_isolation.
check_path("public", transaction_isolation=True)

# Upstream PostgreSQL started reporting search_path changes in version 18.
version = int(execute("SHOW server_version_num;\n", backend)[0])
if version >= 180000:
    for path in paths:
        check_path(path, runtime=True)

print(f"search_path restoration passed on PostgreSQL {version}")
