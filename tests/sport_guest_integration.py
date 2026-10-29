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

FIXTURE = Path(__file__).resolve().parent / "fixtures/sport_guest"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def execute(args):
    # CPU construction creates a synthetic OTP file. Keep it temporary and
    # independent of any user-supplied OTP in the caller's working directory.
    with tempfile.TemporaryDirectory(prefix="sport-guest-cpu-") as tmp:
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
    expected = b"".join(struct.pack("<I", 0x12340001 + i * 0x101) for i in range(16))
    identities = {"image_sha256": manifest["image_sha256"]}
    for mode in ("clock", "no-clock"):
        baseline = None
        for repeat in range(3):
            prefix = output / f"{mode}-{repeat}"
            args = [runner, image, prefix, mode] + (["--chain"] if repeat == 2 else [])
            run = execute(args)
            prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
            assert run.returncode == 0, f"{args}\n{run.stdout}\n{run.stderr}"
            pcm, csv, events = (prefix.with_suffix(ext).read_bytes() for ext in (".pcm", ".csv", ".json"))
            assert pcm == (expected if mode == "clock" else b""), "exact native PCM differs"
            report = json.loads(events)
            box = report["mailbox"]
            assert box["timer_reti"] == manifest["symbols"]["idle_drain_resume" if mode == "clock" else "idle_dma_resume"]
            if mode == "clock":
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
        if len(sys.argv) > 3:
            op1emu = Path(sys.argv[3]).resolve()
            prefix = output / f"cli-{mode}"
            with tempfile.TemporaryDirectory(prefix="sport-guest-cli-") as tmp:
                work = Path(tmp)
                (work / "gui").mkdir()
                (work / "gui/ui.json").symlink_to(FIXTURE.parents[2] / "gui/ui.json")
                script = work / "input-script"
                script.write_text("wait 30000\nquit\n")
                args = [op1emu, work / "missing.nand", image, "--headless", "--deterministic", "--nand-snapshot",
                        "--input-script", script, "--sport-tx-mode", "transport", "--sport-tx-capture", prefix]
                if mode == "clock":
                    args += ["--sport-tx-clock", "1000000", "--sport-tx-frame-clocks", "64"]
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
