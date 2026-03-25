# Contributing to AimBuddy

Thanks for contributing. This project includes performance-sensitive Android and C++ runtime paths, so review quality is more important than change volume.

## Contribution Principles

- Fix root causes, not symptoms.
- Keep pull requests focused on one objective.
- Preserve behavior unless change scope explicitly includes behavior updates.
- Prefer simple and maintainable implementations.

## Development Workflow

1. Create a topic branch.
2. Implement with minimal unrelated edits.
3. Validate locally.
4. Update docs when behavior, architecture, settings, or workflow changed.
5. Open a pull request with clear evidence.

## Required Validation

Run before opening a pull request:

```powershell
./gradlew.bat clean assembleDebug
```

If your change touches training or export, run the affected scripts in `training/scripts/` and include output summary and report paths.

## Code Standards

- No placeholder implementations, dead code, or commented-out blocks.
- Keep naming consistent with module conventions.
- Keep hot paths efficient in capture, detector, tracking, control, and renderer loops.
- Avoid unrelated formatting-only churn.

Do not mix broad refactors with behavior changes in the same pull request.

## Pull Request Content

Include:

- problem statement
- technical approach
- validation output
- risk and rollback notes (if behavior changed)
- explicit scope boundaries

## Review Criteria

A pull request can be rejected if it:

- bundles unrelated changes
- lacks local validation
- introduces architecture drift without clear justification
- reduces maintainability or runtime stability

## Documentation Standards

- Keep docs clear, direct, and easy to execute.
- Keep Android and training commands aligned with current repository behavior.
- Use AI-based aim assistant terminology in user-facing documentation.
- Update the following when relevant: `README.md`, `docs/`, and `training/README.md`.

## Licensing

By contributing to this repository, you agree that your contribution is released under the repository license in `LICENSE`.
