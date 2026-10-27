"""Tests of meta-aurora's boot image helpers (recipes-bsp/aurora-boot-images/files):
aurora-split-dtbs.py (split concatenated DTBs) and aurora-vkb-assemble.py (vendor_kernel_boot ramdisk from the
module manifest, with the module_layout KMI CRC gate). All inputs are synthetic files in a temporary directory."""
import importlib.util
import struct
import sys
from pathlib import Path

import pytest

FILES = Path(__file__).resolve().parents[2] / "meta-aurora" / "recipes-bsp" / "aurora-boot-images" / "files"


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), FILES / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


split = load("aurora-split-dtbs")
vkb = load("aurora-vkb-assemble")

CRC = 0x1234ABCD


def fdt(total, fill=b"\x01"):
    """A fake flattened device tree: the header magic and total size, then filler up to total bytes."""
    return struct.pack(">II", split.FDT_MAGIC, total) + fill * (total - 8)


def ko(path, crc=CRC, extra=b""):
    """A fake .ko: the CRC (u32 LE) + 4 bytes of padding right before "module_layout\\0", as the gate reads it."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x7fELF" + b"\0" * 12 + struct.pack("<I", crc) + b"\0" * 4 + vkb.MOD_LAYOUT_SYM + extra)
    return path


def run_main(mod, argv, monkeypatch):
    monkeypatch.setattr(sys, "argv", [mod.__name__] + [str(a) for a in argv])
    mod.main()


# ---- aurora-split-dtbs.py ----

def test_split_two_trees_with_padding(tmp_path, monkeypatch, capsys):
    blob = fdt(64) + b"\0" * 7 + fdt(48, b"\x02") + b"\0" * 3
    src = tmp_path / "in.dtb"
    src.write_bytes(blob)
    run_main(split, [src, tmp_path / "out"], monkeypatch)
    assert capsys.readouterr().out.strip() == "2"
    assert (tmp_path / "out" / "dtb-0.dtb").read_bytes() == fdt(64)
    assert (tmp_path / "out" / "dtb-1.dtb").read_bytes() == fdt(48, b"\x02")


@pytest.mark.parametrize("blob", [
    b"\xde\xad\xbe\xef" + struct.pack(">I", 64) + b"\0" * 56,  # wrong magic
    struct.pack(">II", 0xD00DFEED, 16) + b"\0" * 8,              # total below the header size
    fdt(64)[:40],                                                # truncated tree
])
def test_split_rejects_bad_headers(tmp_path, monkeypatch, blob):
    src = tmp_path / "in.dtb"
    src.write_bytes(blob)
    with pytest.raises(SystemExit) as e:
        run_main(split, [src, tmp_path / "out"], monkeypatch)
    assert "bad FDT header" in str(e.value)


def test_split_empty_or_tail_is_no_dtb(tmp_path, monkeypatch):
    src = tmp_path / "in.dtb"
    src.write_bytes(b"\0" * 4)  # shorter than a header
    with pytest.raises(SystemExit) as e:
        run_main(split, [src, tmp_path / "out"], monkeypatch)
    assert "no DTB found" in str(e.value)


def test_split_ignores_a_short_tail(tmp_path, monkeypatch, capsys):
    src = tmp_path / "in.dtb"
    src.write_bytes(fdt(40) + b"\x05\x06")  # 2 stray bytes: too short for another header
    run_main(split, [src, tmp_path / "out"], monkeypatch)
    assert capsys.readouterr().out.strip() == "1"


# ---- aurora-vkb-assemble.py: helpers ----

def test_parse_manifest(tmp_path):
    m = tmp_path / "m.lst"
    m.write_text("# comment\n\nfoo:k\nbar_baz : tp   # trailing comment\n@drop old_mod\n@dep foo.ko: bar_baz.ko x.ko\n")
    assert list(vkb.parse_manifest(m)) == [
        ("module", ("foo", "k")),
        ("module", ("bar_baz", "tp")),
        ("drop", "old_mod"),
        ("dep", ("foo.ko", "bar_baz.ko x.ko")),
    ]


@pytest.mark.parametrize("line,msg", [
    ("foo\n", "missing src tag"),
    ("foo:x\n", "must be 'k' or 'tp'"),
    ("@dep foo.ko bar.ko\n", "@dep missing ':'"),
])
def test_parse_manifest_errors(tmp_path, line, msg):
    m = tmp_path / "m.lst"
    m.write_text(line)
    with pytest.raises(SystemExit) as e:
        list(vkb.parse_manifest(m))
    assert msg in str(e.value) and "manifest:1" in str(e.value)


def test_module_layout_crc(tmp_path):
    assert vkb.module_layout_crc(ko(tmp_path / "a.ko", 0xDEADBEEF)) == 0xDEADBEEF
    (tmp_path / "b.ko").write_bytes(b"no symbol here")
    assert vkb.module_layout_crc(tmp_path / "b.ko") is None
    (tmp_path / "c.ko").write_bytes(b"abc" + vkb.MOD_LAYOUT_SYM)  # symbol too close to the start
    assert vkb.module_layout_crc(tmp_path / "c.ko") is None


def test_kernel_module_layout_crc(tmp_path):
    s = tmp_path / "Module.symvers"
    s.write_text("0x00000001\tprintk\tvmlinux\tEXPORT_SYMBOL\n0x1234abcd\tmodule_layout\tvmlinux\tEXPORT_SYMBOL\n")
    assert vkb.kernel_module_layout_crc(s) == CRC
    s.write_text("0x00000001\tprintk\tvmlinux\tEXPORT_SYMBOL\nshort\n")
    assert vkb.kernel_module_layout_crc(s) is None


def test_find_ko_variants_and_debug(tmp_path):
    ko(tmp_path / "k" / ".debug" / "my_mod.ko")
    assert vkb.find_ko([str(tmp_path / "k")], "my_mod") is None  # .debug copies do not count
    want = ko(tmp_path / "k" / "drivers" / "my-mod.ko")
    assert vkb.find_ko([str(tmp_path / "k")], "my_mod") == str(want)  # underscore -> dash
    assert vkb.find_ko([None, str(tmp_path / "missing"), str(tmp_path / "k")], "my-mod") == str(want)
    assert vkb.find_ko([str(tmp_path / "k")], "other") is None


def test_strip_debug_only_wlan(tmp_path):
    calls = tmp_path / "calls"
    strip = tmp_path / "strip"
    strip.write_text(f"#!/bin/sh\necho \"$@\" >> {calls}\n")
    strip.chmod(0o755)
    vkb.strip_debug(str(ko(tmp_path / "msm_drm.ko")), str(strip))
    assert not calls.exists()
    vkb.strip_debug(str(ko(tmp_path / "wlan.ko")), str(strip))
    assert calls.read_text().split() == ["--strip-debug", str(tmp_path / "wlan.ko")]
    # a missing or failing strip is not fatal
    vkb.strip_debug(str(tmp_path / "wlan.ko"), str(tmp_path / "no-such-strip"))


# ---- aurora-vkb-assemble.py: the whole assembly ----

@pytest.fixture
def tree(tmp_path):
    k = tmp_path / "kernel"
    tp = tmp_path / "techpack"
    static = tmp_path / "static"
    static.mkdir()
    ko(k / "drivers" / "clk-qcom.ko")
    ko(k / "drivers" / "gdsc.ko")
    ko(tp / "usr" / "lib" / "wlan.ko")
    (tmp_path / "Module.symvers").write_text(f"0x{CRC:08x}\tmodule_layout\tvmlinux\tEXPORT_SYMBOL\n")
    (static / "modules.dep").write_text(
        "/lib/modules/clk-qcom.ko: /lib/modules/gdsc.ko /lib/modules/old.ko\n"
        "/lib/modules/old.ko:\n"
        "garbage line\n"
        "\n"
        "gdsc.ko:\n")
    (static / "modules.alias").write_text("alias of:N*T*Cqcom foo\n")
    (static / "modules.softdep").write_text("")
    m = tmp_path / "m.lst"
    m.write_text("gdsc:k\nclk_qcom:k\nwlan:tp\n@drop old\n@dep wlan.ko: /lib/modules/gdsc.ko clk-qcom.ko\n"
                 "@dep extra.ko: gdsc.ko\n")
    return tmp_path


def assemble(tree, monkeypatch, manifest=None, symvers=None):
    run_main(vkb, ["--manifest", manifest or tree / "m.lst", "--kernel-pkgdir", tree / "kernel",
                   "--techpack-dir", tree / "techpack", "--symvers", symvers or tree / "Module.symvers",
                   "--static-dir", tree / "static", "--out", tree / "out", "--strip", tree / "no-strip"],
             monkeypatch)
    return tree / "out" / "lib" / "modules"


def test_assemble(tree, monkeypatch, capsys):
    (tree / "out" / "stale").mkdir(parents=True)  # the output directory is wiped first
    md = assemble(tree, monkeypatch)
    out = capsys.readouterr().out
    assert "KMI gate PASSED (3 modules" in out
    assert not (tree / "out" / "stale").exists()
    assert sorted(p.name for p in md.glob("*.ko")) == ["clk_qcom.ko", "gdsc.ko", "wlan.ko"]
    assert (md / "modules.load").read_text() == "gdsc.ko\nclk_qcom.ko\nwlan.ko\n"
    assert (md / "modules.load.recovery").read_text() == (md / "modules.load").read_text()
    dep = (md / "modules.dep").read_text().splitlines()
    # stock entries seeded (the dropped module and its uses removed), manifest entries added, @dep applied
    assert "/lib/modules/clk-qcom.ko: /lib/modules/gdsc.ko" in dep
    assert "/lib/modules/gdsc.ko:" in dep
    assert not any(d.split(":", 1)[1].count("old.ko") for d in dep)  # nobody depends on the dropped module
    assert "/lib/modules/wlan.ko: /lib/modules/gdsc.ko /lib/modules/clk-qcom.ko" in dep
    assert "/lib/modules/extra.ko: /lib/modules/gdsc.ko" in dep
    assert "WARN: @dep extra.ko not in module list" in out
    assert (md / "modules.alias").is_file() and (md / "modules.softdep").is_file()
    assert "WARN: static modules.load.charger missing" in out


@pytest.mark.xfail(strict=True, reason="bug: the stock modules.dep line of an @drop'd module is kept: the key "
                   "'old.ko' is compared with the bare name 'old' (dependencies are compared without .ko); "
                   "mainline/agents/tests/patches/meta-smartwatch/0001")
def test_assemble_drops_the_stock_entry_of_a_dropped_module(tree, monkeypatch):
    md = assemble(tree, monkeypatch)
    dep = (md / "modules.dep").read_text().splitlines()
    assert not any(d.startswith("/lib/modules/old.ko:") for d in dep)


def test_assemble_kmi_mismatch(tree, monkeypatch, capsys):
    ko(tree / "techpack" / "usr" / "lib" / "wlan.ko", crc=0x0BADC0DE)
    with pytest.raises(SystemExit) as e:
        assemble(tree, monkeypatch)
    assert "KMI-CRC gate failed for 1 module(s)" in str(e.value)
    assert "KMI FAIL: wlan.ko (tp) module_layout=0x0badc0de" in capsys.readouterr().out


def test_assemble_no_crc_in_module(tree, monkeypatch, capsys):
    (tree / "kernel" / "drivers" / "gdsc.ko").write_bytes(b"stripped")
    with pytest.raises(SystemExit):
        assemble(tree, monkeypatch)
    assert "KMI FAIL: gdsc.ko (k) module_layout=none" in capsys.readouterr().out


def test_assemble_no_kernel_crc(tree, monkeypatch):
    (tree / "Module.symvers").write_text("")
    with pytest.raises(SystemExit) as e:
        assemble(tree, monkeypatch)
    assert "module_layout CRC not in" in str(e.value)


def test_assemble_missing_module_and_wrong_tag(tree, monkeypatch):
    m = tree / "m2.lst"
    m.write_text("nothere:k\n")
    with pytest.raises(SystemExit) as e:
        assemble(tree, monkeypatch, manifest=m)
    assert str(e.value) == "ABORT: nothere.ko not built in k-source"
    m.write_text("wlan:k\n")  # it is a techpack module
    with pytest.raises(SystemExit) as e:
        assemble(tree, monkeypatch, manifest=m)
    assert "found in techpack -- wrong :k tag?" in str(e.value)
    m.write_text("gdsc:tp\n")
    with pytest.raises(SystemExit) as e:
        assemble(tree, monkeypatch, manifest=m)
    assert "found in kernel-pkg -- wrong :tp tag?" in str(e.value)


def test_assemble_dropped_then_listed(tree, monkeypatch):
    m = tree / "m3.lst"
    m.write_text("@drop gdsc\ngdsc:k\n")
    with pytest.raises(SystemExit) as e:
        assemble(tree, monkeypatch, manifest=m)
    assert "lists gdsc but @drop'd earlier" in str(e.value)


def test_assemble_without_stock_dep(tree, monkeypatch, capsys):
    (tree / "static" / "modules.dep").unlink()
    md = assemble(tree, monkeypatch)
    assert "stock modules.dep" not in capsys.readouterr().out
    dep = (md / "modules.dep").read_text().splitlines()
    assert "/lib/modules/gdsc.ko:" in dep and "/lib/modules/clk_qcom.ko:" in dep
