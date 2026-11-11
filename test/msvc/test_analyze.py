import pytest

ANALYZE = ["/analyze", "/analyze:autolog-"]

# Reading x is C6001 (using uninitialized memory) in the code analysis.
UNINITIALIZED = "int f(void) { int x; return x; }\n"


def test_analysis_warnings_are_replayed(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text(UNINITIALIZED)

    first = ccache_test.compile([*ANALYZE, "/c", "test.c"])
    assert "C6001" in first.stdout
    stats_1 = ccache_test.stats()
    assert stats_1["miss"] == 1
    assert stats_1["total_hit"] == 0

    second = ccache_test.compile([*ANALYZE, "/c", "test.c"])
    assert "C6001" in second.stdout
    stats_2 = ccache_test.stats()
    assert stats_2["miss"] == 1
    assert stats_2["direct_hit"] == 1
    assert not list(ccache_test.workdir.glob("*.nativecodeanalysis.xml"))


def test_analysis_with_log_file_is_not_cached(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text(UNINITIALIZED)

    ccache_test.compile(["/analyze", "/c", "test.c"])
    ccache_test.compile(["/analyze", "/c", "test.c"])
    stats = ccache_test.stats()
    assert stats["miss"] == 0
    assert stats["total_hit"] == 0
    assert (ccache_test.workdir / "test.nativecodeanalysis.xml").exists()


def test_analysis_environment_is_hashed(ccache_test):
    source = ccache_test.workdir / "test.c"
    source.write_text(UNINITIALIZED)

    ccache_test.env["CAExcludePath"] = str(ccache_test.workdir / "a")
    ccache_test.compile([*ANALYZE, "/c", "test.c"])
    ccache_test.env["CAExcludePath"] = str(ccache_test.workdir / "b")
    ccache_test.compile([*ANALYZE, "/c", "test.c"])
    stats = ccache_test.stats()
    assert stats["miss"] == 2
    assert stats["total_hit"] == 0


@pytest.mark.parametrize("mode", [{}, {"CCACHE_NODIRECT": "1"}, {"CCACHE_DEPEND": "1"}])
@pytest.mark.parametrize("macro", ["_PREFAST_", "_AST_FE_"])
def test_header_only_included_for_analysis_is_tracked(ccache_test, mode, macro):
    # The analysis pass defines _PREFAST_ and _AST_FE_; the normal compilation
    # doesn't.
    ccache_test.env.update(mode)
    header = ccache_test.workdir / "suppress.h"
    header.write_text("#pragma warning(disable: 6001)\n")
    source = ccache_test.workdir / "test.c"
    source.write_text(
        f'#ifdef {macro}\n#include "suppress.h"\n#endif\n' + UNINITIALIZED
    )

    first = ccache_test.compile([*ANALYZE, "/c", "test.c"])
    assert "C6001" not in first.stdout

    header.write_text("\n")
    second = ccache_test.compile([*ANALYZE, "/c", "test.c"])
    assert "C6001" in second.stdout
    stats = ccache_test.stats()
    assert stats["miss"] == 2
    assert stats["total_hit"] == 0


def test_user_definition_of_prefast_wins(ccache_test):
    # cl defines _PREFAST_ ahead of the other arguments.
    source = ccache_test.workdir / "test.c"
    source.write_text(
        "#if _PREFAST_ == 2\n#pragma warning(disable: 6001)\n#endif\n" + UNINITIALIZED
    )

    result = ccache_test.compile([*ANALYZE, "/D_PREFAST_=2", "/c", "test.c"])
    assert "C6001" not in result.stdout


def test_prefix_command_cpp(ccache_test):
    # The analysis variant must get all arguments, also with a prefix.
    ccache_test.env["CCACHE_PREFIX_CPP"] = "cmd /c"
    source = ccache_test.workdir / "test.c"
    source.write_text(
        "#if defined(_PREFAST_) && X == 1\n#pragma warning(disable: 6001)\n#endif\n"
        + UNINITIALIZED
    )

    ccache_test.compile([*ANALYZE, "/c", "test.c", "-DX=1", "-DY=0"])
    second = ccache_test.compile([*ANALYZE, "/c", "test.c", "-DX=2", "-DY=0"])
    assert "C6001" in second.stdout
    stats = ccache_test.stats()
    assert stats["miss"] == 2
    assert stats["total_hit"] == 0


def test_non_utf8_header_only_included_for_analysis(ccache_test):
    header = ccache_test.workdir / "latin1.h"
    header.write_bytes(b'const char* text = "\xe4";\n')
    source = ccache_test.workdir / "test.c"
    source.write_text('#ifdef _PREFAST_\n#include "latin1.h"\n#endif\n')

    ccache_test.env["CCACHE_NODIRECT"] = "1"
    ccache_test.compile([*ANALYZE, "/c", "test.c"])
    stats = ccache_test.stats()
    assert stats["miss"] == 0
    assert stats["total_hit"] == 0
