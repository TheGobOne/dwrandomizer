---
name: dwr-agent-suite
description: Runs the DWR review suite of three specialist roles on the randomizer. Use whenever the user asks to review, test, audit or check DWR code, player features, a screen, menu or the website's usability, or asks for a run of "the agents", "the suite", or the Code, QoL or UIUX roles, even if they don't name the role. Also use for "run our agents through the changes" or a review of recent work.
---

# DWR agent suite

Three roles check the DWR randomizer from different angles. Pick the roles the request needs, load each role's brief, and do the work in that brief's voice.

## Roles

| Role | Brief | Use for |
|---|---|---|
| Code | `references/code.md` | 6502 and C review of all randomizer code: bugs, bytes, cycles, clarity |
| QoL | `references/qol.md` | Player features, settings, and fairness for racing |
| UIUX | `references/uiux.md` | Screens, menus, the website, labels, keyboard and mouse flow, accessibility |

## How to run a role

1. Read that role's brief before you start.
2. Read the files the brief names, in the project at `C:\Users\Ryan\code\dwrandomizer`.
3. For UIUX, get a real screenshot or read the drawing code before you judge a screen. Never describe a screen from memory.
4. Report in the brief's format, with at most 5 items per list, ranked by impact.
5. Do not commit, push or release unless the user asks. The project's push rules apply.

## Running several roles

Run independent roles in parallel with subagents, one brief each. Give each subagent its brief path, the request, and the files it needs. Collect the results and report them in one summary, one section per role, with the top item first.

## Project rules that apply to every role

- Write DWR in output, not DWRando.
- Use plain sentences with no em dashes, as the project's writing rules require.
- End the summary with one next action the user can finish in under 2 minutes.
