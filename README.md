# NVIDIA powered-off DisplayPort resume report

Start with [REPORT.md](REPORT.md), the self-contained report draft. Prepared on 2026-10-09 for NVIDIA 615.71.09 / Arch kernel 7.2.9, with an RTX 2070 and an LG UltraGear DP monitor.

The key evidence is an absent sink still being activated by the pre-fix console restore, followed by a notifier stall and GSP timeouts. Two patched monitor-off resumes now avoid activation and restore the monitor when it is powered on later. This is promising initial evidence, not a proven universal fix.

## Contents

- [REPORT.md](REPORT.md): environment, reproduction, failing sequence, experimental changes, latest hardware results and open questions.
- [RELATED.md](RELATED.md): verified NVIDIA GitHub/forum links, with differences from this failure.
- [PATCHES.md](PATCHES.md): patch series, complete diff provenance, application/rollback and design caveats.
- [TESTS.md](TESTS.md): compile/host/hardware verification and remaining gaps.
- `patches/`: diagnostics stage one, diagnostics stage two, functional fix, and an alternative complete diff against original source.
- `logs/`: failing/patched kernel journals, focused excerpts, successful suspend-service log, build and host-test outputs.
- `tests/`: source-extracting mocked regressions and a compiled DP policy harness; no binary modules or full NVIDIA source distribution.
- [environment.txt](environment.txt): concise captured configuration.
- `SHA256SUMS`: integrity hashes of report attachments.

## Publishing checklist

This directory has not been uploaded or submitted. Raw logs contain the local hostname and may include boot arguments, hardware/filesystem identifiers and other personal data. Build output also includes local filesystem paths. Review/redact attachments before public posting and regenerate hashes after redaction.

Before posting, fill in the exact monitor model, actual test mode/refresh rate, cable topology and monitor deep-sleep setting. A same-version proprietary-driver comparison and a precisely established previous-good driver/kernel pair are not available. `nvidia-bug-report.log.gz` is not included; do not imply it was captured during the failure.

The report deliberately separates diagnostic logging from the functional fix and distinguishes the tested early-rejection path from host-tested race cleanup. Do not apply both the incremental series and the complete patch: they are alternative representations of the same source changes.
