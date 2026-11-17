def test_file_clone_restored_object_is_newer_than_source(ccache_test):
    ccache_test.env["CCACHE_FILECLONE"] = "1"
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    object_file = ccache_test.workdir / "test.obj"

    ccache_test.compile(["/c", "test.c"])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1

    # As after a checkout: the source gets a newer time than the cached object.
    source.touch()
    ccache_test.compile(["/c", "test.c"])
    stats_2 = ccache_test.stats()
    assert stats_2["total_hit"] == 1
    assert object_file.stat().st_mtime_ns >= source.stat().st_mtime_ns
