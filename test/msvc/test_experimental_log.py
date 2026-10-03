def test_experimental_log_with_attached_filename(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    sarif = ccache_test.workdir / "result.sarif"

    ccache_test.compile(["/c", "test.c", "/Fooutput.obj", "/experimental:logresult"])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    expected = sarif.read_bytes()

    sarif.unlink()

    ccache_test.compile(["/c", "test.c", "/Fooutput.obj", "/experimental:logresult"])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert sarif.read_bytes() == expected


def test_experimental_log_with_separate_directory(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    output_dir = ccache_test.workdir / "sarif_output"
    output_dir.mkdir()
    sarif = output_dir / "test.sarif"
    cl_args = [
        "/c",
        "test.c",
        "/Fooutput.obj",
        "/experimental:log",
        "sarif_output\\",
    ]

    ccache_test.compile(cl_args)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    expected = sarif.read_bytes()

    sarif.unlink()

    ccache_test.compile(cl_args)
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert sarif.read_bytes() == expected
