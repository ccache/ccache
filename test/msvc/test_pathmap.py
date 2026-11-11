import json
from pathlib import Path

import pytest

SOURCE = """#include "h.h"
const char* file = __FILE__;
int f(void) { int unused; return h(); }
"""

# With /Z7, an object with a class type holds the CWD ("predefined C++ types").
CPP_SOURCE = """#include <string>
#include "h.h"
struct B { virtual ~B() {} };
struct D : B { std::string s; };
int f() { D d; return (int)d.s.size() + h(); }
"""


def make_tree(parent: Path) -> Path:
    root = parent / "root"
    (root / "src").mkdir(parents=True)
    (root / "build" / "obj").mkdir(parents=True)
    (root / "src" / "h.h").write_text("int h(void) { return 1; }\n")
    (root / "src" / "test.c").write_text(SOURCE)
    (root / "src" / "test.cpp").write_text(CPP_SOURCE)
    # Found through INCLUDE or EXTERNAL_INCLUDE.
    (root / "inc").mkdir()
    (root / "inc" / "h2.h").write_text("static const char* h2_file = __FILE__;\n")
    (root / "src" / "inc.c").write_text(
        "#include <h2.h>\nconst char* g(void) { return h2_file; }\n"
    )
    return root


@pytest.fixture
def roots(ccache_test) -> list[Path]:
    # Two source trees at different depths.
    return [
        make_tree(ccache_test.workdir / "a"),
        make_tree(ccache_test.workdir / "b" / "c" / "d"),
    ]


def root_maps(root: Path, new_path: str = "", option: str = "/pathmap:") -> list[str]:
    # cl maps a path only with the spelling of its separators.
    back = str(root).replace("/", "\\")
    forward = back.replace("\\", "/")
    return [f"{option}{back}\\={new_path}", f"{option}{forward}/={new_path}"]


def compile_in(
    ccache_test,
    root: Path,
    args: list[str],
    *,
    maps: list[str] | None = None,
    cwd: Path | None = None,
    obj: str = "test.obj",
    source: str = "test.c",
):
    ccache_test.env["CCACHE_BASEDIR"] = str(root)
    maps = root_maps(root) if maps is None else maps
    cwd = root / "build" if cwd is None else cwd
    source = (
        f"..\\src\\{source}" if cwd.parent == root else str(root / "src" / source)
    )
    return ccache_test.compile(
        ["/c", "/Z7", "/experimental:deterministic", *maps, *args, source]
        + [f"/Fo{obj}"],
        cwd=cwd,
    )


def stats(ccache_test) -> dict[str, int]:
    result = ccache_test.run(
        [ccache_test.ccache_exe, "--print-stats", "--format", "json"]
    )
    return json.loads(result.stdout)


def assert_not_shared(ccache_test):
    stats = ccache_test.stats()
    assert stats["miss"] == 2
    assert stats["total_hit"] == 0


@pytest.mark.parametrize(
    "args, options",
    [
        pytest.param([], {}, id="plain"),
        pytest.param(["/W4"], {}, id="warning"),
        pytest.param(["/FC", "/W4"], {}, id="FC"),
        pytest.param(["/FS", "/MP"], {}, id="FS_MP"),
        pytest.param(["/showIncludes"], {}, id="showIncludes"),
        pytest.param(["/EHsc"], {"source": "test.cpp"}, id="cpp"),
        pytest.param([], {"obj": "obj\\test.obj"}, id="subdirectory"),
        pytest.param(["/FA"], {"outputs": ["test.asm"]}, id="FA"),
        pytest.param(["/showIncludes"], {"depend": True}, id="depend"),
        pytest.param([], {"option": "-pathmap:"}, id="dash"),
    ],
)
def test_shares(ccache_test, roots, args, options):
    obj = options.get("obj", "test.obj")
    outputs = [obj, *options.get("outputs", [])]
    if options.get("depend"):
        ccache_test.env["CCACHE_DEPEND"] = "1"

    def run(root):
        maps = root_maps(root, option=options.get("option", "/pathmap:"))
        return compile_in(
            ccache_test,
            root,
            args,
            maps=maps,
            obj=obj,
            source=options.get("source", "test.c"),
        )

    run(roots[0])
    hit = run(roots[1])
    stats = ccache_test.stats()
    assert stats["miss"] == 1
    assert stats["total_hit"] == 1
    build = roots[1] / "build"
    hit_outputs = {name: (build / name).read_bytes() for name in outputs}

    # The oracle: the same command in the second tree without the cache.
    ccache_test.env["CCACHE_RECACHE"] = "1"
    oracle = run(roots[1])
    del ccache_test.env["CCACHE_RECACHE"]
    assert hit.stdout == oracle.stdout
    for name in outputs:
        assert hit_outputs[name] == (build / name).read_bytes(), name


@pytest.mark.parametrize(
    "new_paths, build_dirs, hit",
    [
        (("", ""), ("build", "build"), True),
        (("X:\\", "Y:\\"), ("build", "build"), False),
        (("", ""), ("build", "build2"), True),
    ],
    ids=["same", "different", "other_build_dir"],
)
def test_nohashdir_hashes_new_path(ccache_test, roots, new_paths, build_dirs, hit):
    ccache_test.env["CCACHE_NOHASHDIR"] = "1"
    for root, new_path, build in zip(roots, new_paths, build_dirs):
        (root / build).mkdir(exist_ok=True)
        compile_in(
            ccache_test, root, [], maps=root_maps(root, new_path), cwd=root / build
        )
    assert ccache_test.stats()["total_hit"] == (1 if hit else 0)


