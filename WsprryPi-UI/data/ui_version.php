<?php

function getWsprryPiUiVersion(): string
{
    static $version = null;

    if ($version !== null) {
        return $version;
    }

    $output = shell_exec('/usr/local/bin/wsprrypi --version');
    if (!is_string($output)) {
        $version = '';
        return $version;
    }

    $version = trim($output);
    $prefix = 'WsprryPi version ';
    if (strncmp($version, $prefix, strlen($prefix)) === 0) {
        $version = substr($version, strlen($prefix));
    }
    $version = rtrim($version, ".");

    return $version;
}

const WSPRRYPI_UI_MANIFEST_SCHEMA_VERSION = 1;
const WSPRRYPI_UI_MANIFEST_FILENAME = 'ui-manifest.json';
const WSPRRYPI_UI_IDENTITY_DOMAIN = "wsprrypi-ui-manifest-v1\0";

final class WsprryPiUiIdentityError extends RuntimeException
{
}

function wsprrypiUiIsNormalizedPath(string $path): bool
{
    if ($path === '' || $path[0] === '/' || strpos($path, '\\') !== false ||
        preg_match('//u', $path) !== 1) {
        return false;
    }

    $parts = explode('/', $path);
    foreach ($parts as $part) {
        if ($part === '' || $part === '.' || $part === '..') {
            return false;
        }
    }

    return true;
}

function wsprrypiUiIsCoveredPath(string $path): bool
{
    if (!wsprrypiUiIsNormalizedPath($path)) {
        return false;
    }

    $parts = explode('/', $path);
    if (in_array($parts[0], ['cache', 'backups'], true)) {
        return false;
    }
    $filename = $parts[count($parts) - 1];
    return !in_array($filename, [WSPRRYPI_UI_MANIFEST_FILENAME, '.DS_Store'], true);
}

function wsprrypiUiNormalizedRelativePath(string $root, string $pathname): string
{
    $prefix = rtrim(str_replace('\\', '/', $root), '/') . '/';
    $normalized = str_replace('\\', '/', $pathname);
    if (strncmp($normalized, $prefix, strlen($prefix)) !== 0) {
        throw new WsprryPiUiIdentityError('UI file is outside the UI root.');
    }

    $relative = substr($normalized, strlen($prefix));
    if (!wsprrypiUiIsNormalizedPath($relative)) {
        throw new WsprryPiUiIdentityError('UI file has an unsafe path.');
    }
    return $relative;
}

function wsprrypiUiBuildFileRecords(?string $uiRoot = null): array
{
    $requestedRoot = $uiRoot ?? __DIR__;
    $root = realpath($requestedRoot);
    if ($root === false || !is_dir($root) || !is_readable($root)) {
        throw new WsprryPiUiIdentityError('UI root is unavailable.');
    }

    $records = [];
    try {
        $directory = new RecursiveDirectoryIterator(
            $root,
            FilesystemIterator::SKIP_DOTS
        );
        $filter = new RecursiveCallbackFilterIterator(
            $directory,
            static function (SplFileInfo $current) use ($root): bool {
                $pathname = $current->getPathname();
                $relative = str_replace(
                    '\\',
                    '/',
                    substr($pathname, strlen($root) + 1)
                );
                $segments = explode('/', $relative);
                if ($current->isDir() && count($segments) === 1 &&
                    in_array($segments[0], ['cache', 'backups'], true)) {
                    return false;
                }
                return true;
            }
        );
        $iterator = new RecursiveIteratorIterator(
            $filter,
            RecursiveIteratorIterator::SELF_FIRST
        );
    } catch (UnexpectedValueException $error) {
        throw new WsprryPiUiIdentityError('UI directory could not be read.', 0, $error);
    }

    try {
        foreach ($iterator as $file) {
            if (!$file instanceof SplFileInfo) {
                continue;
            }
            if ($file->isLink()) {
                throw new WsprryPiUiIdentityError(
                    'Symbolic links are not supported in UI artifacts.'
                );
            }
            if (!$file->isFile()) {
                continue;
            }

            $relative = wsprrypiUiNormalizedRelativePath($root, $file->getPathname());
            if (!wsprrypiUiIsCoveredPath($relative)) {
                continue;
            }
            if (!is_readable($file->getPathname())) {
                throw new WsprryPiUiIdentityError('A covered UI file is unreadable.');
            }
            $contentHash = @hash_file('sha256', $file->getPathname());
            if (!is_string($contentHash)) {
                throw new WsprryPiUiIdentityError('A covered UI file could not be hashed.');
            }
            $records[] = [
                'path' => $relative,
                'sha256' => $contentHash,
            ];
        }
    } catch (UnexpectedValueException $error) {
        throw new WsprryPiUiIdentityError('UI directory could not be read.', 0, $error);
    }

    usort(
        $records,
        static fn(array $left, array $right): int => strcmp($left['path'], $right['path'])
    );
    return $records;
}

