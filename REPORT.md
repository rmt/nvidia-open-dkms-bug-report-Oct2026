# 615.71.09: resume activates an absent DisplayPort sink, followed by notifier stall and GSP RPC timeouts (RTX 2070, Arch Linux)

Prepared 2026-10-09. This is a report draft and experimental patch bundle, not an upstream-approved fix.

## Summary

On Arch Linux with NVIDIA open kernel modules 615.71.09, resuming from suspend-to-RAM can leave the DisplayPort monitor dark and the display stack unable to recover. Rebooting was required. Powering the monitor off before resuming reproduced the problem during investigation; earlier suspend/resume attempts sometimes worked.

Added driver diagnostics show a successful connection query reporting no connected DP displays, including the previously connected monitor. Nevertheless, the framebuffer-console restore attempts to activate that display. The display core notifier does not complete normally, the existing driver synthesizes completion after three seconds, and subsequent DP packet RPCs to GSP time out. Xid 119 and then Xid 154 follow. A later display-core cleanup stalls with GET behind PUT.

An experimental patch rejects activation of an absent external DP sink during SET_MODE validation, including test-only requests, and always schedules connector reprobe after resume. On the first patched hardware test boot, two monitor-off resumes completed, and the monitor was detected and restored when powered on later. Neither cycle logged the previous NVIDIA failures.

This strongly supports stale activation of an absent sink as a trigger. It does not establish the underlying firmware defect or prove long-term reliability.

## Environment

| Component | Observed configuration |
|---|---|
| OS | Arch Linux, x86_64 |
| Kernel used for the detailed failing and successful captures | `7.2.9-arch1-1`, stock Arch stable kernel, PREEMPT_DYNAMIC |
| Driver/module package | `nvidia-open-dkms 615.71.09-1` |
| NVIDIA userspace | `nvidia-utils 615.71.09-1` |
| GPU | GeForce RTX 2070 / TU106, PCI `0000:01:00.0`, ID `10de:1f02` |
| Additional display controller | Intel UHD Graphics 630, ID `8086:3e98`; affected DP path is on NVIDIA |
| Desktop | Mango / Wayland, `mangowm 0.17.5-1`; console restoration is implicated, not only compositor rendering |
| Monitor | EDID product name `LG ULTRAGEAR`; exact retail model still to be supplied |
| Driver connector | NVKMS `DP-4`, display ID `0x2000`, API head 0 / hardware head 3 (`0x8`) |
| Connected DRM connector when inspected | `card1-DP-3`; DRM/NVKMS numbering differs and does not identify the physical socket order |
| Restored link | Four lanes, `rate_10mhz=810` (8.1 Gbit/s per lane) |
| Sleep mode | `deep`; `/sys/power/mem_sleep` reports `s2idle [deep]` |
| NVIDIA DRM | `modeset=Y`, `fbdev=Y` |
| systemd | `262-1` |

Loaded NVIDIA parameters inspected after the patched tests:

```text
UseKernelSuspendNotifiers: 1
PreserveVideoMemoryAllocations: 2
DynamicPowerManagement: 3
EnableGpuFirmware: 0
EnableGpuFirmwareLogs: 2
TemporaryFilePath: "/var/tmp"
```

The recorded `EnableGpuFirmware=0` value must not be described as proof that GSP is disabled: the failing open-driver capture explicitly contains GSP RPC timeouts.

`nvidia-suspend.service` and `nvidia-resume.service` are disabled. `suspend-chvt.service` switches to TTY2, waits one second, and is ordered `Before=systemd-suspend.service`. Its completion precedes both patched suspends. The effective suspend environment is `SYSTEMD_SLEEP_FREEZE_USER_SESSIONS=true`; a local override supersedes the vendor no-freeze setting, and the successful test journal explicitly records freezing/thawing `user.slice`.

Earlier boots included an LTS kernel, which complicates informal comparisons. The detailed before/after captures discussed here both use 7.2.9. Failures began after a driver/kernel upgrade, but a controlled previous-driver comparison has not been performed, so the exact regression boundary is not established. The same-version proprietary kernel driver has not been tested; this is not confirmed to be open-driver-specific.

## Reproduction and expected behavior

1. Boot the affected open-driver/kernel combination with the DP monitor working.
2. Suspend to RAM; the configured service switches away from Mango to TTY2 before suspend.
3. Power the monitor off before resuming, leaving it off throughout resume. The precise timing of power-off relative to suspend entry was not separately recorded.
4. Resume using the normal wake mechanism. Repeat cycles if necessary; a failure was observed after preceding successful cycles.
5. Turn the monitor on later.

