import json

import pytest


@pytest.mark.parametrize("option", [b"/Foa\xfc.obj", b"/Fo a\xfc.obj"])
def test_non_utf8_path_falls_back_to_compiler(ccache_test, option):
    source = ccache_test.workdir / "test.c"
    source.write_text("int x;\n")
    response = ccache_test.workdir / "args.rsp"
    response.write_bytes(b"/c /nologo test.c " + option)

    # The original compiler must accept the response file without ccache.
    ccache_test.run(["cl", "@args.rsp"])
    objects = list(ccache_test.workdir.glob("*.obj"))
    assert len(objects) == 1
    output = objects[0]
    output.unlink()

    ccache_test.compile(["@args.rsp"])

    assert output.exists()
    stats = json.loads(
        ccache_test.run(
            [ccache_test.ccache_exe, "--print-stats", "--format", "json"]
        ).stdout
    )
    assert stats["bad_compiler_arguments"] == 1
    assert stats["cache_miss"] == 0


def test_ascii_response_file_is_cached(ccache_test):
    (ccache_test.workdir / "test.c").write_text("int x;\n")
    (ccache_test.workdir / "args.rsp").write_bytes(b"/c /nologo test.c /Fooutput.obj")

    ccache_test.compile(["@args.rsp"])
    (ccache_test.workdir / "output.obj").unlink()
    ccache_test.compile(["@args.rsp"])

    assert (ccache_test.workdir / "output.obj").exists()
    stats = ccache_test.stats()
    assert stats["miss"] == 1
    assert stats["total_hit"] == 1