def test_sibling_build_dirs_are_not_shared(ccache_test, roots):
    for root, build in zip(roots, ["build1", "build2"]):
        (root / build).mkdir()
        ccache_test.env["CCACHE_BASEDIR"] = str(root)
        ccache_test.compile(
            ["/c", "/Z7", "/experimental:deterministic", *root_maps(root)]
            + ["..\\src\\test.c"],
            cwd=root / build,
        )
    assert_not_shared(ccache_test)


@pytest.mark.parametrize(
    "runs",
    [
        [("build1", "..\\build\\obj\\t.obj"), ("build2", "..\\build\\obj\\t.obj")],
        [("build", "obj1\\t.obj"), ("build", "obj2\\t.obj")],
    ],
    ids=["cwd", "object_directory"],
)
def test_one_tree_different_directory_is_not_shared(ccache_test, roots, runs):
    root = roots[0]
    for build, obj in runs:
        (root / build).mkdir(exist_ok=True)
        (root / build / obj).parent.mkdir(exist_ok=True)
        compile_in(
            ccache_test, root, ["/EHsc"], cwd=root / build, obj=obj, source="test.cpp"
        )
    assert_not_shared(ccache_test)


@pytest.mark.parametrize("old_path", ["other", "root"])
def test_skipped_pathmap_is_hashed(ccache_test, roots, old_path):
    old = roots[0] if old_path == "root" else ccache_test.workdir / "other"
    for new_path in ["X", "Y"]:
        compile_in(
            ccache_test, roots[0], ["--ccache-skip", f"/pathmap:{old}\\={new_path}"]
        )
    assert_not_shared(ccache_test)


def test_output_naming_root_is_not_stored(ccache_test, roots):
    # ccache doesn't recognize the /showIncludes lines with another prefix, so
    # their absolute paths stay in stdout.
    ccache_test.env["CCACHE_MSVC_DEP_PREFIX"] = "Something else:"
    args = ["/FC", "/W4", "/showIncludes"]
    result = compile_in(ccache_test, roots[0], args)
    compile_in(ccache_test, roots[1], args)
    assert_not_shared(ccache_test)
    log = ccache_test.log_file.read_text()
    assert "Not storing the result" in log
    assert "falling back" not in log
    assert result.stdout.count("warning C4101") == 1
    assert "Note: including file" in result.stdout
    assert (roots[0] / "build" / "test.obj").exists()
    compile_in(ccache_test, roots[0], args)
    assert ccache_test.stats()["miss"] == 3
    ccache_test.env["CCACHE_RECACHE"] = "1"
    compile_in(ccache_test, roots[0], args)
    assert stats(ccache_test).get("recache", 0) == 1


def test_not_relocated_without_deterministic(ccache_test, roots):
    for root in roots:
        ccache_test.env["CCACHE_BASEDIR"] = str(root)
        ccache_test.compile(
            ["/c", "/Z7", *root_maps(root), "..\\src\\test.c", "/Fotest.obj"],
            cwd=root / "build",
        )
    assert_not_shared(ccache_test)


def test_not_relocated_with_one_spelling(ccache_test, roots):
    for root in roots:
        compile_in(ccache_test, root, [], maps=root_maps(root)[:1])
    assert_not_shared(ccache_test)


@pytest.mark.parametrize(
    "variable, spelling, shared",
    [
        ("INCLUDE", "{root}\\inc", False),
        ("INCLUDE", "{parent}\\x\\..\\{name}\\inc", False),
        ("INCLUDE", "{parent}\\\\{name}\\inc", False),
        ("INCLUDE", "{root}\\inc", True),
        ("EXTERNAL_INCLUDE", "{root}\\inc", True),
    ],
    ids=["plain", "dotdot", "double_separator", "shared", "external_shared"],
)
def test_not_relocated_with_include_variable_under_root(
    ccache_test, roots, variable, spelling, shared
):
    # A shared entry into the first tree would give the second tree a hit that
    # names the first tree's header as the second tree's.
    previous = ccache_test.env.get(variable, "")
    # Toolset 14.51 searches EXTERNAL_INCLUDE only with an /external:W option.
    args = ["/external:W0"] if variable == "EXTERNAL_INCLUDE" else []
    for root in roots:
        target = roots[0] if shared else root
        entry = spelling.format(root=target, parent=target.parent, name=target.name)
        ccache_test.env[variable] = f"{entry};{previous}"
        compile_in(ccache_test, root, args, source="inc.c")
    assert_not_shared(ccache_test)


def test_not_relocated_with_cwd_outside_root(ccache_test, roots):
    for root in roots:
        obj = str(root / "build" / "test.obj")
        compile_in(ccache_test, root, [], cwd=root.parent, obj=obj)
    assert_not_shared(ccache_test)


def test_not_relocated_with_pch(ccache_test, roots):
    ccache_test.env["CCACHE_SLOPPINESS"] = "pch_defines,time_macros"
    for root in roots:
        compile_in(ccache_test, root, ["/Ych.h", "/Fppch.pch"], obj="pch.obj")
    ccache_test.reset_stats()
    for root in roots:
        compile_in(ccache_test, root, ["/Yuh.h", "/Fppch.pch"])
    assert_not_shared(ccache_test)
