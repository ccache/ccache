def test_assembler_listing_is_restored_and_output_name_is_not_hashed(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    object_file = ccache_test.workdir / "output.obj"
    first_listing = ccache_test.workdir / "first.asm"
    second_listing = ccache_test.workdir / "second.asm"

    ccache_test.compile(["/c", "test.c", "/Fooutput.obj", "/FA", "/Fafirst.asm"])
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    expected = first_listing.read_bytes()

    object_file.unlink()
    first_listing.unlink()

    ccache_test.compile(["/c", "test.c", "/Fooutput.obj", "/FA", "/Fafirst.asm"])
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert object_file.exists()
    assert first_listing.read_bytes() == expected

    object_file.unlink()
    first_listing.unlink()

    ccache_test.compile(["/c", "test.c", "/Fooutput.obj", "/FA", "/Fasecond.asm"])
    stats_3 = ccache_test.stats()
    assert stats_3["miss"] == 1
    assert stats_3["total_hit"] == 2
    assert object_file.exists()
    assert second_listing.read_bytes() == expected


def test_machine_code_listing_uses_cod_extension(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text("int main() {}\n")
    object_file = ccache_test.workdir / "output.obj"
    listing = ccache_test.workdir / "test.cod"
    cl_args = ["/c", "test.c", "/Fooutput.obj", "/FAc"]

    ccache_test.compile(cl_args)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0
    expected = listing.read_bytes()

    object_file.unlink()
    listing.unlink()

    ccache_test.compile(cl_args)
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["total_hit"] == 1
    assert object_file.exists()
    assert listing.read_bytes() == expected
