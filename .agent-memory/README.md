# Agent memory and plans

This folder stores repository-local plans and handoff context. Always-on rules
live in [.github/copilot-instructions.md](../.github/copilot-instructions.md);
workflows live in [.github/skills/](../.github/skills/).

## Using plans

- Before substantial work, read relevant existing plans and validate assumptions
  against current files. Do not treat old plans as proof of current behavior.
- Create future plans here as `<task-slug>.md`, using a descriptive filename.
  Update a relevant existing plan when continuing the same task rather than
  creating competing copies.
- Record the problem, approved scope and exclusions, acceptance criteria,
  implementation sequence and dependencies, Docker-based testing strategy,
  decisions, and open questions.
- Clearly distinguish proposed work from approved work. A saved plan does not
  authorize implementation or expand the user's scope.
- Update the plan when approved scope or execution state changes. At handoff,
  record what was completed, commands and concise verification results, blockers,
  and the next action. Label unexecuted checks explicitly.
- Plans are context, not instructions that override current repository guidance
  or the user's current request.

## Storage and commits

- This README is the shared, tracked-intended folder guide.
- Future plan files are committed **only when explicitly requested**. Do not
  include them accidentally with broad staging commands such as `git add .`;
  stage explicit paths and inspect the staged file list when a commit is requested.
- This directory is not ignored and is not a privacy boundary. Uncommitted plans
  may still appear in Git status or local tooling.
- Never store credentials, secrets, personal information, raw logs, or generated
  build/test output here. Keep handoffs concise and limited to technical context.
- Do not overwrite or delete another task's plans as cleanup.
