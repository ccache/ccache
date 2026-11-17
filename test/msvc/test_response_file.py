import shutil
import winreg

import pytest

ENCODINGS = [
    ("cl", "ascii"),
    ("cl", "system"),
    ("cl", "utf-8"),
    ("cl", "utf-8-sig"),
    ("cl", "utf-16"),
    ("clang-cl", "ascii"),
    ("clang-cl", "utf-8"),
    ("clang-cl", "utf-8-sig"),
    ("clang-cl", "utf-16"),
    ("clang-cl", "utf-16-be"),
]


@pytest.mark.parametrize(
    "compiler,encoding,option",
    [
        (compiler, encoding, option)
        for compiler, encoding in ENCODINGS
        for option in (["/Fo", "/Fo "] if compiler == "cl" else ["/Fo"])
    ],
)
def test_response_encoding_matches_compiler_and_is_cached(
    ccache_test, compiler, encoding, option
):
    executable = shutil.which(compiler)
    if not executable:
        pytest.skip(f"{compiler} not found in PATH")
    (ccache_test.workdir / "test.c").write_text("int x;\n", encoding="ascii")
    character = "\u00fc"
    if encoding == "system":
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SYSTEM\CurrentControlSet\Control\Nls\CodePage",
        ) as key:
            encoding = "cp" + winreg.QueryValueEx(key, "ACP")[0]
        for candidate in ("\u00fc", "\u3042", "\u4e2d", "\u044f", "\u00e9"):
            try:
                candidate.encode(encoding)
                character = candidate
                break
            except UnicodeEncodeError:
                continue
        else:
            pytest.skip(f"No test character available for {encoding}")
    name = "output.obj" if encoding == "ascii" else "out" + character + ".obj"
    content = ("/c /nologo test.c " + option + name).encode(encoding)
    if encoding == "utf-16-be":
        content = b"\xfe\xff" + content
    (ccache_test.workdir / "args.rsp").write_bytes(content)

    ccache_test.run([executable, "@args.rsp"])
    objects = list(ccache_test.workdir.glob("*.obj"))
    assert len(objects) == 1
    output = objects[0]
    output.unlink()

    command = [ccache_test.ccache_exe, executable, "@args.rsp"]
    ccache_test.run(command)
    assert output.exists()
    output.unlink()
    ccache_test.run(command)
    assert output.exists()
    stats = ccache_test.stats()
    assert stats["miss"] == 1
    assert stats["total_hit"] == 1


def test_utf16_response_file_preserves_control_z_byte(ccache_test):
    (ccache_test.workdir / "test.c").write_text("int x;\n", encoding="ascii")
    (ccache_test.workdir / "args.rsp").write_bytes(
        "/c /nologo test.c /Foout\u041a.obj".encode("utf-16")
    )
    ccache_test.compile(["@args.rsp"])
    (ccache_test.workdir / "out\u041a.obj").unlink()
    ccache_test.compile(["@args.rsp"])
    assert (ccache_test.workdir / "out\u041a.obj").exists()
    stats = ccache_test.stats()
    assert stats["miss"] == 1
    assert stats["total_hit"] == 1


@pytest.mark.parametrize("compiler", ["cl", "clang-cl"])
@pytest.mark.parametrize("encoding", ["utf-8-sig", "utf-16"])
def test_unicode_macro_value_is_preserved(ccache_test, compiler, encoding):
    executable = shutil.which(compiler)
    if not executable:
        pytest.skip(f"{compiler} not found in PATH")
    (ccache_test.workdir / "test.c").write_text(
        "int main(void) { return VALUE; }\n", encoding="ascii"
    )
    (ccache_test.workdir / "args.rsp").write_bytes(
        "/c /nologo test.c /Fooutput.obj /DVALUE=L'\u00fc'".encode(encoding)
    )
    for phase in ("direct", "miss", "hit"):
        command = [executable, "@args.rsp"]
        if phase != "direct":
            command.insert(0, ccache_test.ccache_exe)
        ccache_test.run(command)
        ccache_test.run([executable, "/nologo", "output.obj", f"/Fe{phase}.exe"])
        result = ccache_test.run(
            [str(ccache_test.workdir / f"{phase}.exe")], check=False
        )
        assert result.returncode == 252
        (ccache_test.workdir / "output.obj").unlink()
    stats = ccache_test.stats()
    assert stats["miss"] == 1
    assert stats["total_hit"] == 1


@pytest.mark.parametrize(
    "compiler,encoding", [("cl", "utf-16-be"), ("clang-cl", "cp1252")]
)
def test_invalid_response_encoding_preserves_compiler_failure(
    ccache_test, compiler, encoding
):
    executable = shutil.which(compiler)
    if not executable:
        pytest.skip(f"{compiler} not found in PATH")
    (ccache_test.workdir / "test.c").write_text("int x;\n", encoding="ascii")
    content = "/c /nologo test.c /Foout\u00fc.obj".encode(encoding)
    if encoding == "utf-16-be":
        content = b"\xfe\xff" + content
    (ccache_test.workdir / "args.rsp").write_bytes(content)
    direct = ccache_test.run([executable, "@args.rsp"], check=False)
    outputs = list(ccache_test.workdir.glob("*.obj"))
    for output in outputs:
        output.unlink()
    cached = ccache_test.run(
        [ccache_test.ccache_exe, executable, "@args.rsp"], check=False
    )
    assert cached.returncode == direct.returncode
    assert sorted(ccache_test.workdir.glob("*.obj")) == sorted(outputs)
