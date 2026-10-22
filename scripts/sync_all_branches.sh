#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
clone_if_missing() {
    local repo_url="$1"
    local repo_dir="$2"

    if [[ ! -d "$repo_dir/.git" ]]; then
        printf "Cloning %s into %s.\n" "$repo_url" "$repo_dir"
        git clone --recurse-submodules -j8 "$repo_url" "$repo_dir"
    fi

    cd "$repo_dir"
}

fetch_and_prune() {
    printf "Fetching all remotes and pruning stale branches.\n"
    git fetch --all --prune
}

track_remote_branches() {
    printf "Tracking remote branches.\n"
    for full_ref in $(git for-each-ref --format='%(refname)' refs/remotes/origin/); do
        local remote_branch="${full_ref#refs/remotes/origin/}"
        if ! git show-ref --quiet --verify "refs/heads/$remote_branch"; then
            git branch --track "$remote_branch" "origin/$remote_branch" 2>/dev/null || true
        fi
    done
}

prompt_to_delete_stale_branches() {
    printf "Checking for local branches that do not exist on origin.\n"
    local to_delete=()

    for full_ref in $(git for-each-ref --format='%(refname)' refs/heads/); do
        local branch="${full_ref#refs/heads/}"
        if ! git show-ref --verify --quiet "refs/remotes/origin/$branch"; then
            to_delete+=("$branch")
        fi
    done

    if (( ${#to_delete[@]} > 0 )); then
        for b in "${to_delete[@]}"; do
            printf "Delete local branch '%s'? [y/N]: " "$b"
            read -r answer < /dev/tty || { echo; continue; }
            if [[ "$answer" =~ ^[Yy]$ ]]; then
                git branch -D "$b"
            else
                printf "Skipping '%s'.\n" "$b"
            fi
        done
    else
        printf "No local-only branches found.\n"
    fi
}

iterate_and_update_branches() {
    local stashed_any=0
    cd "$(git rev-parse --show-toplevel)"
    local stash_name original_branch
    stash_name="auto-sync-branches-$(date +%s)"
    original_branch=$(git symbolic-ref --quiet --short HEAD || echo "")

    if [[ -n "$(git status --porcelain)" ]]; then
        printf "Uncommitted changes detected. Stashing before processing.\n"
        git stash push -u -m "$stash_name"
        stashed_any=1
    fi

    for full_ref in $(git for-each-ref --format='%(refname)' refs/heads/); do
        local branch="${full_ref#refs/heads/}"
        printf "Switching to '%s'.\n" "$branch"
        git checkout "$branch" 2>/dev/null || {
            printf "Warning: Failed to checkout '%s'. Skipping.\n" "$branch" >&2
            continue
        }

        if git rev-parse --verify --quiet "origin/$branch" > /dev/null; then
            git pull --ff-only || {
                printf "Fast-forward failed on '%s'.\n" "$branch" >&2
            }
        fi

        printf "\nAbout to reinit submodules, this may take a moment if there are many.\n"
        git submodule update --init --recursive 2>/dev/null || true
    done

    if [[ -n "$original_branch" ]]; then
        git checkout "$original_branch" 2>/dev/null || {
            printf "Warning: Could not return to original branch '%s'.\n" "$original_branch" >&2
        }
    fi

    if (( stashed_any )); then
        stash_ref=$(git stash list | grep "$stash_name" | awk -F: '{print $1}')
        if [[ -n "$stash_ref" ]]; then
            printf "Restoring previously stashed changes.\n"
            git stash pop "$stash_ref"
        else
            printf "Stash was made but not found. You may need to restore manually.\n"
        fi
    else
        printf "All branches updated with no stashing required.\n"
    fi
}

main() {
    local repo_url="${1:-}"
    local repo_dir="${2:-}"

    if [[ -n "$repo_url" ]]; then
        repo_dir="${repo_dir:-$(basename "$repo_url" .git)}"
        clone_if_missing "$repo_url" "$repo_dir"
    elif ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        printf "Error: Run inside a Git repo or pass a repo URL.\n" >&2
        exit 1
    fi

    if [[ "$(pwd)" != "$(git rev-parse --show-toplevel)" ]]; then
        printf "\nIf your current directory does not exist in one of your branches,\n"
        printf "You may see a fatal error after the script wuns when you try to\n"
        printf "perform certain actions.  If that happens, just 'cd ..' and you\n"
        printf "will be able to continue.  This is harmless.\n\n"
    fi

    fetch_and_prune
    track_remote_branches
    prompt_to_delete_stale_branches
    iterate_and_update_branches
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