function wsprrypiUiBuildIdFromRecords(array $records): string
{
    $context = hash_init('sha256');
    hash_update($context, WSPRRYPI_UI_IDENTITY_DOMAIN);
    foreach ($records as $record) {
        hash_update($context, $record['path']);
        hash_update($context, "\0");
        hash_update($context, $record['sha256']);
        hash_update($context, "\n");
    }
    return 'sha256:' . hash_final($context);
}

function wsprrypiUiValidateManifest(mixed $manifest): array
{
    if (!is_array($manifest) || array_is_list($manifest)) {
        throw new WsprryPiUiIdentityError('Manifest must be a JSON object.');
    }
    $expectedFields = [
        'application_version', 'files', 'packaged_ui_build_id',
        'schema_version', 'source_commit',
    ];
    $actualFields = array_keys($manifest);
    sort($actualFields, SORT_STRING);
    if ($actualFields !== $expectedFields) {
        throw new WsprryPiUiIdentityError('Manifest fields do not match schema version 1.');
    }
    if ($manifest['schema_version'] !== WSPRRYPI_UI_MANIFEST_SCHEMA_VERSION) {
        throw new WsprryPiUiIdentityError('Unsupported manifest schema version.');
    }
    if (!is_string($manifest['source_commit']) ||
        preg_match('/^(?:[0-9a-f]{40}|[0-9a-f]{64})$/D', $manifest['source_commit']) !== 1) {
        throw new WsprryPiUiIdentityError('Manifest source commit is invalid.');
    }
    if (!is_string($manifest['application_version']) ||
        trim($manifest['application_version']) === '') {
        throw new WsprryPiUiIdentityError('Manifest application version is invalid.');
    }
    if (!is_string($manifest['packaged_ui_build_id']) || !is_array($manifest['files']) ||
        !array_is_list($manifest['files'])) {
        throw new WsprryPiUiIdentityError('Manifest identity or files are invalid.');
    }

    $records = [];
    $previousPath = null;
    foreach ($manifest['files'] as $entry) {
        if (!is_array($entry) || array_is_list($entry)) {
            throw new WsprryPiUiIdentityError('Manifest file entry is invalid.');
        }
        $entryFields = array_keys($entry);
        sort($entryFields, SORT_STRING);
        if ($entryFields !== ['path', 'sha256'] ||
            !is_string($entry['path']) || !wsprrypiUiIsCoveredPath($entry['path']) ||
            !is_string($entry['sha256']) ||
            preg_match('/^[0-9a-f]{64}$/D', $entry['sha256']) !== 1) {
            throw new WsprryPiUiIdentityError('Manifest file entry is unsafe or malformed.');
        }
        if ($previousPath !== null && strcmp($entry['path'], $previousPath) <= 0) {
            throw new WsprryPiUiIdentityError(
                'Manifest paths must be unique and deterministically ordered.'
            );
        }
        $previousPath = $entry['path'];
        $records[] = ['path' => $entry['path'], 'sha256' => $entry['sha256']];
    }

    if (!hash_equals(
        wsprrypiUiBuildIdFromRecords($records),
        $manifest['packaged_ui_build_id']
    )) {
        throw new WsprryPiUiIdentityError('Manifest identity does not match its file records.');
    }
    return $manifest;
}

