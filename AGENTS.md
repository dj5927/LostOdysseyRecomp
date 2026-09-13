# Repository agent workflow

## Feature versioning

Track the v0.5.0 backend and QOL requirements in the bilingual roadmaps and [QOL requirements](docs/notes/v0.5.0-qol-requirements.md). Recording a requirement does not authorize its implementation.

The current user request authorizes publishing `v0.5.9` from `main`, including the version update, release commits, pushes, tag and GitHub Release. Verify the PPC input/compiler identity and synchronize a matching private PPC cache when needed before release CI consumes it. The subsequent shader-update request authorizes version `0.5.10` and committing/pushing the scoped changes. Keep source version `0.5.10` until another version change is requested; publication of this version is not yet authorized. Do not add a version number for each subtask or validation. Preserve `v0.5.6`, the former `0.5.6-hotfix1` candidate, and earlier artifact versions as historical provenance, including city measurements made with a source-`0.5.4` binary. Future publication beyond this request requires explicit authorization.

## Project management agent

Invoke an on-demand subagent named `project_manager` when requirements are added or changed; development starts, pauses or finishes; validation or acceptance changes; commit or release state changes; or the user explicitly requests Project/TODO synchronization. Read the account-level OpenCode agent `~/.config/opencode/agent/project_manager.md` and [project workflow](docs/agents/project-management.md). If a Codex session is in use, also read `~/.codex/agents/project_manager.toml` under `CODEX_HOME` when that file exists. If the current session cannot load that custom agent type, use `task(subagent_type="project_manager", ...)` or a normal subagent with the same name and instructions, explicitly selecting the configured model and reasoning instead of inheriting the parent's model. Give it a bounded context handoff. Do not describe this as a scheduler or resident service.

The bound GitHub Project is the work-item status entry point; the English and Chinese roadmaps are synchronized repository mirrors. `project_manager` owns Project fields, deduplication, source/evidence references and task-state synchronization. Reuse existing Issues; use Project drafts for historical or planned work without an Issue. New Issues require an actual need and authorization. Preserve Issue state separately from implementation, validation, acceptance and release evidence.

The user's standing authorization covers creating, configuring, importing and synchronizing this LostOdysseyRecomp Project on demand. It does not authorize other repositories, implementing queued work, code/version changes, commits, pushes, releases or bulk comments. Give the agent a bounded scope and review its synchronization result. It must not delegate recursively.

## Documentation agent

Use an on-demand subagent named `docs_sync` for documentation synchronization after a feature or fix is accepted, and before a requested commit or release when behavior, validation, or publication status has changed. Also invoke it for explicit documentation synchronization requests. Small typo-only edits can be handled directly.

Read the account-level OpenCode agent `~/.config/opencode/agent/docs_sync.md` and `docs/agents/documentation.md`, then give the subagent the relevant change summary, test evidence, user acceptance, release state, and a bounded list of files to edit. Invoke with `task(subagent_type="docs_sync", ...)`. The parent agent continues independent work and reviews the documentation diff before completing the task. If subagents are unavailable, perform the same workflow locally and state that limitation.

`docs_sync` retains README, CHANGELOG, STATUS and release/validation narrative ownership. Coordinate roadmap edits with `project_manager`: hand off the affected passages before either agent writes, and never edit the shared mirror concurrently. Whoever verifies a new fact sends the other agent its precise source and scope; reuse that evidence instead of repeating tests for synchronization.

This is a repository workflow, not a scheduled or continuously running service. The documentation subagent must not delegate recursively. It does not grant permission to commit, push, publish, or operate the game.

## Tests and cleanup

Choose proportionate checks that verify meaningful behavior; do not add tests for reversible, low-impact edits or tests that merely restate the implementation. Use the selected suites in `tools/tests/README.md`; do not implicitly build or run everything. Once relevant checks pass, repeat or broaden them only for new changes, failures or unresolved concerns. Keep test CI separate from release packaging. Before handoff, remove only disposable files created by the current task, preserving evidence needed for review, user data and unrelated work.

Agent runtime tests run in the background without activating or stealing foreground focus. Non-audio tests default to muted sound; preserve user settings and use test-only mute configuration. If a verification depends on foreground interaction, do not take focus without a separate user arrangement; record the limitation and wait for that arrangement.
