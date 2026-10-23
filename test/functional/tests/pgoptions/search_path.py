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


def check_path(path, runtime=False, transaction_isolation=False, normalized=None):
    expected = json.loads(execute(set_path(path) + "\n" + STATE + ";\n", backend)[-1])
    restored = [normalized if normalized is not None else expected[0], expected[1]]
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
    initial = json.loads(result[1])
    # A pool reset can restore the path before the first observation.
    assert initial[0] in (expected[0], restored[0]), (path, "initial", result, expected, restored)
    assert initial[1] == expected[1], (path, "initial schemas", result, expected)
    assert json.loads(result[4]) == restored, (path, "restored", result, restored)


client = os.environ.copy()
client.setdefault("PGHOST", "127.0.0.1")
client.setdefault("PGPORT", "6432")
client.setdefault("PGUSER", "useropt")
client.setdefault("PGDATABASE", "postgres")
client.pop("PGOPTIONS", None)
backend = client.copy()
backend["PGPORT"] = os.environ.get("ODYSSEY_TEST_POSTGRES_PORT", "5432")
backend["PGUSER"] = "postgres"

def identifier(value):
    return '"' + value.replace('"', '""') + '"'


prefix = f"odyssey_path_{os.getpid()}"
schemas = [prefix + suffix for suffix in ["_a", "_b", "_MiXeD", "-dash", "_O'Reilly", '_a"b', "_кириллица"]]
long_name = (prefix + "_" + "a" * 63)[:63]
schemas.append(long_name)
execute("\n".join(f"CREATE SCHEMA {identifier(name)};" for name in schemas), backend)
try:
    a, b, mixed, dash, apostrophe, quote, unicode = schemas[:7]
    paths = [
        ('"$user",public,popis', '"$user", public, popis'),
        ("schema01, schema02, schema03, schema04, schema05, schema06, schema07, public", None),
        (", ".join(["public"] * 150), None),
        ('"my-schema", public', None),
        ('"O\'Reilly", "a\\b", public', None),
        ('"кириллица", public', None),
        ('"$user", public', None),
        ('"$$", "$a$", public', None),
        ('"public;create/**/table/**/zzz()", public', None),
        ("", None),
        (f"{a},{b},public", f"{a}, {b}, public"),
        (f"{a.upper()}, {b}, public", f"{a}, {b}, public"),
        (f"{identifier(a)}, {b}, public", f"{a}, {b}, public"),
        (f"{a}, {identifier(mixed)}, public", None),
        (f"{identifier(dash)}, {b}, public", None),
        (f"{identifier(apostrophe)}, {b}, public", None),
        (f"{identifier(quote)}, {b}, public", None),
        (f"{identifier(unicode)}, {b}, public", None),
        (f"{long_name}, public", None),
        (f"{long_name}a, public", None),
        (f"{identifier(long_name + 'a')}, public", None),
        ("default, true, all", '"default", "true", "all"'),
    ]
    for path, normalized in paths:
        check_path(path, normalized=normalized)

    # The SELECT fallback must run after transaction settings.
    check_path(f"{identifier(dash)}, public", transaction_isolation=True)

    # Upstream PostgreSQL started reporting search_path changes in version 18.
    version = int(execute("SHOW server_version_num;\n", backend)[0])
    if version >= 180000:
        for path, normalized in paths:
            check_path(path, runtime=True, normalized=normalized)

    # Unsupported or malformed input must never become another SQL statement.
    for path in ["public$; SELECT 1 AS x$", "public,", '"unterminated']:
        env = client.copy()
        env["PGOPTIONS"] = "-c search_path=" + path.replace("\\", "\\\\").replace(" ", "\\ ")
        result = subprocess.run(PSQL, input="SELECT 1;\n", env=env, text=True, capture_output=True, timeout=30)
        assert result.returncode != 0 and not result.stdout, (path, result.stdout, result.stderr)

    print(f"search_path restoration passed on PostgreSQL {version}")
finally:
    execute("\n".join(f"DROP SCHEMA {identifier(name)};" for name in schemas), backend)