function wsprrypiUiLoadManifest(string $manifestPath): array
{
    if (!is_file($manifestPath) || !is_readable($manifestPath)) {
        throw new WsprryPiUiIdentityError('Packaged UI manifest is unavailable.');
    }
    $content = @file_get_contents($manifestPath);
    if (!is_string($content)) {
        throw new WsprryPiUiIdentityError('Packaged UI manifest could not be read.');
    }
    try {
        $manifest = json_decode($content, true, 512, JSON_THROW_ON_ERROR);
    } catch (JsonException $error) {
        throw new WsprryPiUiIdentityError('Packaged UI manifest is malformed.', 0, $error);
    }
    return wsprrypiUiValidateManifest($manifest);
}

function wsprrypiUiUnknownIdentity(
    string $error,
    ?string $packagedBuildId = null,
    ?string $installedBuildId = null
): array {
    return [
        'schema_version' => WSPRRYPI_UI_MANIFEST_SCHEMA_VERSION,
        'installed_state' => 'unknown',
        'packaged_ui_build_id' => $packagedBuildId,
        'installed_ui_build_id' => $installedBuildId,
        'modified_files' => [],
        'added_files' => [],
        'missing_files' => [],
        'error' => $error,
    ];
}

function wsprrypiUiIdentityStatus(
    ?string $uiRoot = null,
    ?string $manifestPath = null
): array {
    $root = $uiRoot ?? __DIR__;
    $manifest = $manifestPath ?? $root . DIRECTORY_SEPARATOR . WSPRRYPI_UI_MANIFEST_FILENAME;
    try {
        $installedRecords = wsprrypiUiBuildFileRecords($root);
        $installedBuildId = wsprrypiUiBuildIdFromRecords($installedRecords);
    } catch (WsprryPiUiIdentityError $error) {
        return wsprrypiUiUnknownIdentity($error->getMessage());
    }

    try {
        $packagedManifest = wsprrypiUiLoadManifest($manifest);
    } catch (WsprryPiUiIdentityError $error) {
        return wsprrypiUiUnknownIdentity(
            $error->getMessage(),
            null,
            $installedBuildId
        );
    }

    $packagedByPath = [];
    foreach ($packagedManifest['files'] as $record) {
        $packagedByPath[$record['path']] = $record['sha256'];
    }
    $installedByPath = [];
    foreach ($installedRecords as $record) {
        $installedByPath[$record['path']] = $record['sha256'];
    }

    $modifiedFiles = [];
    foreach (array_intersect(array_keys($packagedByPath), array_keys($installedByPath)) as $path) {
        if (!hash_equals($packagedByPath[$path], $installedByPath[$path])) {
            $modifiedFiles[] = $path;
        }
    }
    $addedFiles = array_values(array_diff(array_keys($installedByPath), array_keys($packagedByPath)));
    $missingFiles = array_values(array_diff(array_keys($packagedByPath), array_keys($installedByPath)));
    foreach ([&$modifiedFiles, &$addedFiles, &$missingFiles] as &$paths) {
        usort($paths, static fn(string $left, string $right): int => strcmp($left, $right));
    }
    unset($paths);

    $packagedBuildId = $packagedManifest['packaged_ui_build_id'];
    $state = hash_equals($packagedBuildId, $installedBuildId) &&
        $modifiedFiles === [] && $addedFiles === [] && $missingFiles === []
        ? 'packaged'
        : 'locally_modified';
    return [
        'schema_version' => WSPRRYPI_UI_MANIFEST_SCHEMA_VERSION,
        'installed_state' => $state,
        'packaged_ui_build_id' => $packagedBuildId,
        'installed_ui_build_id' => $installedBuildId,
        'modified_files' => $modifiedFiles,
        'added_files' => $addedFiles,
        'missing_files' => $missingFiles,
        'error' => null,
    ];
}

function getWsprryPiInstalledUiBuildId(): string
{
    static $buildId = null;
    if ($buildId !== null) {
        return $buildId;
    }
    $identity = wsprrypiUiIdentityStatus();
    $buildId = is_string($identity['installed_ui_build_id'])
        ? $identity['installed_ui_build_id']
        : '';
    return $buildId;
}

function getWsprryPiUiBuildId(): string
{
    return getWsprryPiInstalledUiBuildId();
}

function wsprrypiAssetUrl(string $path): string
{
    $buildId = getWsprryPiInstalledUiBuildId();
    if ($buildId === '') {
        return $path;
    }

    $separator = strpos($path, '?') !== false ? '&' : '?';
    return $path . $separator . 'v=' . rawurlencode($buildId);
}
