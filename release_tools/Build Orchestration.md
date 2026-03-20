# Release Orchestration

Wsprry Pi builds require the tag and branch to be consistent for use in the install script as well as the executable linking for compiler tags. To do that, we will follow these steps.

This guide outlines the precise steps to prepare a release where the Git **branch** and **tag** share the same name. In this situation, Git can be ambiguous when resolving references. We resolve this by being explicit with `refs/heads/` (branches) and `refs/tags/` (tags) in all commands.

Globally search/replace `2.2.1` and `v2.2.1` with your desired version.

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
        declare REPO_BRANCH="${REPO_BRANCH:-main}"
        declare GIT_TAG="${GIT_TAG:-v2.2.1}"
        declare SEM_VER="${SEM_VER:-2.2.1}"
        ```

    * Apply any required feature changes or bug fixes.

3. **Commit those edits**

   ```bash
   git add scripts/install.sh
   git add "release_tools/Build Orchestration.md"
   git commit -m "Prepare 2.2.1 release"
   ```

4. **Create an annotated tag on that commit**

   ```bash
   git tag -a v2.2.1 -m "Release 2.2.1"
   ```

5. **Compilation**

   * If a version-specific compile or any other process depends on the tag, execute that process now

6. **Stage any additional changes**

   ```bash
   git add ./executables/
   ```

7. **Amend the previous commit to include the binary**

   ```bash
   git commit --amend --no-edit
   ```

8. **Force the tag to point to the amended commit**

   ```bash
   git tag -f v2.2.1
   ```

9. **Push the branch and tag to the origin**

   ```bash
   git push origin HEAD:refs/heads/main
   git push origin tag v2.2.1
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
