def test_basic_compilation(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")

    ccache_test.compile(["/c", "test.c", "/Fohello.obj"])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0

    ccache_test.compile(["/c", "test.c", "/Fohello.obj"])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert (ccache_test.workdir / "hello.obj").exists()

    ccache_test.compile(["/c", "test.c"])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 2
    assert (ccache_test.workdir / "test.obj").exists()


def test_define_change_is_miss(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int x = VALUE;\n")

    ccache_test.compile(["/c", "/DVALUE=1", "test.c"])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1

    ccache_test.compile(["/c", "/DVALUE=2", "test.c"])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 2
    assert stats_2["total_hit"] == 0


def test_header_change_is_miss(ccache_test):
    header = ccache_test.workdir / "test.h"
    header.write_text("#define VALUE 1\n")
    source = ccache_test.workdir / "test.c"
    source.write_text('#include "test.h"\nint x = VALUE;\n')

    ccache_test.compile(["/c", "test.c"])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0

    header.write_text("#define VALUE 2\n")
    ccache_test.compile(["/c", "test.c"])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 2
    assert stats_2["direct_hit"] == 0
    assert stats_2["total_hit"] == 0


def test_basedir_normalizes_paths(ccache_test):
    ccache_test.env["CCACHE_NOHASHDIR"] = "1"
    ccache_test.env["CCACHE_BASEDIR"] = str(ccache_test.workdir)

    dirs = []
    for name in ["dir1", "dir2"]:
        d = ccache_test.workdir / name
        d.mkdir()
        (d / "test.c").write_text("int x;\n")
        dirs.append(d)

    ccache_test.compile(["/c", "test.c"], cwd=dirs[0])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1

    ccache_test.compile(["/c", "test.c"], cwd=dirs[1])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1


def test_basedir_normalizes_msvc_environment_paths(ccache_test):
    ccache_test.env["CCACHE_NOHASHDIR"] = "1"
    ccache_test.env["CCACHE_BASEDIR"] = str(ccache_test.workdir)

    original_include = ccache_test.env.get("INCLUDE")
    original_external_include = ccache_test.env.get("EXTERNAL_INCLUDE")

    dirs = []
    for name in ["dir1", "dir2"]:
        d = ccache_test.workdir / name
        d.mkdir()

        for subdir in ["include1", "include2", "external1", "external2", "toolchain"]:
            (d / subdir).mkdir()

        (d / "include2" / "test.h").write_text("#define VALUE 42\n")
        (d / "test.c").write_text("#include <test.h>\nint x = VALUE;\n")
        dirs.append(d)

    def set_msvc_environment(d):
        ccache_test.env["VCToolsInstallDir"] = str(d / "toolchain")

        include = [str(d / "include1"), str(d / "include2")]
        if original_include:
            include.append(original_include)
        ccache_test.env["INCLUDE"] = ";".join(include)

        external_include = [str(d / "external1"), str(d / "external2")]
        if original_external_include:
            external_include.append(original_external_include)
        ccache_test.env["EXTERNAL_INCLUDE"] = ";".join(external_include)

    set_msvc_environment(dirs[0])
    ccache_test.compile(["/c", "test.c"], cwd=dirs[0])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0

    set_msvc_environment(dirs[1])
    ccache_test.compile(["/c", "test.c"], cwd=dirs[1])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["direct_hit"] == 1
    assert stats_2["total_hit"] == 1
