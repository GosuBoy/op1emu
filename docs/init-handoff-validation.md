# Synthetic INIT handoff validation receipt

Code commits: [e326fba](https://github.com/GosuBoy/op1emu/commit/e326fbaaab0c4418d975b51f484b9928beb2dd16) implements the bounded diagnostic and ROM contract; [b937bcc](https://github.com/GosuBoy/op1emu/commit/b937bcc1ed54ea4fcf972a17567ec256e654804c) strengthens the zero-fill proof. Both are descendants of the preserved readiness checkpoint ac95c97. No existing private or baseline guest image, peripheral default, legacy selection, bcore pin or R4 admission behavior changed.

## Independent CI

[Final CI run 40989197894](https://github.com/GosuBoy/op1emu/actions/runs/40989197894) passed all six jobs for b937bcc:

- Linux profiling OFF: 41/41 native tests.
- Linux profiling ON: 44/44 native tests.
- macOS: 41/41 native tests.
- GNU Binutils 2.44: independent image/source/symbol reconstruction passed.
- Python: 15 tooling unittests and 183 pytest cases passed.
- libc++ TimeSource regression passed.

The initial implementation also passed [all six jobs](https://github.com/GosuBoy/op1emu/actions/runs/40988175064). The unchanged loopback test, unavailable locally under the current execution permissions, passed in CI. No test was relaxed and no new host permission or local dependency installation was requested.

## Executed contract

The final integration suite includes 17 runs/controls plus pre-execution identity and option rejection. It observes actual ROM instructions, INIT entry with live R0 boot data, a correct return into ROM, source redirection by 16 bytes, a validated destination-buffer callback, and transfer through EVT1 into the identical MAIN payload used by the direct-loader control.

The bootstrap first writes FFFFFFFF to a mailbox word. The positive ROM path clears it to zero; removing FILL produces E007. This check cannot pass solely from constructor zeros. Missing INIT/ignored redirection produce E004; malformed boot data, bad callback context and missing setup produce E001/E002/E003. Cycle and Run bounds are exact; the parent watchdog and explicit trace-omission results distinguish timeout and incomplete observation.

The positive run uses 2,261 issue packets / CPU Run calls, including 2,069 in ROM, and 90,156 model cycles. Each native configuration independently asserts byte-identical repeats of its state, event and history files. These counts are functional model timing, not measured clocks or hardware qualification.

## Input and output identities

Guest compiler identity: GNU Binutils 2.44 bfin-elf assembler/linker, -mcpu=bf524; no guest C compiler/runtime. The source/linker hashes and all case images are pinned in the fixture manifest. Native local identity below is the Apple clang 17.0.0 / LLVM 15.0.7 build; CI executable identities are recorded separately in each supervision receipt.

- Normal outer image SHA256: `0edac2d7b2188bb3c3a55d5e3e9adc8c3a4156dcbf7a23577f68a7c09945610f`.
- Identical MAIN payload SHA256: `f5715114ac2d4276b9d12104000e5eeba0424768eed941b0e5c94e9f237922fd`.
- Generated, unchanged synthetic NAND-pattern SHA256: `6c0294b06b53f1e89f9978a127bf9ee4182239125570f700aa47f39d79a6d278`.
- Local native diagnostic executable SHA256: `0430ff010cb7e94fcbb2612f285092c53c29c858db351a5b616b777f59445f87`.

Local final positive-repeat output SHA256 values (each pair was compared byte for byte):

- .events.jsonl: `c243462620db96c9c044767fc6be2e1ecbb3d50cd5fd874dec6a523fec602a1a`.
- .portg-hysteresis.jsonl: `557fd952da25226935362dd2ded695644b4614e4516c68e708a169bca58612e5`.
- .result.json: `6fd86463c7703f8de11ce04efcab141b80b357898bfe8da9b8bd72b6999b7c85`.
- .state.json: `3ffcc606f99a8115920e7dcf22a9bdf043c179b6cdf815193c2efed0def090ac`.

These local hashes are not asserted as universal across different native builds. The integration summary inside each CI artifact records that job's identities. Artifact bundles include the existing public synthetic captures and the new JSON/JSONL reports; no raw private firmware, NAND, OTP or private research is added.

- [sport-transport-macos-15-profiling-OFF](https://github.com/GosuBoy/op1emu/actions/runs/40989197894/artifacts/12949080713) — artifact 12949080713; sha256:7ba508127688f749ac61079a47bab18e78c3bd2980220800345185e15e1419ba.
- [sport-transport-ubuntu-22.04-profiling-OFF](https://github.com/GosuBoy/op1emu/actions/runs/40989197894/artifacts/12948770928) — artifact 12948770928; sha256:f4c3d31cd19485acb74fa6ae573ef7b4968a02a0bc52be462e57a9f06a73e308.
- [sport-transport-ubuntu-22.04-profiling-ON](https://github.com/GosuBoy/op1emu/actions/runs/40989197894/artifacts/12948282286) — artifact 12948282286; sha256:ee4f5d3031a5cc7d21b8909c8e3281d2be7d5b896e4c8e5a2bdd3d6ba11c593f.

## Reproduction and qualification

Use the build and supervised commands in [init-handoff.md](init-handoff.md), or run:

```sh
cmake --build build --target op1-init-handoff op1-bounded-execution-test
ctest --test-dir build --output-on-failure -R 'op1-(bounded-execution-test|init-handoff)$'
```

Local validation passed the focused regressions and exact fixture rebuild. The local full suite was 40/41 because loopback listening returned EPERM; local pytest was unavailable and was not installed. CI supplied the complete independent results above.

Only the shared memory-boot INIT protocol is qualified. The same explicit synthetic NAND snapshot is attached in each path and issues zero commands; NAND boot/ECC/context behavior is not qualified. No owner OTP is supplied and preboot is not entered. Physical BF524 ROM identity, TE-BOOT 2.30, CLKIN/PLL relock, actual SDRAM/cache/CPLB behavior, board/codec state and physical audio remain unqualified. No forced audio clock or private image replay was used.

The remote branch contains the tested files. Local Git metadata is read-only in this execution context, so the local HEAD remains ac95c97 with working files matching the published implementation. Publication used the existing GitHub connection and non-forced expected-head updates, without changing permissions or any other branch/PR.

The authorized milestone ends here. No hardware work or subsequent implementation phase is started.
