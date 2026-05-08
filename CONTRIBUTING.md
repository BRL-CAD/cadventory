# Contributing To CADventory

CADventory is still in a prototype-to-MVP stage, so the most helpful contributions are the ones that increase clarity, trust, and forward momentum without creating avoidable churn.

## Before You Start

Read these first:

- [README.md](README.md)
- [doc/CADventory_project_review_2026-05-07.md](doc/CADventory_project_review_2026-05-07.md)
- [doc/CADventory_execution_plan_2026-05-07.md](doc/CADventory_execution_plan_2026-05-07.md)

Those documents explain the current project direction, the main gaps, and the active backlog.

## Good Contribution Areas

The highest-value areas right now are:

- README and onboarding improvements
- reproducible build and test workflows
- MVP metadata/schema definition
- architecture cleanup and consolidation
- targeted UI/UX improvements
- better release and packaging hygiene

## How To Propose Work

Because this project may live in more than one repository host or mirror, use the issue tracker and pull-request workflow of the host you cloned from.

If you are collaborating directly with the maintainer outside a public issue tracker, include:

- the problem you are solving
- the files or subsystem involved
- whether the change aligns with an execution-plan item
- any environment constraints such as OS, Qt version, or BRL-CAD setup

For larger changes, align on scope before implementing them. The execution plan is the best starting point for that conversation.

## Development Guidelines

- Keep changes focused. Small, reviewable steps are much easier to integrate than broad rewrites.
- Prefer a fresh build directory over reusing an old moved checkout.
- Avoid committing generated build directories, editor backup files, or machine-specific artifacts.
- If your change touches product behavior, update documentation alongside the code.
- If your change materially shifts priorities or scope, update the planning docs in `doc/`.

## Validation Expectations

Before submitting work, try to provide the most relevant validation you can.

Typical examples:

- build the project in a fresh build directory
- run the affected tests with `ctest --output-on-failure`
- note any environment-specific limitations
- include screenshots when changing the UI

If you could not validate something, say so explicitly.

## Planning And Roadmap

Current planning artifacts:

- Review note: [doc/CADventory_project_review_2026-05-07.md](doc/CADventory_project_review_2026-05-07.md)
- Execution plan: [doc/CADventory_execution_plan_2026-05-07.md](doc/CADventory_execution_plan_2026-05-07.md)

If you complete a planned item, update the relevant status or progress notes so future work can continue cleanly.
