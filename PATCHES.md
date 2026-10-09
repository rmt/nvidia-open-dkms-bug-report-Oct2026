# Patches and source provenance

## Two equivalent ways to obtain the tested source

| File | Base | Purpose |
|---|---|---|
| `patches/0001-resume-diagnostics.patch` | Original 615.71.09 source | First-stage device/console, packet and notifier tracing |
| `patches/0002-dp-resume-diagnostics.patch` | After 0001 | Connection query, DP resume/HPD/control/link/core-update tracing |
| `patches/0003-dp-resume-fix.patch` | After 0001 and 0002 | Experimental presence guard, reprobe and rejected-commit recovery |
| `patches/complete-debug-and-fix.patch` | Original 615.71.09 source | All changes above in one unified diff |

The complete patch was generated against the reporter-supplied `nvidia-615.71.09.orig.tar.gz`, whose SHA-256 is:

```text
fe34a546efac63f371993076667d72192b310f43617f68885d57c3101bee6a54
```

The archive's source root is `usr/src/nvidia-615.71.09/`. A fresh extraction was patched with the complete diff and compared recursively with `/usr/src/nvidia-615.71.09`: identical. The complete diff was then reversed in that disposable extraction, the three incremental patches applied in order, and the result again compared recursively: identical. The complete diff covers 16 files. Application emits a harmless existing new-blank-line-at-EOF whitespace warning; no hunks fail.

No claim is made that this archive is independently verified against an upstream release tarball. It is the reporter's preserved original package source. The bundled diff contains no generated objects or build output.

## Apply to a disposable source tree for review/build

Set these paths to your local locations. `report_dir` must be absolute because the commands change directory.

```bash
report_dir=/absolute/path/to/nvidia-open-dkms-bug-report
nv_source=/absolute/path/to/original/nvidia-615.71.09
cd "$nv_source"
git apply --check "$report_dir/patches/complete-debug-and-fix.patch"
git apply "$report_dir/patches/complete-debug-and-fix.patch"
make -j8 KERNEL_UNAME=7.2.9-arch1-1 modules
```

Alternatively apply 0001, then 0002, then 0003. Do not additionally apply the complete patch. These `git apply` commands work without initializing a Git repository.

A full rebuild is needed: diagnostics change the `NVDevEvoRec` layout and the fix changes DRM structures. Do not mix old objects or mismatched NVIDIA userspace/firmware with newly compiled modules. Package updates can overwrite the versioned DKMS source directory.

The reporter installed via DKMS for 7.2.9 and regenerated the linux initramfs before the successful test boot. Installation commands for that exact Arch setup were:

```bash
sudo dkms build --force -m nvidia -v 615.71.09 -k 7.2.9-arch1-1 &&
sudo dkms install --force -m nvidia -v 615.71.09 -k 7.2.9-arch1-1 &&
sudo mkinitcpio -p linux
```

These are not commands to run blindly on other distributions or versions. Module installation, reboot and suspend are not part of generating this report.

## Diagnostics

Stage one (`RESUME-DIAG`) records device resume and framebuffer-console restoration boundaries, the first eight non-Adaptive-Sync DP packet calls per resume, and notifier waits beyond the existing three-second threshold. Stalled notifier logs include the value/mask, GET/PUT, active-head mask and elapsed time, and repeat at most every 30 seconds per wait. The existing synthetic-completion fallback has an explicit marker.

Stage two (`RESUME-DIAG2`) records successful/failed connected-display queries, cached/detected DP presence, DP library resume/handoff, HPD callbacks, topology completion, pre-modeset attachments, existing DP control RPC results, link configuration and core update/wait boundaries. A 64-operation budget resets per resume; paired entry/return messages share a budget token.

Instrumentation logs existing operations rather than introducing extra GPU queries. It does not change packet flags, timeout thresholds, driver return statuses or the existing synthetic-completion behavior. Missing messages can reflect budget exhaustion or incomplete capture, not necessarily a stall. Logging can change timing. This does not trace execution inside GSP firmware.

## Functional changes and review concerns

`ValidateProposedDpActivation()` runs after proposed head assignment but before hardware-state validation. It rejects proposed active absent external DPLib sinks on both test-only and committed SET_MODE requests. It does not block detach/all-heads-off updates or configured outputs without hardware heads. The helper exempts internal panels, permits explicit forced SST connectivity, and requires actual upstream/downstream presence for MST. The explicit forced state is tracked and reset consistently with DP resume.

Desktop resume now marks connectors dirty and queues a two-second reprobe, previously confined to a SoC workaround. `mod_delayed_work(system_wq, ...)` updates an already pending HPD deadline; it stays on the existing HPD workqueue so work is not moved between queues while running. This settling delay is not a system-wide PM barrier. Kernel 7.2 logs a workqueue-deprecation warning; a migration must update all enqueue paths consistently and is not included here.

The absence guard can reject a commit after DRM already swapped its software state if a sink disappears between validation and submission. Recovery removes only that transaction's hardware flip accounting, preserves submission/event order, balances actual vblank gets, and completes rejected events behind prior pending flips rather than waiting for nonexistent hardware notifications. Flip IRQ processing is deferred while submission success is unresolved. Failed atomic-state references preserve old scanout resources, and a later recovery forces a complete SET_MODE rather than trusting software deltas.

Retained state is released conservatively only after a blocking update with a queued hardware flip completes without timeout, or at device teardown. A no-flip/nonblocking recovery can retain state longer; repeated failures can retain multiple transactions. This policy merits maintainer review rather than being presented as an optimal upstream solution.

Input-fence conversion can reserve a semaphore before hardware rejects the transaction. The patch tracks those reservations and synchronizes cancellation against callbacks. A canceled callback cannot write READY into a reused slot, and callback/transaction references keep the callback data alive. Cancellation permits both NOT_READY and already-signaled READY reservations. The SET_MODE/flip failure paths were reviewed for their pre-submission rejection contract. This does not attempt firmware reset or recovery after accepted hardware work stalls.

Runtime markers added by the fix:

```text
DP activation refused: absent external sink; ...
RESUME-FIX: scheduling connector reprobe and hotplug
DP-RESUME: atomic recovery; ...
DP-RESUME: recovery hardware apply accepted; ...
DP-RESUME: recovery old states released; ...
```

The successful hardware captures contain the first two markers only. Race/semaphore cleanup was not reached in those tests.

## Source rollback

To remove all local changes from a source tree matching this bundle:

```bash
git apply --check --reverse "$report_dir/patches/complete-debug-and-fix.patch"
git apply --reverse "$report_dir/patches/complete-debug-and-fix.patch"
```

To retain diagnostics and remove only the functional experiment, reverse 0003 instead. This changes source only; rebuild/reinstall and regenerate the initramfs separately to change installed modules. Preserve any subsequent edits and check applicability before reversal.
