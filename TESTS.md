# Verification results and test instructions

## Compile and source checks

- Full module build targeting `7.2.9-arch1-1`: exit 0. [Final build log](logs/build-7.2.9.log).
- Built `nvidia-drm.ko` vermagic: `7.2.9-arch1-1 SMP preempt mod_unload`.
- Tested DKMS source files matched the build copy before handoff.
- Complete patch and incremental series each reproduced the current DKMS source exactly from the supplied original archive.
- Patches can be reverse-applied cleanly to the corresponding modified source.

These checks do not establish successful GPU operation. Reporter installation and subsequent reboot produced the hardware logs listed below.

## Compiled DP presence policy

The host C++ harness links the actual built `nvdp-connector.o`, not a separate implementation of the policy. It passed these cases:

- External SST sink absent: reject; present: permit.
- Stale cached EDID and connected-display mask: still reject an absent sink.
- Explicit forced SST connection: permit.
- Internal panel: permit.
- MST: reject absent upstream, missing downstream device, or unplugged downstream; permit a plugged sink on a present upstream link. Forced SST connectivity cannot manufacture an MST sink.

Output: [activation-host-test.log](logs/activation-host-test.log).

To rerun on x86_64, first build the complete patched source, then:

```bash
NVIDIA_SOURCE=/absolute/path/to/built/patched/nvidia-615.71.09 \
  bash tests/activation-regression.sh
```

The harness supplies a host return thunk and links with unused sections discarded. It performs no GPU operation. The temporary executable is retained at the path printed by the script.

## Extracted flip/event recovery

The runner extracts exact function bodies from the patched source at execution time: queue enqueue/dequeue, event handling, rejected submission cleanup, deferred IRQ replay, and the successful no-hardware-event deferred-flip block. Mock objects provide lists, locks, event/vblank counters and state references.

Seven scenarios pass in both the atomic-state and atomic-commit reference API variants under AddressSanitizer and UndefinedBehaviorSanitizer:

1. FIFO A then B, with deferred A IRQ and multiple hardware events required for A.
2. Rejected queued B behind pending A: remove only B and deliver A before B.
3. Rejected queued B without older work: resolve its event/vblank reference immediately.
4. Failure before event transfer, with prior A pending.
5. Failure before event transfer, with no older work.
6. Retain one old transaction reference per affected CRTC, including no-flip transactions.
7. A pending, B rejected, C successful without a hardware event: delivery remains A, B, C.

Mocks check list consistency, event ownership, lock nesting, positive vblank references and allocations/frees. LeakSanitizer is disabled due to the tracing environment; allocation counts are checked explicitly. These are sequential control-flow tests, not actual interrupt concurrency, full atomic integration, retained-state release or GPU operation.

```bash
NVIDIA_SOURCE=/absolute/path/to/patched/nvidia-615.71.09 \
  python3 tests/flip-regression.py
```

Requires a C compiler supporting ASan/UBSan; set `CC` to choose it. Output: [flip-host-tests.log](logs/flip-host-tests.log).

## Extracted semaphore callback/cancellation

Actual callback/decision code, cancellation function and registration-error block are extracted and compiled with mocks. Passed cases include:

- Signal before rejection: READY reservation becomes reusable DONE.
- Rejection before signal: later callback does not modify a reused slot or dereference a deliberately stale device pointer.
- Successful submission: normal callback GPU signaling behavior preserved.
- Registration failure: its reservation and earlier transaction reservations cleaned up.
- Empty/repeated decision cleanup and reference release.

```bash
NVIDIA_SOURCE=/absolute/path/to/patched/nvidia-615.71.09 \
  python3 tests/semaphore-regression.py
```

Output: [semaphore-host-tests.log](logs/semaphore-host-tests.log). This sequential mocked test does not model a real simultaneously executing fence callback or full input-fence conversion/GPU submission.

## Hardware: first patched test boot

The reporter performed two monitor-off suspend/resume cycles on 2026-10-09, kernel 7.2.9, boot `fd88fa51009147b482c58b0f96878976`. Both completed with the sink absent. The guard rejected stale console activation; later monitor power-on produced HPD, successful link training/modeset and DP packet success. No Xid, GSP timeout, synthetic-completion or flip-event timeout appears in the captured boot. Detailed timing is in [REPORT.md](REPORT.md).

This is positive initial testing of the early presence guard plus normal late hotplug recovery. It does not hardware-test the post-validation disconnect-race event/semaphore paths.

## Suggested further hardware checks

Keep kernel, cable, monitor settings, driver parameters and systemd configuration fixed when comparing:

1. Monitor stays on through repeated suspend/resume: no regression.
2. Monitor remains off through resume for several minutes, then is powered on: prompt recovery without reboot or another suspend.
3. Monitor power changes near resume completion: disconnect/reconnect races.
4. Multiple outputs/clones, MST branches and forced/headless configurations where available.
5. Deep sleep with HPD still asserted: record separately; presence rejection alone cannot handle an unresponsive logically plugged sink.
6. Repeat without diagnostic instrumentation if maintainers provide a suitable split/narrow patch.

Capture the complete kernel journal before attempting GPU reconfiguration; after failure, GPU queries such as `nvidia-smi` or the vendor bug-report script may block. No additional suspend tests were initiated while preparing this bundle.
