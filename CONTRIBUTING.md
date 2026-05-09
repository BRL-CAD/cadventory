# Contributing To CADventory

CADventory is still in a prototype-to-MVP stage, so the most helpful contributions are the ones that increase clarity, trust, and forward momentum without creating avoidable churn.

## Before You Start

Read these first:

- [README.md](README.md)

Those documents explain the current product direction and the current state of the repository.

## Good Contribution Areas

The highest-value areas right now are:

- README and onboarding improvements
- reproducible build and test workflows
- MVP metadata/schema definition
- architecture cleanup and consolidation
- targeted UI/UX improvements
- better release and packaging hygiene

## Development Guidelines

- Keep changes focused. Small, reviewable steps are much easier to integrate than broad rewrites.
- Avoid committing generated build directories, editor backup files, or machine-specific artifacts.
- If your change touches product behavior, update documentation alongside the code.

## Architecture

```text
┌──────────────────────────────────────────────────────────────────────┐
│                               CADventory                             │
│                                                                      │
│    ┌──────────────────────────────────────────────────────────────┐  │
│    │                      User Interface (Qt)                     │  │
│    └─────────────┬───────────────────────┬─────────────────┬──────┘  │
│                  │                       │                 │         │
│    ┌─────────────▼───────────┐  ┌────────▼─────────┐  ┌────▼──────┐  │
│    │  Filesystem Processor   │  │ SQLite or JSON   │◄─│ Report    │  │
│    └─────────────┬───────────┘  │ Storage Manager  │  │ Generator │  │
│                  │              └──────────────────┘  └────┬──────┘  │
│                  │                                         │         │
│    ┌─────────────▼───────────┐                             │         │
│    │ Geometry/Image/Document │                             │         │
│    │ Handler                 │                             │         │
│    └─────────────┬───────────┘                             │         │
│                  │                                         │         │
│    ┌─────────────▼────────┐                                │         │
│    │     CAD Libraries    │◄───────────────────────────────┘         │
│    └──────────────────────┘                                          │
└──────────────────────────────────────────────────────────────────────┘
```

## Repository Layout

```text
.
├── src/                  Application source
│   ├── core/             App bootstrap and shared utilities
│   ├── domain/           Model, indexing, job, processing, and LLM logic
│   ├── ui/               Qt UI code
│   └── tests/            Automated tests
├── doc/                  Design docs, planning notes, and validation notes
├── scripts/              Small helper scripts
├── cmake/                CMake modules and dependency helpers
└── third_party/          Vendored third-party assets
```
