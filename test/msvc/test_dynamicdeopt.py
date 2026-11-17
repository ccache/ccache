import os
from pathlib import Path


def test_deoptimized_object_is_restored(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    object_file = ccache_test.workdir / "test.obj"
    alt_object = ccache_test.workdir / "test.alt.obj"
    alt_object.write_bytes(b"stale")
    cl_args = ["/c", "/Z7", "/dynamicdeopt", "test.c"]

    ccache_test.compile(cl_args)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    expected = alt_object.read_bytes()

    object_file.unlink()
    alt_object.write_bytes(b"stale")

    ccache_test.compile(cl_args)
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert object_file.exists()
    assert alt_object.read_bytes() == expected


def test_deoptimized_object_name_follows_suffix_and_object_name(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    object_file = ccache_test.workdir / "output.c.obj"
    alt_object = ccache_test.workdir / "output.c.dd.obj"
    cl_args = [
        "/c",
        "/Z7",
        "/dynamicdeopt",
        "/dynamicdeopt:suffix",
        ".dd",
        "/Fooutput.c.obj",
        "test.c",
    ]

    ccache_test.compile(cl_args)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    expected = alt_object.read_bytes()

    object_file.unlink()
    alt_object.unlink()

    ccache_test.compile(cl_args)
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert object_file.exists()
    assert alt_object.read_bytes() == expected


def test_deoptimized_object_is_left_alone_if_compiler_ignores_option(ccache_test):
    cl_x86 = Path(os.environ["VCToolsInstallDir"], "bin", "Hostx64", "x86", "cl.exe")
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    alt_object = ccache_test.workdir / "test.alt.obj"
    alt_object.write_bytes(b"old")
    command = [ccache_test.ccache_exe, cl_x86, "/c", "/Z7", "/dynamicdeopt", "test.c"]

    ccache_test.run(command)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    assert alt_object.read_bytes() == b"old"

    alt_object.unlink()

    ccache_test.run(command)
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert not alt_object.exists()
