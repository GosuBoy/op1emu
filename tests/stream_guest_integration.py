#!/usr/bin/env python3
"""Independent sample, ownership and cadence oracle for the public stream guest."""
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

FIXTURE = Path(__file__).resolve().parent / "fixtures/stream_guest"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def expected(block, component, premature=False):
    # Two-channel signed16 bit patterns packed in the MSBs of SPORT32 words.
    frame, channel = divmod(component, 2)
    tag = 0x5000 if premature and block == 0 and component >= 8 else (block + 1) * 128
    return ((tag | channel * 64 | frame) & 65535) * 65536


def word_hash(values):
    h = 2166136261
    for v in values:
        for byte in struct.pack("<I", v):
            h = ((h ^ byte) * 16777619) & 0xffffffff
    return h


def check_capture(prefix, mode, report, symbols, rate=48000):
    late = mode in ("delay", "stop-delay")
    stopped = mode == "stop-delay"
    replays = (42 if stopped else 43) if rate == 44100 and late else 59 if late else 0
    timeout_words = 4134 if rate == 44100 else 4138
    stalled_read = 48 if rate == 44100 else 52
    stalled_accepted = stalled_read - 4
    rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(prefix.with_suffix(".csv").open())]
    assert rows and all(r["valid"] == 1 and r["bits"] == 32 and r["lsb_first"] == 0 for r in rows)
    assert prefix.with_suffix(".pcm").read_bytes() == b"".join(struct.pack("<I", r["value"]) for r in rows)
    clock = report["clock_events"]
    resume = clock[-1]["ns"] if mode == "pause" else None
    repeat_count, phase_errors, corruption = 0, 0, 0
    new = []
    last = None
    for i, row in enumerate(rows):
        assert row["index"] == i and row["slot"] == i % 2
        assert abs((row["time_ns"] - rows[0]["time_ns"] -
                    (500000 if resume and row["time_ns"] > resume else 0)) * (2 * rate) - i * 1000000000) <= 2 * rate, "frame cadence/gap"
        if row["repeated"]:
            repeat_count += 1
            assert late and row["value"] == last, "undocumented repeated or stale output"
        else:
            component = len(new)
            value = expected(component // 128, component % 128, mode == "premature")
            assert row["value"] == value, f"generation/index mismatch at fresh component {component}"
            corruption += row["value"] != expected(component // 128, component % 128)
            phase_errors += row["slot"] != component % 2
            new.append(row)
            last = row["value"]
    timeout = mode == "clock-timeout"
    assert len(new) == (timeout_words if timeout else 1280 if stopped else 32768)
    assert repeat_count == replays
    assert phase_errors == (31488 if mode == "delay" else 0), "late refill must expose the LR phase slip"
    assert corruption == (120 if mode == "premature" else 0)
    assert report["sport"]["underflows"] == (0 if timeout else repeat_count + 1)
    assert report["sport"]["overflows"] == 0 and report["invalid_words"] == 0
    assert report["buffer_high_water"] == {"dma": 4, "sport": 6, "combined": 10}
    errors = report["sport_error_observations"]
    assert (errors[-1]["underflows"] if errors else 0) == report["sport"]["underflows"]
    if mode not in ("delay", "stop-delay", "clock-timeout"):
        assert len(errors) == 1 and errors[0]["observed_ns"] > rows[-1]["time_ns"], "unexpected steady-state error"
    assert report["trace_dropped"] == 0 and report["mailbox"][51] == 0
    assert report["mailbox"][40] == (3 if stopped else 1 if mode == "delay" else 2 if mode == "premature" else 0)
    # DMA reads and acceptance are checked independently from serialized data.
    for block, record in enumerate(report["blocks"]):
        partial = timeout and block == 32
        count = stalled_read if partial else 128
        values = [expected(block, j, mode == "premature") for j in range(count)]
        assert record["read_words"] == count and record["read_hash"] == word_hash(values)
        assert record["accepted_words"] == (stalled_accepted if partial else 128)
        assert record["accept_hash"] == word_hash(values[:record["accepted_words"]])
        if not partial:
            state = record["completion"]
            assert state["modeled"] and state["sync"] and not state["running"] and state["done"]
            assert state["buffered"] == state["remaining"] == 0 and state["read"] == state["accepted"] == 128
            assert state["address"] == 0x3000 + (block % 2) * 512 + 512
            assert record["serialized_at_completion"] == (block + 1) * 128 - 6 + (replays if mode == "delay" and block >= 10 else 0)
    # Guest ledger validates every ownership transition and actual IDLE IRQ.
    owners, generations, refills = [0, 0], [None, None], [0, 0]
    irq_count = 0
    saw_premature = False
    for event in report["guest_events"]:
        kind, block = event[:2]
        slot = block % 2
        if kind == 1:
            assert owners[slot] == 0
            owners[slot] = 1
        elif kind == 2:
            assert owners[slot] == 1
            owners[slot], generations[slot] = 2, block
            refills[slot] += 1
        elif kind == 3:
            assert owners[slot] == 2 and generations[slot] == block
            owners[slot] = 3
        elif kind == 4:
            assert block == irq_count and owners[slot] == 3 and generations[slot] == block
            assert event[4] == 0 and event[5] == 0 and event[14] == 0 and event[15] == 1
            assert event[9] & 0x200 and event[8] == symbols[
                "delay_idle_resume" if late and block == 9 else "idle_dma_resume"]
            assert (event[6] & 0x30) == (0x10 if mode == "delay" and block >= 10 else 0)
            irq_count += 1
        elif kind == 5:
            assert owners[slot] == 3 and block < irq_count
            owners[slot] = 0
        elif kind == 8:
            assert mode == "premature" and block == 0 and owners[0] == 3
            saw_premature = True
        assert event[7] == owners[0] | (owners[1] << 8), "guest ownership ledger inconsistent"
    assert irq_count == (32 if timeout else 10 if stopped else 256)
    assert refills == ([17, 17] if timeout else [5, 5] if stopped else [128, 128])
    assert saw_premature == (mode == "premature")
    if mode in ("pause", "clock-timeout"):
        assert clock[1]["label"] == "stalled-a" and clock[2]["label"] == "stalled-b"
        assert clock[1]["dma"] == clock[2]["dma"] and clock[1]["sport"] == clock[2]["sport"]
        s, d = clock[1]["sport"], clock[1]["dma"]
        assert d["buffered"] == 4 and s["fifo"] == 4 and s["holding"] and s["shifting"]
        assert d["remaining"] == 128 - stalled_read and d["read"] == stalled_read and d["accepted"] == stalled_accepted
        assert s["transmitted"] == timeout_words and s["underflows"] == 0
        if mode == "pause":
            assert clock[-1]["ns"] - clock[0]["ns"] == 500000 and report["mailbox"][55] == 0
        else:
            assert rows[-1]["time_ns"] < clock[0]["ns"] and report["mailbox"][55] == 1
    if mode == "premature":
        assert report["mailbox"][54] == 8
        assert [p["value"] for p in report["prefix_reads"]] == [expected(0, j, True) for j in range(16)]
    return {"fresh_components": len(new), "repeated_components": repeat_count, "channel_phase_errors": phase_errors,
            "deliberate_corrupt_components": corruption, "first_serial_ns": rows[0]["time_ns"],
            "last_serial_ns": rows[-1]["time_ns"], "guest_cycles": report["cycles"],
            "buffer_productions": refills, "dma_interrupts": irq_count}


def execute(args):
    with tempfile.TemporaryDirectory(prefix="stream-cpu-") as tmp:
        return subprocess.run(args, cwd=tmp, capture_output=True, text=True, timeout=40)


def main():
    runner, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    rate = int(sys.argv[4]) if len(sys.argv) > 4 else 48000
    suffix = "-44100" if rate == 44100 else ""
    manifest = json.loads((FIXTURE / "manifest.json").read_text())
    for name, digest in manifest["sources"].items():
        assert sha((FIXTURE / name).read_bytes()) == digest, f"rebuild stale source: {name}"
    identities, metrics = {}, {}
    for mode in ("normal", "delay", "premature", "pause", "clock-timeout") + (("stop-delay",) if rate == 44100 else ()):
        image_name = mode if mode in ("delay", "premature", "stop-delay") else "normal"
        image_name += suffix
        identity = manifest["images"][image_name]
        image = FIXTURE / (image_name + ".ldr")
        assert sha(image.read_bytes()) == identity["image_sha256"]
        baseline = None
        for repeat in range(3):
            prefix = output / f"{mode}-{repeat}"
            run = execute([runner, image, prefix, mode, "--rate", str(rate)] + (["--chain"] if repeat == 2 else []))
            prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
            assert run.returncode == 0, run.stdout + run.stderr
            report = json.loads(prefix.with_suffix(".json").read_text())
            metrics[mode] = check_capture(prefix, mode, report, identity["symbols"], rate)
            fingerprint = tuple(prefix.with_suffix(ext).read_bytes() for ext in (".pcm", ".csv", ".json"))
            if baseline is None:
                baseline = fingerprint
            assert fingerprint == baseline, f"repeat/chaining difference: {mode}"
            print(run.stdout.strip())
        identities[mode] = {"image_sha256": identity["image_sha256"], **{
            k: sha(v) for k, v in zip(("pcm_sha256", "csv_sha256", "events_sha256"), baseline)}}
        if len(sys.argv) > 3 and mode in ("normal", "delay", "premature"):
            with tempfile.TemporaryDirectory(prefix="stream-cli-") as tmp:
                work = Path(tmp)
                (work / "gui").mkdir()
                (work / "gui/ui.json").symlink_to(FIXTURE.parents[2] / "gui/ui.json")
                script = work / "input-script"
                script.write_text("wait 30000\nquit\n")
                prefix = output / f"cli-{mode}"
                args = [Path(sys.argv[3]).resolve(), work / "missing.nand", image, "--headless", "--deterministic",
                        "--nand-snapshot", "--twi-speedup", "1", "--sport-tx-mode", "transport", "--cs42l52-test-mclk",
                        str(rate * 256), "--sport-tx-capture", prefix, "--input-script", script, "--max-cycles", "200000000"]
                run = subprocess.run(args, cwd=work, capture_output=True, text=True, timeout=40)
                prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
                assert run.returncode == 0 and "Finished executing DXE" in run.stdout + run.stderr, run.stdout + run.stderr
                assert tuple(Path(str(prefix) + ".sport0" + ext).read_bytes() for ext in (".pcm", ".csv")) == baseline[:2]
                assert Path(str(prefix) + ".sport1.pcm").read_bytes() == b"" and not (work / "missing.nand").exists()
    prefix = output / "limited"
    run = execute([runner, FIXTURE / ("normal" + suffix + ".ldr"), prefix, "normal", "--rate", str(rate), "--max-cycles", "100"])
    prefix.with_suffix(".log").write_text(run.stdout + run.stderr)
    report = json.loads(prefix.with_suffix(".json").read_text())
    assert run.returncode == 1 and report["errors"] and report["pc"] != 0x08000000
    assert all(k in report for k in ("pc", "cec", "dma", "sport", "codec_events", "guest_events"))
    (output / "identities.json").write_text(json.dumps(identities, indent=2, sort_keys=True) + "\n")
    (output / "metrics.json").write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n")
    print(json.dumps(metrics, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
