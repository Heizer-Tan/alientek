# Web OTA Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Board webserver accepts `.swu` upload, runs A/B upgrade via `board-apply-update`, auto-reboots; disable SWUpdate built-in web.

**Architecture:** `POST /api/upgrade` streams multipart to `/var/tmp/web-upgrade.swu`, runs `board-apply-update` (no `--reboot` yet), returns JSON, then `reboot`. Busy lock prevents concurrent upgrades.

**Tech Stack:** C (webserver), shell (`board-apply-update`), HTML/JS fetch, Yocto bb.

## Global Constraints

- No auth; auto-reboot after success; reuse `board-apply-update` for A/B.
- Do not use SWUpdate Mongoose web UI.
- Chinese comments; keep changes focused.

---

### Task 1: Revert SWUpdate web trial

- [ ] Restore `swupdate-minimal.cfg` (WEBSERVER/MONGOOSE off)
- [ ] Remove `15-webserver-port` and bbappend install bits
- [ ] Replace README “SWUpdate 内置网页” section with board web upgrade notes

### Task 2: webserver upgrade API

- [ ] Extend request parsing for POST + header/body streaming
- [ ] Multipart extract field `swu` → `/var/tmp/web-upgrade.swu`
- [ ] Busy lock; run `/usr/bin/board-apply-update <path>`; JSON response; then reboot on success
- [ ] Host compile smoke (`-lsqlite3`)

### Task 3: UI + recipe

- [ ] Upgrade section in `index.html`
- [ ] `RDEPENDS` += `board-update-tools`
- [ ] Commit when user asks
