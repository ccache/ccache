INTERFACE = """\
export module m;
export inline int value() { return %d; }
"""

IMPORTER = """\
import m;
int main() { return value(); }
"""


def test_importer_depends_on_ifc_in_current_directory(ccache_test):
    (ccache_test.workdir / "main.cpp").write_text(IMPORTER)
    # Without /ifcOutput, MSVC writes m.ifc to the current directory, where an
    # import without /reference finds it.
    interface_args = ["cl", "/nologo", "/std:c++20", "/c", "m.ixx"]
    importer_args = ["/nologo", "/std:c++20", "/c", "main.cpp"]

    (ccache_test.workdir / "m.ixx").write_text(INTERFACE % 1)
    ccache_test.run(interface_args)
    ccache_test.compile(importer_args)
    ccache_test.compile(importer_args)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 1

    (ccache_test.workdir / "m.ixx").write_text(INTERFACE % 2)
    ccache_test.run(interface_args)
    ccache_test.compile(importer_args)
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 2
    assert stats_2["total_hit"] == 1

    ccache_test.run(["link", "/nologo", "main.obj", "m.obj", "/out:main.exe"])
    result = ccache_test.run([str(ccache_test.workdir / "main.exe")], check=False)
    assert result.returncode == 2
