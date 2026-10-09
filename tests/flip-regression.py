#!/usr/bin/env python3
"""Extract unchanged driver functions, compile mocked host tests, and run them."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
SOURCES = {
    "nvidia-drm-crtc.h": ["nv_drm_crtc_enqueue_flip", "nv_drm_crtc_dequeue_flip"],
    "nvidia-drm-modeset.c": ["nv_drm_handle_flip_event_locked",
        "__nv_drm_handle_flip_event", "nv_drm_abort_submission", "nv_drm_end_submission"],
}


def balanced_block(text, start, pos):
    depth = 1
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for token in tokens.finditer(text, pos):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:token.end()]
    raise RuntimeError("Unterminated C block")


def extract(text, name):
    # Only static definitions, never comments, prototypes, or call sites.
    pattern = r"^static\s+(?:inline\s+)?[^;{}]*?\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{"
    matches = list(re.finditer(pattern, text, re.M))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one definition of {name}, found {len(matches)}")
    match = matches[0]
    # Ignore braces in comments and C string/character literals.
    return balanced_block(text, match.start(), match.end())


def main():
    snippets = []
    hashes = {}
    for filename, names in SOURCES.items():
        path = Path(os.environ.get("NVIDIA_SOURCE", str(ROOT / "source"))) / "kernel-open/nvidia-drm" / filename
        data = path.read_bytes()
        hashes[path] = hashlib.sha256(data).hexdigest()
        for name in names:
            snippets.append(extract(data.decode(), name))
            print(f"Extracted {filename}: {name}", flush=True)
        if filename == "nvidia-drm-modeset.c":
            # Extract the complete successful no-HW-event defer block verbatim.
            # Wrap just this block because the enclosing commit function needs
            # unrelated hardware/atomic-commit machinery.
            text = data.decode()
            pattern = r"if \(nv_new_crtc_state->nv_flip != NULL\) \{"
            matches = list(re.finditer(pattern, text))
            blocks = [balanced_block(text, m.start(), m.end()) for m in matches]
            blocks = [b for b in blocks if "nv_last_flip" in b]
            if len(blocks) != 1:
                raise RuntimeError("Expected one successful deferred-flip block")
            snippets.append("static void actual_success_defer(struct nv_drm_crtc *nv_crtc, "
                "struct nv_drm_crtc_state *nv_new_crtc_state, struct drm_crtc *crtc) {\n"
                + blocks[0] + "\n}")
            print("Extracted modeset.c: successful no-HW-event defer block", flush=True)
    with tempfile.TemporaryDirectory(prefix="nv-drm-host-") as tmp:
        generated = Path(tmp) / "actual-functions.h"
        generated.write_text("\n\n".join(snippets) + "\n")
        for variant in ("state", "commit"):
            binary = Path(tmp) / variant
            command = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
                       "-Werror", "-g", "-O1", "-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer", "-I", tmp,
                       str(HERE / "harness.c"), "-o", str(binary)]
            if variant == "commit":
                command.append("-DNV_DRM_ATOMIC_COMMIT_STRUCT_PRESENT")
            subprocess.run(command, check=True)
            print(f"Running {variant} reference API variant", flush=True)
            env = dict(os.environ)
            env["ASAN_OPTIONS"] = "detect_leaks=0"  # LSan cannot run under host ptrace.
            subprocess.run([str(binary)], check=True, env=env)
    for path, expected in hashes.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, path
    print("PASS: driver inputs unchanged")


if __name__ == "__main__":
    main()
