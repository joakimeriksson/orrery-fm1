# Agent startup and shared work

This repository is building the meloDock workstation described in PLAN.md. Do not rely on chat memory to know
what exists or what another agent owns.

## Every session

1. Read PLAN.md (architecture), EXECUTION.md (task deliverables/workflow), CLAUDE.md (build/engine rules), and
   your handoff under work/agents/. Read relevant work/decisions/ before changing an interface.
2. Run `python3 tools/work.py status` and `git status --short`. WORK.json is the live status source. An agent
   handoff is resume context, not authority to steal an existing claim or call a task complete.
3. Choose a stable unique owner name. Claim one ready task using the tracker before implementation. Suggested
   first pair: agent-a takes M0-01; agent-b takes M0-02. Later use ready tasks, not a fixed assumption about phase.
4. Respect task resources and file scope. Never edit another agent's owned files or run shared integration while
   they are changing its inputs. Ask the coordinator to serialize shared-header/build/plan/ledger structure edits.
5. Write progress and exact next action using `note`; include actual validation and evidence paths. Before ending,
   update your own handoff. Submit finished work for the other agent's review, not directly to DONE.

## Two-agent rules

- Use two implementation lanes with cross-review. Agent A normally handles portable control/sequencing; Agent B
  handles instruments/effects/device integration. Lane assignment can change through explicit task release/claim.
- Review tasks promptly to unblock dependencies. `submit` reserves the task's resources until another owner
  accepts or requests revisions. `revise` returns review work to its original owner; it is not a handoff.
- Active, review and blocked tasks retain their resources; use `release` only when another agent can safely take
  over the partially changed files. Always describe the partial work and remaining checks first.
- Do not hand-edit WORK.json for progress. Use the locked CLI. Task additions, dependency/resource changes and
  abandoned-owner recovery are coordinator operations, performed with other tracker writers stopped and a
  recorded reason. Never infer abandonment from elapsed time or remove an OS lock file to force access.
- Keep one ledger shared by both agents. Separate worktrees require the same absolute `--file` path. Initial
  shared-checkout work is supported; no HEAD commit exists at planning time, so do not assume worktrees work.
- No further delegation is necessary for these two lanes. Do not spawn additional agents beyond the requested
  pair unless the user requests more parallelism. Be clear which actual owner name maps to each handoff file.

## Engineering constraints

- New sequencer implementation waits for the source-backed Melodee reuse decision and reviewed API. Audit
  results must not be invented from the README. API/harness work follows its recorded dependencies.
- Musical echo/reverb, track inserts and master sweep belong in meloDock. Host EQ/protection/global room are
  device processing. Electronic FX generators are instruments that can replace an existing part, not a fifth track.
- Engine files in engines/ are imported copies. Fix shared DSP upstream, run standalone checks, then take.py;
  put adapters and new local electronic-FX DSP here. Record import versions/hashes. External checkout writes
  still require the environment's normal filesystem approval.
- Audio uses fixed point, bounded state/queues and no allocation, host calls, waits, locks or 64-bit per-sample
  division. Audio owns live DSP; UI edits pass through a bounded handoff. Preserve note/source ownership.
- Verify relevant native tests per task; verify emulator/audio budgets at integration gates. Record commands,
  failures and limitations. Do not report tests on a moving shared tree as a stable integrated result.
- Commit only when the user asks. Never deploy to a real FM-1 without permission. Preserve existing untracked
  files; there is no permission to discard unrelated changes. Do not call planned behavior implemented.

For exact CLI commands, task scopes, handoff/evidence templates and acceptance gates, use EXECUTION.md.
