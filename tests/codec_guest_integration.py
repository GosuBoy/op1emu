#!/usr/bin/env python3
"""Run the same public LDR twice per clock condition, then with CPU chaining.

Every run must produce byte-identical PCM, serial timestamps and guest event
records. No Blackfin toolchain is needed to execute the committed fixture.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

FIXTURE = Path(__file__).resolve().parent / "fixtures/codec_guest"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def execute(args):
    # CPU construction creates a synthetic OTP file. Keep it temporary and
    # independent of any user-supplied OTP in the caller's working directory.
    with tempfile.TemporaryDirectory(prefix="codec-guest-cpu-") as tmp:
        return subprocess.run(args, cwd=tmp, capture_output=True, text=True, timeout=30)


def main():
    runner, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((FIXTURE / "manifest.json").read_text())
    image = FIXTURE / "diagnostic.ldr"
    assert sha(image.read_bytes()) == manifest["image_sha256"]
    assert image.stat().st_size == manifest["image_bytes"]
    for name, digest in manifest["sources"].items():
        assert sha((FIXTURE / name).read_bytes()) == digest, f"stale fixture: {name}; rebuild it"
    expected = b"".join(struct.pack("<I", 0x12340000 + i * 0x10100) for i in range(16))
    identities = {"image_sha256": manifest["image_sha256"]}
    for mode in ("clock", "no-mclk", "reset-held", "pause"):
        baseline = None
        for repeat in range(3):
            prefix = output / f"{mode}-{repeat}"
            args = [runner, image, prefix, mode] + (["--chain"] if repeat == 2 else [])
            run = execute(args)
            prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
            assert run.returncode == 0, f"{args}\n{run.stdout}\n{run.stderr}"
            pcm, csv, events = (prefix.with_suffix(ext).read_bytes() for ext in (".pcm", ".csv", ".json"))
            assert pcm == (expected if mode in ("clock", "pause") else b""), "exact native PCM differs"
            report = json.loads(events)
            box = report["mailbox"]
            assert report["trace_dropped"] == 0 and report["direct_sport_clock_hz"] == 0
            writes = [e for e in report["codec_events"] if e["operation"] == "write"]
            if mode in ("clock", "pause"):
                assert any(e["clock_reason"] == "unsupported_clock_configuration" for e in writes)
                assert any(e["clock_reason"] == "unsupported_serial_configuration" for e in writes)
                assert any(e["clock_reason"] == "serial_pins_high_impedance" for e in writes)
                assert any(e["clock_reason"] == "slave_no_external_serial_source" for e in writes)
                assert any(e["bit_hz"] == 3072000 and e["frame_hz"] == 48000 and
                           e["master_mute"] == 3 and e["output_mute"] == 15 for e in writes)
                assert report["serialized_at_dma_isr"] == 10
            if mode == "pause":
                clocks = [e for e in report["codec_events"] if e["operation"] == "mclk"]
                assert [e["after"] for e in clocks] == [0, 12288000]
                assert clocks[1]["ns"] - clocks[0]["ns"] == 50000
            if mode != "reset-held":
                assert box["timer_reti"] == manifest["symbols"]["idle_drain_resume" if mode in ("clock", "pause") else "idle_dma_resume"]
            if mode in ("clock", "pause"):
                assert box["dma_reti"] == manifest["symbols"]["idle_dma_resume"], "DMA did not interrupt guest IDLE"
            fingerprint = (pcm, csv, events)
            if baseline is None:
                baseline = fingerprint
            else:
                assert fingerprint == baseline, f"{mode} repeat/chaining changes native output or guest event trace"
            print(run.stdout.strip())
        identities[mode] = {ext: sha(data) for ext, data in zip(("pcm_sha256", "csv_sha256", "events_sha256"), baseline)}
        # Also exercise the shipping positional LDR frontend. A missing NAND
        # in snapshot mode supplies erased pages; no proprietary input needed.
        if len(sys.argv) > 3 and mode in ("clock", "no-mclk"):
            op1emu = Path(sys.argv[3]).resolve()
            prefix = output / f"cli-{mode}"
            with tempfile.TemporaryDirectory(prefix="codec-guest-cli-") as tmp:
                work = Path(tmp)
                (work / "gui").mkdir()
                (work / "gui/ui.json").symlink_to(FIXTURE.parents[2] / "gui/ui.json")
                script = work / "input-script"
                script.write_text("wait 30000\nquit\n")
                args = [op1emu, work / "missing.nand", image, "--headless", "--deterministic", "--nand-snapshot",
                        "--input-script", script, "--sport-tx-mode", "transport", "--sport-tx-capture", prefix]
                args += ["--cs42l52-test-mclk", "12288000" if mode == "clock" else "0", "--twi-speedup", "1"]
                run = subprocess.run(args, cwd=work, capture_output=True, text=True, timeout=30)
                prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
                assert run.returncode == 0 and "Finished executing DXE" in run.stdout + run.stderr, run.stdout + run.stderr
                assert not (work / "missing.nand").exists()
                assert tuple(Path(str(prefix) + ".sport0" + ext).read_bytes() for ext in (".pcm", ".csv")) == baseline[:2], "CLI capture differs"
                assert Path(str(prefix) + ".sport1.pcm").read_bytes() == b"", "unexpected SPORT1 output"
    # A deliberately short execution budget must fail and still write useful
    # state. This exercises the watchdog around a guest that cannot finish.
    prefix = output / "limited"
    run = execute([runner, image, prefix, "clock", "--max-cycles", "100"])
    prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
    report = json.loads(prefix.with_suffix(".json").read_text())
    assert run.returncode == 1 and report["errors"] and report["pc"] != 0x08000000
    assert all(field in report for field in ("pc", "seqstat", "retx", "cec", "sic", "dma", "sport", "mailbox"))
    (output / "identities.json").write_text(json.dumps(identities, indent=2, sort_keys=True) + "\n")
    print(json.dumps(identities, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
