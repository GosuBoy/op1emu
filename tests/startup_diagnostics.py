#!/usr/bin/env python3
"""Shipping CLI diagnostics on the public guest; private inputs never used."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = ROOT / "tests/fixtures/sport_guest/diagnostic.ldr"
    with tempfile.TemporaryDirectory(prefix="op1-startup-diagnostics-") as tmp:
        work = Path(tmp)
        (work / "gui").mkdir()
        (work / "gui/ui.json").symlink_to(ROOT / "gui/ui.json")
        script = work / "input"
        script.write_text("accel 0 0 800\nvolume 0\nwait 30000\nquit\n")
        base = [binary, work / "missing.nand", fixture, "--headless", "--deterministic", "--nand-snapshot",
                "--input-script", script, "--sport-tx-mode", "transport"]
        def run(name, extra):
            prefix = work / name
            result = subprocess.run(base + ["--diagnostics", prefix] + extra,
                                    cwd=work, capture_output=True, text=True, timeout=30)
            assert (prefix.with_suffix(".state.json")).exists(), result.stdout + result.stderr
            return result, json.loads(prefix.with_suffix(".state.json").read_text()), prefix
        result, state, prefix = run("complete", ["--sport-tx-clock", "1000000", "--max-cycles", "2000000",
                                                 "--diagnostics-memory", "0x2000:128", "--diagnostics-memory", "0x3000:64",
                                                 "--diagnostics-checkpoint", "0xffa00100"])
        assert result.returncode == 0, result.stdout + result.stderr
        assert state["stop_reason"] == "ldr_return" and state["pc"] == 0x08000000 and not state["cpu_failed"]
        assert state["sport"][0]["accepted"] == state["sport"][0]["transmitted"] == 16
        mailbox = struct.unpack("<32I", Path(str(prefix) + ".memory-2000.bin").read_bytes())
        assert mailbox[0] == 0x50415353 and mailbox[2] == 1 and mailbox[4] == 0x52544939
        samples = struct.unpack("<16I", Path(str(prefix) + ".memory-3000.bin").read_bytes())
        assert samples == tuple(0x12340001 + i * 0x101 for i in range(16))
        events = [json.loads(line) for line in prefix.with_suffix(".events.jsonl").read_text().splitlines()]
        assert events[0]["kind"] == "contract"
        assert any(e["kind"] == "pc_first" and e["pc"] == 0xffa00100 for e in events)
        assert any(e["kind"] == "mmio_write" and e["address"] == 0xffc00800 and e["value"] == 0x4401 for e in events)
        assert state["events_written"] == len(events) and state["events_omitted_or_coalesced"] > 0
        assert state["panel_profile"] is None
        assert state["portg_hysteresis_history"]["complete"]
        history = [json.loads(line) for line in Path(str(prefix) + ".portg-hysteresis.jsonl").read_text().splitlines()]
        assert any(e.get("label") == "cpu_constructed" and e["word"] == 0 for e in history)
        assert any(e.get("label") == "dxe_load_end" for e in history)
        assert any(e.get("label") == "requested_pc" and e["pc"] == 0xffa00100 for e in history)
        assert Path(str(prefix) + ".checkpoint-ffa00100.memory-2000.bin").stat().st_size == 128
        for value in ("1", "0x100000000", "-2"):
            invalid = subprocess.run(base + ["--diagnostics", work / "bad-pc", "--diagnostics-checkpoint", value],
                                     cwd=work, capture_output=True, text=True, timeout=30)
            assert invalid.returncode == 1, value
        result, state, _ = run("panel_profile", ["--max-cycles", "100", "--synthetic-panel-ddb", "123456789a"])
        assert result.returncode == 124 and state["panel_profile"] == [0x12, 0x34, 0x56, 0x78, 0x9a]
        assert "conditional" in result.stderr
        for value in ("", "123", "123456789x", "123456789abc"):
            result = subprocess.run(base + ["--synthetic-panel-ddb", value], cwd=work,
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 1, value
        for option, reason in (("--max-cycles", "cycle_limit"), ("--max-packets", "packet_limit")):
            result, state, _ = run(reason, [option, "100"])
            assert result.returncode == 124 and state["stop_reason"] == reason and state["pc"] != 0x08000000
            assert "ffc00d30" in state["mmr"] and "ffe0210c" in state["mmr"] and state["dpregs"]
        for value in ("0xffc01488:4", "0xef000000:4", "0xfffffff0:64", "0x2000:1048577", "0x2000:0"):
            result = subprocess.run(base + ["--diagnostics", work / "invalid", "--diagnostics-memory", value],
                                    cwd=work, capture_output=True, text=True, timeout=30)
            assert result.returncode == 1, value
        assert not (work / "missing.nand").exists()
    print("startup diagnostics: normal guest, event records, memory snapshots, cycle/packet limits and invalid ranges passed")


if __name__ == "__main__":
    main()
