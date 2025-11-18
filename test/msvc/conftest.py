"""
pytest configuration and shared fixtures for MSVC integration tests.
"""

import json
import os
import shutil
import tempfile
from pathlib import Path
from subprocess import PIPE, CompletedProcess
from subprocess import run as sp_run
from typing import Optional

import pytest


class CcacheTest:
    def __init__(self, ccache_exe: Path):
        self.ccache_exe = ccache_exe
        self.tmpdir = Path(tempfile.gettempdir())
        self.cache_dir = None
        self.log_file = None
        self.workdir = None
        self.env = None

    def __enter__(self):
        self.cache_dir = Path(tempfile.mkdtemp(prefix="ccache_", dir=self.tmpdir))
        self.workdir = Path(tempfile.mkdtemp(prefix="work_", dir=self.tmpdir))
        log_dir = tempfile.mkdtemp(prefix="log_", dir=self.tmpdir)
        self.log_file = Path(log_dir) / "ccache.log"

        self.env = os.environ.copy()
        self.env["CCACHE_DIR"] = str(self.cache_dir)
        self.env["CCACHE_LOGFILE"] = str(self.log_file)

        self.reset_stats()

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self.cache_dir
        assert self.workdir
        assert self.log_file

        # Print log on failure for debugging
        if exc_type is not None and self.log_file.exists():
            print(f"\n--- CCACHE_LOGFILE content ({self.log_file}) ---")
            print(self.log_file.read_text(errors="replace"))
            print("--- End of CCACHE_LOGFILE ---\n")

        shutil.rmtree(self.cache_dir, ignore_errors=True)
        shutil.rmtree(self.workdir, ignore_errors=True)
        shutil.rmtree(self.log_file.parent, ignore_errors=True)

    def _run(
        self, args: list[str], *, cwd: Optional[Path] = None, check: bool = True
    ) -> CompletedProcess:
        return sp_run(
            args,
            cwd=cwd or self.workdir,
            env=self.env,
            stdout=PIPE,
            stderr=PIPE,
            text=True,
            check=check,
        )

    def run(
        self, args: list[str], *, cwd: Optional[Path] = None, check: bool = True
    ) -> CompletedProcess:
        return self._run(args, cwd=cwd, check=check)

    def compile(self, cl_args: list, *, cwd: Optional[Path] = None) -> CompletedProcess:
        """Compile with ccache + cl."""
        cmd = [self.ccache_exe, "cl", *cl_args]
        return self._run(cmd, cwd=cwd)

    def stats(self) -> dict[str, int]:
        result = self._run([self.ccache_exe, "--print-stats", "--format", "json"])
        stats_data = json.loads(result.stdout)

        direct_hit = stats_data.get("direct_cache_hit", 0)
        preprocessed_hit = stats_data.get("preprocessed_cache_hit", 0)
        miss = stats_data.get("cache_miss", 0)

        return {
            "direct_hit": direct_hit,
            "preprocessed_hit": preprocessed_hit,
            "miss": miss,
            "total_hit": direct_hit + preprocessed_hit,
        }

    def reset_stats(self) -> None:
        self._run([self.ccache_exe, "-z"])


def pytest_addoption(parser):
    parser.addoption(
        "--ccache", action="store", help="Path to ccache.exe", required=True
    )


@pytest.fixture(scope="session")
def ccache_exe(request):
    ccache_path = request.config.getoption("--ccache")
    ccache_exe = Path(ccache_path).resolve()
    if not ccache_exe.exists():
        pytest.fail(f"ccache.exe not found at {ccache_exe}")
    return ccache_exe


@pytest.fixture(scope="session")
def verify_cl_available():
    cl = shutil.which("cl")
    if not cl:
        pytest.fail("cl.exe not found in PATH")
    return cl


@pytest.fixture
def ccache_test(ccache_exe, verify_cl_available):  # noqa: ARG001
    with CcacheTest(ccache_exe) as test:
        yield test
