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
