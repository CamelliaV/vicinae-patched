# Repository Guidelines

## Project Structure & Module Organization
- `src/server`, `src/cli`, `src/snippet`, and `src/data-control-server` contain the main C++ applications.
- `src/lib/*` contains reusable libraries (`emoji`, `xdgpp`, `script-command`, `vicinae-ipc`, `common`) with local tests.
- `src/proto` holds shared protobuf contracts used by C++ and TypeScript.
- `src/typescript/api` and `src/typescript/extension-manager` contain the extension SDK/runtime.
- `extra/` stores themes, desktop assets, and packaging resources; `scripts/` contains release/build helpers.
- Treat `build/`, `src/build/`, `pkg/`, `dist/`, and `node_modules/` as generated artifacts, not source.

## Build, Test, and Development Commands
- `make release`: configure and build a Release build with Ninja.
- `make debug`: Debug build with sanitizers and `BUILD_TESTS=ON`.
- `cmake --build build`: incremental rebuild after configuration.
- `make test`: run primary Catch2 suites (`vicinae-emoji-tests`, `xdgpp-tests`, `scriptcommand-tests`).
- `make format`: run `clang-format` across C++ sources.
- `cd src/typescript/extension-manager && npm run build`: build extension runtime bundle.
- `cd src/typescript/api && npm run build`: build the TypeScript SDK.
- Arch workflow: `makepkg -si`; upstream refresh flow: `./update.sh`.

## Coding Style & Naming Conventions
- Follow `.clang-format` (LLVM-based, 110 column limit, includes not auto-sorted).
- Run `.clang-tidy` checks for new C++ code; legacy code is still being migrated.
- Match existing naming patterns: kebab-case file names (for example, `action-panel.cpp`), `CamelCase` types, and lower camel case methods.
- Keep comments minimal; add them only when intent or tradeoffs are non-obvious.

## Testing Guidelines
- C++ tests use Catch2 and live under `src/lib/*/tests` and `src/snippet/tests`.
- Name test files by behavior or feature (`time-parsing.cpp`, `special-cases.cpp`) and keep `TEST_CASE`s focused.
- For C++ changes, run affected module tests plus `make test` from repo root before opening a PR.
- TypeScript packages currently emphasize build/type checks; run `npm run build` (and `npm run typecheck` where available).

## Commit & Pull Request Guidelines
- Follow conventions used in history: `feat: ...`, `fix: ...`, `chore: ...`, optional scopes like `build(nix): ...`.
- Keep commits focused and logically grouped; write concise, imperative subjects.
- PRs should include a short description, local test evidence, and linked issues/PRs (`#123`) when applicable.
- Include screenshots or a short GIF for UI-facing changes (clipboard behavior, action panel, themes).