Expected: the machine resumes while the sink is absent, without programming an unavailable output or blocking GPU/display operations. A later monitor power-on/hotplug should restore the display on the same connection without rebooting.

Actual before the fix: the monitor remains dark and GPU/display operations fail to recover. SSH worked during some earlier occurrences. During the detailed failure, SSH authentication and PAM session opening were logged, but no useful shell was obtained before reboot. This is not proof of complete kernel or network failure.

The trigger is physical monitor power-off during system resume, not merely DPMS blanking on an otherwise awake machine. Driver-observed unplugged state is confirmed in this capture; other deep-sleep monitors can retain HPD and need separate investigation.

## Detailed failing capture

Boot ID: `2b12cbef0d064432a5ee0617b0c4ca6d`, 2026-10-08, approximately 23:02–23:06 CEST. This boot contains diagnostic instrumentation but not the experimental fix. It first contains a successful monitor-connected resume and then the failing absent-monitor resume.

Times below are kernel monotonic journal timestamps; suspend affects the relationship to wall-clock time.

| Timestamp | Observation |
|---|---|
| 134.411 s | Earlier control resume detects DP-4 present. |
| 134.493–134.505 s | Active head mask `0x8` restored; notifier wait returns after 12 ms. DP packet calls succeed. |
| 181.282 s | Failing resume: DP-4 `detected_plugged=0`, `previous_plugged=1`. The connection query succeeded and returned an empty mask. |
| 181.286–181.291 s | Device initialization with `active_heads=0x0` completes its core wait in 4 ms. |
| 181.304 s | Cached console restoration nonetheless submits a core update with `active_heads=0x8`, PUT `0x688`. |
| 184.304 s | Notifier still incomplete after 3000 ms; GET equals PUT `0x688`. Existing synthetic-completion fallback executes. |
| Following packet calls | DP packet types `0x87` and `0x07`, control `0x730288`, return timeout status `0x65`. Xid 119 reports GSP RPC timeout. |
| 202.861 s | Xid 154 changes recovery action to PF FLR. |
| 205.867, 235.867, 265.867 s | Cleanup notifier remains stalled: GET `0x6b8`, PUT `0x778`; logs at 3, 33 and 63 seconds. |

The synthetic-completion behavior predates these changes. It was instrumented, not introduced or corrected by the patch. A “core wait returned” message alone must not be interpreted as successful hardware completion if the preceding fallback ran.

Evidence: [failure diagnostics](logs/failure-diagnostics.log), [full failing kernel journal](logs/failure-kernel.log). The first GPU failure visible in this sequence is the incomplete core notifier; the later GSP packet timeouts may be consequences rather than the original cause.

## Experimental change

See [patch explanation and application instructions](PATCHES.md). The bundle includes two diagnostic patches and a separate functional patch, plus a complete diff against the reporter's original 615.71.09 source archive.

The functional patch:

- Checks every proposed active DPLib display, including cloned outputs, before SET_MODE hardware validation/programming. An absent external SST sink is rejected unless deliberately forced connected. Internal panels are exempt; MST requires upstream presence and an actual plugged downstream device. Cached EDID is not a presence exemption. Inactive/detach requests remain permitted.
- Applies the check to test-only SET_MODE as well as commit, so an already absent sink is rejected before DRM state swap. Standalone offline mode-feasibility queries remain separate.
- Unfreezes fbdev normally and dirties/reprobes connectors after every modeset-device resume, using delayed hotplug work. Later power-on uses normal HPD recovery.
- Handles a disconnect between validation and commit: resolves only that rejected transaction's events in submission order, balances acquired vblank references, retains old scanout state, and forces full hardware configuration on a subsequent recovery update.
- Reclaims rejected input-fence semaphore reservations under a callback lock. Reference-counted callback data prevents freeing while a callback is outstanding, and canceled callbacks cannot write to a reused semaphore slot.

This is intentionally an experimental review patch. The event/semaphore recovery code is defensive hardening beyond the particular console restore demonstrated in the hardware tests. AI tools assisted with investigation, implementation, independent source review, host tests, and drafting; the reporter performed the hardware tests.

## Latest hardware results

Patched boot ID: `fd88fa51009147b482c58b0f96878976`, 2026-10-09. Reporter confirms resume now works with the monitor powered off. The journal independently confirms two absent-sink resumes followed by successful monitor hotplug and modeset.

