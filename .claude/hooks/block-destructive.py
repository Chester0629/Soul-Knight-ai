#!/usr/bin/env python3
"""PreToolUse(Bash) guard for Soul-Knight-ai.

Blocks clearly-destructive shell commands before they run. Fail-open: if the
input can't be parsed or nothing matches, the command is allowed (exit 0).
Exit 2 blocks the tool call and shows the message to the agent.

Allowed on purpose: rm -rf of regenerable dirs (build/, PTSD/lib, PTSD/build,
/tmp), normal git add/commit/push (non-force). Blocked: history/tree-destroying
git ops, and rm -rf aimed at source dirs or the raw Unity dumps.
"""
import json
import re
import sys

try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)

cmd = (data.get("tool_input") or {}).get("command", "") or ""
if not cmd.strip():
    sys.exit(0)


def block(reason: str) -> None:
    sys.stderr.write("[guard] blocked a destructive command: " + reason + "\n")
    sys.stderr.write("[guard] If this is genuinely intended, run it yourself "
                     "outside the agent.\n")
    sys.exit(2)


c = " " + re.sub(r"\s+", " ", cmd) + " "

# --- Catastrophic git operations ---
if re.search(r"\bgit\s+reset\s+--hard\b", c):
    block("git reset --hard")
if re.search(r"\bgit\s+push\b.*(--force\b|--force-with-lease\b|\s-f\b)", c):
    block("git push --force")
if re.search(r"\bgit\s+clean\b[^|;&]*-\w*f", c):
    block("git clean -f")
if re.search(r"\bgit\s+(checkout|restore)\s+(--\s+)?\.\s", c):
    block("git checkout/restore . (discards the working tree)")

# --- rm -rf / -fr / -Rf aimed at a protected path ---
PROTECTED = (
    r"(PTSD/(src|include|test|example|assets|cmake)\b"
    r"|(^|\s)(src|include|test|docs|tools)/"
    r"|Resources/data\b|\b1\.07\b|\b1\.7\.10\b|\.git(\s|/|$)|\.gitnexus\b)"
)
for m in re.finditer(r"\brm\s+((?:-\S+\s+)+)", c):
    flags = m.group(1).lower()
    if "r" in flags and "f" in flags and re.search(PROTECTED, c):
        block("rm -rf targeting a protected source path or raw dump")

sys.exit(0)
