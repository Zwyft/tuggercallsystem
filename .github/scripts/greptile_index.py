#!/usr/bin/env python3
"""
Indexes Zwyft/tuggercallsystem with Greptile then polls until ready.

Env vars (all provided by the workflow step):
  GREPTILE_API_TOKEN  -- Greptile bearer token (repo secret)
  GITHUB_TOKEN        -- GitHub token for Greptile repo access
  EVENT_NAME          -- github.event_name
  HEAD_REF            -- github.head_ref  (PR source branch)
  REF_NAME            -- github.ref_name  (push branch)
  GITHUB_ENV          -- path to GHA environment file
"""
import os
import time
import urllib.parse
import requests

REPO      = "Zwyft/tuggercallsystem"
MAX_POLLS = 16   # 16 x 15 s = 4 min
POLL_WAIT = 15


def main():
    token    = os.environ["GREPTILE_API_TOKEN"]
    gh_token = os.environ["GITHUB_TOKEN"]
    event    = os.environ.get("EVENT_NAME", "push")
    head_ref = os.environ.get("HEAD_REF", "")
    ref_name = os.environ.get("REF_NAME", "")

    branch = head_ref if (event == "pull_request" and head_ref) else ref_name
    if not branch:
        branch = "dev-optimized-eink"

    headers = {
        "Authorization": f"Bearer {token}",
        "X-Github-Token": gh_token,
        "Content-Type": "application/json",
    }

    # reload=True on push so new commits are indexed.
    # reload=False on PRs — base branch is usually already indexed.
    reload_flag = (event != "pull_request")
    print(f"[greptile] Indexing '{REPO}@{branch}' reload={reload_flag}")

    resp = requests.post(
        "https://api.greptile.com/v2/repositories",
        headers=headers,
        json={
            "remote":     "github",
            "repository": REPO,
            "branch":     branch,
            "reload":     reload_flag,
            "notify":     False,
        },
        timeout=30,
    )
    print(f"[greptile] Index response: {resp.status_code} -- {resp.text[:300]}")

    # Propagate resolved branch to subsequent steps
    env_file = os.environ.get("GITHUB_ENV", "")
    if env_file:
        with open(env_file, "a") as f:
            f.write(f"GREPTILE_BRANCH={branch}\n")

    # Poll until indexed (or timeout)
    repo_id    = urllib.parse.quote(f"github:{REPO}@{branch}", safe="")
    status_url = f"https://api.greptile.com/v2/repositories/{repo_id}"
    READY = {"ready", "completed", "indexed"}
    FAIL  = {"failed", "error"}

    for attempt in range(1, MAX_POLLS + 1):
        time.sleep(POLL_WAIT)
        try:
            s = requests.get(status_url, headers=headers, timeout=15)
        except requests.RequestException as exc:
            print(f"[greptile] Poll {attempt}/{MAX_POLLS}: request error -- {exc}")
            continue

        if s.status_code == 200:
            status = s.json().get("status", "unknown")
            print(f"[greptile] Poll {attempt}/{MAX_POLLS}: status={status}")
            if status in READY:
                print("[greptile] Repository ready.")
                return
            if status in FAIL:
                print("[greptile] Indexing failed -- will attempt query with cached index.")
                return
        else:
            print(f"[greptile] Poll {attempt}/{MAX_POLLS}: HTTP {s.status_code}")

    print("[greptile] Poll timeout -- attempting query with cached index.")


if __name__ == "__main__":
    main()