| Observation | Cycle 1 | Cycle 2 |
|---|---|---|
| PM suspend entry, monotonic | 71.963 s | 189.131 s |
| DP-4 detected absent, monotonic | 74.210 s | 191.533 s |
| Guard rejects stale activation | 74.224 s | 191.547 s |
| PM suspend exit, wall clock | 09:14:34 CEST | 09:16:46 CEST |
| Resume reprobe marker | Present | Present |
| Delayed reprobe, monotonic | ~76.261 s | ~193.573 s |
| Later HPD reports DP-4 present, wall clock | 09:15:04 CEST | 09:17:09 CEST |
| Delay from suspend exit to HPD | ~30 seconds | ~23 seconds |
| Restoring active-head core wait | 30 ms | 27 ms |
| Restored link | Four lanes / 8.1 Gbit/s | Four lanes / 8.1 Gbit/s |
| DP packet types `0x87` and `0x07` | Status `0x0` | Status `0x0` |

Representative guard output:

```text
DP activation refused: absent external sink; connector=DP-4 display=0x2000 dpy=0x2000 api_head=0 hw_heads=0x8 mst=0
RESUME-DIAG: framebuffer console restore complete
RESUME-FIX: scheduling connector reprobe and hotplug
PM: suspend exit
```

The captured patched boot contains no Xid 119/154, GSP RPC timeout, synthetic notifier completion, or flip-event timeout. Systemd completes both suspend units successfully. USB controller reinitialization, slow SATA-link responses, and a workqueue-deprecation warning remain; no evidence here establishes them as the NVIDIA failure's cause. The first suspend unit also shows a delay before `user.slice` thawing, so successful GPU restoration does not imply every resume component was instantaneous.

Evidence: [patched diagnostics](logs/patched-diagnostics.log), [wall-clock timeline](logs/patched-timeline.log), [full patched kernel journal](logs/patched-kernel.log), [suspend services](logs/patched-suspend-services.log).

## Verification and limitations

Full module compilation against 7.2.9 headers passed. Host tests cover the actual compiled DP presence helper, extracted flip/event recovery functions under ASan/UBSan in two DRM reference API variants, and extracted semaphore callback/cancellation functions. Results and reproduction instructions are in [TESTS.md](TESTS.md).

The two hardware tests exercised the early validation rejection, not a disconnect after validation/state swap. The defensive event and semaphore paths are host-tested and source-reviewed, not hardware-proven. The successful boot contains no `DP-RESUME:` post-swap recovery markers.

Not established yet: diagnostic-free reliability; repeated long-duration monitor-off cycles; a deliberately induced validation/commit race; multi-monitor/cloning behavior; MST, internal-panel/DDS/forced-connection compatibility; HPD-high deep sleep; other GPUs; or the same-version proprietary driver. Diagnostic logging adds overhead and may affect timing. Old failed atomic-state references are conservatively kept until a later blocking commit with a queued hardware flip completes without timeout, or device teardown; repeated failures/nonblocking-only recovery can retain resources longer than desirable.

## Related reports and maintainer questions

Related issues are contextual evidence, not identified duplicates. See [RELATED.md](RELATED.md) for verified links and distinctions, especially [PR #1359: unplugged DP detach bookkeeping](https://github.com/NVIDIA/open-gpu-kernel-modules/pull/1359), [issue #1371: DPMS wake deadlock](https://github.com/NVIDIA/open-gpu-kernel-modules/issues/1371), and [issue #1361: blocking commit flip-timeout regression](https://github.com/NVIDIA/open-gpu-kernel-modules/issues/1361).

Questions for NVIDIA:

1. Should a proposed active SET_MODE be rejected when DP discovery has completed successfully with no sink, before console restoration programs the display head?
2. Is the incomplete core notifier followed by packet RPC timeouts consistent with a known GSP/display-engine problem when an absent sink is activated?
3. Is the proposed presence policy compatible with intended forced/headless, MST, internal-panel, and mux behavior? Would a narrower upstream change be preferable?
4. What is the intended handling of a modeset rejected after DRM state swap, including event completion, scanout resource lifetime and semaphore reservation cleanup?
5. Could the nearby unplugged-detach changes discussed in PR #1359 interact with this resume path? That PR has not been applied or tested here.

## Before posting

- Supply the exact LG monitor model, active mode/refresh rate, cable/adapter topology, and monitor deep-sleep setting. EDID capabilities alone do not establish the active test mode.
- Confirm the historical pre-upgrade driver/kernel versions if making a precise regression claim.
- Attach a reviewed `nvidia-bug-report.log.gz` if requested; it is not included or claimed to have been captured during the failure. Generate it after a failure only if GPU queries remain usable; they may hang.
- Review the included raw kernel/service logs and build paths for hostnames, identifiers and other personal information before uploading.
- Prefer the NVIDIA Linux forum unless open-driver-only behavior can be established; the GitHub issue forms explicitly request a same-version proprietary-driver comparison.

Nothing has been submitted publicly by preparing this bundle.
