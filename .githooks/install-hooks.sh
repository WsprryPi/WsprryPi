#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# @brief Install and validate Git hooks for this repository.
#        Can be run from anywhere inside the repo.
# -----------------------------------------------------------------------------

# Resolve repo root
if ! repo_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf "%s\n" "Error: Not inside a Git repository." >&2
    exit 1
fi

hooks_dir="${repo_root}/.githooks"

printf "%s\n" "[install-hooks] Repo root: ${repo_root}"

# Validate hooks directory exists
if [[ ! -d "${hooks_dir}" ]]; then
    printf "%s\n" "Error: .githooks directory not found at repo root." >&2
    exit 1
fi

# Expected hooks
EXPECTED_HOOKS=(
    "post-checkout"
    "post-merge"
    "pre-commit"
    "pre-push"
)

# Ensure hooks are executable
printf "%s\n" "[install-hooks] Ensuring hooks are executable"

for hook in "${EXPECTED_HOOKS[@]}"; do
    hook_path="${hooks_dir}/${hook}"

    if [[ -f "${hook_path}" ]]; then
        chmod +x "${hook_path}"
    else
        printf "%s\n" "[install-hooks] WARNING: Missing hook: ${hook}" >&2
    fi
done

# Ensure library files are readable (not executable necessarily)
if [[ -d "${hooks_dir}/lib" ]]; then
    find "${hooks_dir}/lib" -type f -name "*.sh" -exec chmod 644 {} \;
fi

# Set hooks path explicitly at repo root
git -C "${repo_root}" config core.hooksPath .githooks

printf "%s\n" "[install-hooks] core.hooksPath set to .githooks"

# Final validation
printf "%s\n" "[install-hooks] Verifying installation"

actual_path="$(git -C "${repo_root}" config core.hooksPath || true)"

if [[ "${actual_path}" != ".githooks" ]]; then
    printf "%s\n" "Error: Failed to set core.hooksPath." >&2
    exit 1
fi

printf "%s\n" "[install-hooks] SUCCESS"
