#!/usr/bin/env python3
"""
Queries Greptile for a structured firmware code review and writes the result
to greptile-review.md in the workspace root.

Env vars:
  GREPTILE_API_TOKEN  -- Greptile bearer token
  GITHUB_TOKEN        -- GitHub token
  GREPTILE_BRANCH     -- branch to query (written by greptile_index.py)
  COMMIT_SHA          -- full commit SHA
  GITHUB_ENV          -- path to GHA environment file
"""
import os
import sys
import uuid
import requests

REPO = "Zwyft/tuggercallsystem"

REVIEW_PROMPT = (
    "You are an expert embedded systems engineer reviewing Arduino/ESP32 firmware.\n"
    "Perform a comprehensive code review of these three files:\n\n"
    "- firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino\n"
    "- firmware/TuggerDeviceFirmware/TuggerFirmware.ino\n"
    "- firmware/LineDeviceFirmware/LineDeviceFirmware.ino\n\n"
    "Review each file for (reference exact paths and line numbers):\n"
    "1. Critical bugs -- compile errors, undefined variables, duplicate declarations\n"
    "2. Logic errors -- broken control flow, code trapped inside comments\n"
    "3. Memory safety -- buffer overflows, string truncation, missing null terminators\n"
    "4. ISR/concurrency safety -- volatile correctness, blocking in ISR context\n"
    "5. LoRa/mesh protocol -- TX queue, CAD, dedup table, TTL/hop handling\n"
    "6. NVS/Preferences -- version checks, read/write pairing, cross-reboot integrity\n"
    "7. E-ink display -- drawing vs refresh separation, fastmode usage\n"
    "8. Cross-system consistency -- packet types, struct layout, configVal vs configStr\n"
    "9. Dead code -- functions defined but never called\n"
    "10. Minor issues -- style, redundant calls, misleading comments\n\n"
    "Format as Markdown:\n\n"
    "## Executive Summary\n(2-3 sentences)\n\n"
    "## Critical Issues\n(numbered: file, line, description, fix)\n\n"
    "## Major Issues\n(numbered: file, description, fix)\n\n"
    "## Minor Issues and Recommendations\n(bulleted)\n\n"
    "## Overall Assessment\n(conclusion and fix priority order)"
)


def main():
    token      = os.environ.get("GREPTILE_API_TOKEN", "").strip()
    gh_token   = os.environ.get("GITHUB_TOKEN", "")
    branch     = os.environ.get("GREPTILE_BRANCH", "dev-optimized-eink")
    commit_sha = os.environ.get("COMMIT_SHA", "unknown")[:8]
    env_file   = os.environ.get("GITHUB_ENV", "")

    if not token:
        _write_error(
            "GREPTILE_API_TOKEN secret is not configured.\n\n"
            "To enable Greptile firmware reviews:\n"
            "1. Go to **Settings → Secrets and variables → Actions**\n"
            "2. Click **New repository secret**\n"
            "3. Name: `GREPTILE_API_TOKEN`\n"
            "4. Value: your Greptile API token from greptile.com\n"
        )
        _setenv(env_file, "REVIEW_SUCCESS", "false")
        return

    headers = {
        "Authorization": f"Bearer {token}",
        "X-Github-Token": gh_token,
        "Content-Type": "application/json",
    }

    payload = {
        "messages": [
            {"id": str(uuid.uuid4()), "content": REVIEW_PROMPT, "role": "user"}
        ],
        "repositories": [
            {"remote": "github", "repository": REPO, "branch": branch}
        ],
        "sessionId": f"firmware-review-{commit_sha}",
        "stream":    False,
    }

    print(f"[greptile] Token length: {len(token)} chars")
    print(f"[greptile] Querying '{REPO}@{branch}'  sessionId=firmware-review-{commit_sha}")
    try:
        resp = requests.post(
            "https://api.greptile.com/v2/query",
            headers=headers,
            json=payload,
            timeout=180,
        )
    except requests.Timeout:
        _write_error("Greptile query timed out after 180 s.")
        _setenv(env_file, "REVIEW_SUCCESS", "false")
        return

    print(f"[greptile] Query response: {resp.status_code}")
    if resp.status_code == 404:
        print("[greptile] 404 — repository not indexed yet.")
        print("[greptile] The index script must complete successfully before querying.")
        print(f"[greptile] Response body:\n{resp.text}")
        _write_error(
            "Repository not indexed yet (HTTP 404).\n\n"
            "The `greptile_index.py` script must run and confirm `status=ready` "
            "before a query is possible. Check the **Index repository** step logs."
        )
        _setenv(env_file, "REVIEW_SUCCESS", "false")
        sys.exit(1)
    elif resp.status_code != 200:
        print(f"[greptile] Response body:\n{resp.text}")

    if resp.status_code == 200:
        data    = resp.json()
        message = data.get("message", "_(No review content returned)_")
        sources = data.get("sources", [])

        with open("greptile-review.md", "w") as f:
            f.write("# Greptile Firmware Code Review\n\n")
            f.write(f"**Branch:** `{branch}` | **Commit:** `{commit_sha}`\n\n---\n\n")
            f.write(message)
            if sources:
                f.write("\n\n---\n\n## Source References\n\n")
                for src in sources:
                    f.write(
                        f"- `{src.get('filepath','?')}` "
                        f"lines {src.get('linestart','?')}--{src.get('lineend','?')}\n"
                    )

        print("[greptile] Review written to greptile-review.md")
        _setenv(env_file, "REVIEW_SUCCESS", "true")
    else:
        _write_error(f"HTTP {resp.status_code}\n\n```\n{resp.text}\n```")
        _setenv(env_file, "REVIEW_SUCCESS", "false")
        sys.exit(1)


def _write_error(msg):
    with open("greptile-review.md", "w") as f:
        f.write(f"# Greptile Code Review -- Error\n\n{msg}\n")


def _setenv(path, key, value):
    if path:
        with open(path, "a") as f:
            f.write(f"{key}={value}\n")


if __name__ == "__main__":
    main()
