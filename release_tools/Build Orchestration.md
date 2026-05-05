# Release Orchestration

Wsprry Pi builds require the tag and branch to be consistent for use in the install script as well as the executable linking for compiler tags. To do that, we will follow these steps.

This guide outlines the precise steps to prepare a release where the Git **branch** and **tag** share the same name. In this situation, Git can be ambiguous when resolving references. We resolve this by being explicit with `refs/heads/` (branches) and `refs/tags/` (tags) in all commands.

Globally search/replace `3.0.0-rc.4` and `v3.0.0-rc.4` with your desired version.

---

## Build Orchestration Steps

1. **Create or update your release branch**

   ```bash
   git checkout main
   git pull origin main
   git merge devel
   ```

   * Merge any release features into main.

2. **Edit your source and install script**

    * Update `scripts\install.sh` with proper version:

        ```bash
        declare DEFAULT_REPO_BRANCH="devel"
        declare DEFAULT_SEM_VER="3.0.0-rc.4"
        ```

    * Apply any required feature changes or bug fixes.

3. **Commit those edits**

   ```bash
   git add scripts/install.sh
   git add "release_tools/Build Orchestration.md" # This file because you updated the tags
   git commit -m "Prepare 3.0.0-rc.4 release"
   ```

4. **Create an annotated tag on that commit**

   ```bash
   git tag -a v3.0.0-rc.4 -m "Release 3.0.0-rc.4"
   ```

5. **Compilation**: If a version-specific compile or any other process depends on the tag, execute that process now

   * **Stage any additional changes**: If you need to use the tag locaally such as with compiled executables, do these now.

      ```bash
      git add ./executables/
      ```

   * **Amend the previous commit to include the binary**

      ```bash
      git commit --amend --no-edit
      ```

   * **Force the tag to point to the amended commit**

      ```bash
      git tag -f v3.0.0-rc.4
      ```

6. **Push the branch and tag to the origin** (with -f if needed)

   ```bash
   git push origin HEAD:refs/heads/main
   git push origin tag v3.0.0-rc.4
   ```

---

## Why This Order?

* **Tag before build**: Ensures `git describe --tags` returns the correct version for embedding in the executable.
* **Build before staging**: Guarantees the binary contains that exact tag.
* **Amend + retag**: Ensures all release content (code, install script, and binary) lives in a single commit.
* **Explicit refs**: Prevents ambiguity between `refs/tags/main` and `refs/heads/main`.
* **Force-push allowed only on release branches**: Avoid rewriting history on `main`.

---

## Notes

* `git checkout main` will prefer the **branch**.

* To check out the tag explicitly, use:

  ```bash
  git checkout refs/tags/main
  ```

---

## Validation Tip

Verify what `git describe` sees before and after the tag:

```bash
git describe --tags --always
```

To confirm the tag points to the correct commit:

```bash
git show refs/tags/main
```

---

By following this process with **explicit reference usage**, your release artifacts will remain consistent and unambiguous across source, install scripts, and compiled binaries.
