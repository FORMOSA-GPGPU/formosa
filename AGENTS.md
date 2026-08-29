# Agent Notes

- Do not assume `direnv` is active in the shell. Check with `direnv status`
  or environment variables such as `DIRENV_DIR` / `IN_NIX_SHELL` before relying
  on the flake environment.
- Prefer running commands through `direnv exec . <command>` so they use the
  repo's `.envrc` environment.
- If `direnv exec . <command>` reports that `.envrc` is blocked, run
  `direnv allow` in the repo first.
- If `direnv` is unavailable or cannot load the environment, fall back to
  `nix develop -c <command>`.
