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


def test_source_dependencies_output(ccache_test):
    command = ["/c", "test.c", "/sourceDependencies", "sourceDependencies.json"]

    header = ccache_test.workdir / "myheader.h"
    header.write_text(
        """
#define GREETING "Hello from header"
"""
    )

    source = ccache_test.workdir / "test.c"
    source.write_text(
        """
#include "myheader.h"
#include <stdio.h>
int main(void) {
    printf(GREETING "\\n");
    return 0;
}
"""
    )

    ccache_test.compile(command)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1

    source_deps = ccache_test.workdir / "sourceDependencies.json"
    test_obj = ccache_test.workdir / "test.obj"
    assert source_deps.exists()
    test_obj.unlink()
    source_deps.unlink()

    ccache_test.compile(command)
    stats_2 = ccache_test.stats()
    assert stats_2["total_hit"] == 1
    assert test_obj.exists()
    assert source_deps.exists()


def test_preprocessor_mode_detects_header_changes(ccache_test):
    # Force preprocessor mode.
    ccache_test.env["CCACHE_NODEPEND"] = "1"
    ccache_test.env["CCACHE_NODIRECT"] = "1"

    command = ["/c", "test.c"]

    header = ccache_test.workdir / "test.h"
    header.write_text("int x;\n")

    source = ccache_test.workdir / "test.c"
    source.write_text('#include "test.h"\n')

    test_obj = ccache_test.workdir / "test.obj"

    ccache_test.compile(command)
    stats_1 = ccache_test.stats()
    assert stats_1["total_hit"] == 0
    assert stats_1["miss"] == 1
    test_obj.unlink()

    ccache_test.compile(command)
    stats_2 = ccache_test.stats()
    assert stats_2["total_hit"] == 1
    assert stats_2["miss"] == 1
    test_obj.unlink()

    header.write_text("int y;\n")

    ccache_test.compile(command)
    stats_3 = ccache_test.stats()
    assert stats_3["miss"] == 2
    assert stats_3["total_hit"] == 1

    ccache_test.compile(command)
    stats_4 = ccache_test.stats()
    assert stats_4["miss"] == 2
    assert stats_4["total_hit"] == 2


def test_depend_mode_with_source_dependencies(ccache_test):
    ccache_test.env["CCACHE_DEPEND"] = "1"

    command = ["/c", "test.c"]

    header = ccache_test.workdir / "test.h"
    header.write_text('#define VERSION "1.0"\n')

    # Create source that includes the header
    source = ccache_test.workdir / "test.c"
    source.write_text("""\
#include "test.h"
#include <stdio.h>
int main(void) {
  printf("Version: " VERSION "\\n");
  return 0;
}
""")

    test_obj = ccache_test.workdir / "test.obj"

    ccache_test.compile(command)
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    test_obj.unlink()

    ccache_test.compile(command)
    stats_2 = ccache_test.stats()
    assert stats_2["total_hit"] == 1
    test_obj.unlink()

    header.write_text('#define VERSION "2.0"\n')

    ccache_test.compile(command)
    stats_3 = ccache_test.stats()
    assert stats_3["miss"] == 2
    assert stats_3["total_hit"] == 1

    ccache_test.compile(command)
    stats_4 = ccache_test.stats()
    assert stats_4["miss"] == 2
    assert stats_4["total_hit"] == 2
