import shutil

import pytest

from conftest import CcacheTest


def test_clang_cl_pch_with_argument_terminator(ccache_exe):
    compiler = shutil.which("clang-cl")
    if not compiler:
        pytest.skip("clang-cl not found in PATH")

    with CcacheTest(ccache_exe) as test:
        test.env["CCACHE_SLOPPINESS"] = "pch_defines,time_macros"
        (test.workdir / "test.h").write_text("int x;\n")
        (test.workdir / "test.cpp").write_text('#include "test.h"\n')
        command = [
            ccache_exe,
            compiler,
            "/c",
            "/Yctest.h",
            "/Fptest.pch",
            "--",
            "test.cpp",
        ]

        test.run(command)
        assert test.stats()["miss"] == 1
        assert test.stats()["total_hit"] == 0
        for name in ["test.obj", "test.pch"]:
            (test.workdir / name).unlink()

        test.run(command)
        assert test.stats()["miss"] == 1
        assert test.stats()["total_hit"] == 1
        assert (test.workdir / "test.obj").exists()
        assert (test.workdir / "test.pch").exists()
