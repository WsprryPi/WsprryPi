<?php
require_once __DIR__ . '/../data/ui_version.php';

function requireTrue(bool $condition, string $message): void
{
    if (!$condition) {
        throw new RuntimeException($message);
    }
}

function removeTree(string $root): void
{
    if (!is_dir($root)) {
        return;
    }
    $iterator = new RecursiveIteratorIterator(
        new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS),
        RecursiveIteratorIterator::CHILD_FIRST
    );
    foreach ($iterator as $entry) {
        if ($entry->isDir() && !$entry->isLink()) {
            rmdir($entry->getPathname());
        } else {
            unlink($entry->getPathname());
        }
    }
    rmdir($root);
}

$root = sys_get_temp_dir() . '/wsprrypi-ui-identity-' . bin2hex(random_bytes(8));
$manifestPath = $root . '/ui-manifest.json';
mkdir($root . '/views', 0700, true);
mkdir($root . '/cache', 0700, true);
mkdir($root . '/backups', 0700, true);
file_put_contents($root . '/site.js', "const packaged = true;\n");
file_put_contents($root . '/views/index.php', "<?php echo 'packaged';\n");
file_put_contents($root . '/café.css', "body { color: black; }\n");

try {
    $generator = realpath(__DIR__ . '/../scripts/generate_ui_manifest.py');
    requireTrue(is_string($generator), 'manifest generator was not found');
    $command = implode(' ', [
        'python3',
        escapeshellarg($generator),
        '--ui-root', escapeshellarg($root),
        '--output', escapeshellarg($manifestPath),
        '--source-commit', escapeshellarg(str_repeat('c', 40)),
        '--application-version', escapeshellarg('1.2.3'),
    ]);
    exec($command, $output, $status);
    requireTrue($status === 0, 'Python manifest generation failed');

    $manifest = json_decode(file_get_contents($manifestPath), true, 512, JSON_THROW_ON_ERROR);
    $packaged = wsprrypiUiIdentityStatus($root, $manifestPath);
    requireTrue(
        $packaged['installed_state'] === 'packaged',
        'matching UI must be packaged: ' . json_encode($packaged)
    );
    requireTrue(
        $packaged['installed_ui_build_id'] === $manifest['packaged_ui_build_id'],
        'PHP and Python identities must match'
    );
    requireTrue($packaged['modified_files'] === [], 'packaged UI must not report modifications');

    $edited = "const userEdited = true;\n";
    file_put_contents($root . '/site.js', $edited);
    file_put_contents($root . '/custom.css', "body { color: purple; }\n");
    unlink($root . '/views/index.php');
    file_put_contents($root . '/cache/runtime.json', "runtime\n");
    file_put_contents($root . '/backups/site.js', "old\n");

    $modified = wsprrypiUiIdentityStatus($root, $manifestPath);
    requireTrue($modified['installed_state'] === 'locally_modified', 'edits must be local');
    requireTrue($modified['modified_files'] === ['site.js'], 'changed file inventory mismatch');
    requireTrue($modified['added_files'] === ['custom.css'], 'added file inventory mismatch');
    requireTrue($modified['missing_files'] === ['views/index.php'], 'missing inventory mismatch');
    requireTrue(file_get_contents($root . '/site.js') === $edited, 'classification changed user edit');

    $unsafe = $manifest;
    $unsafe['files'][0]['path'] = '../outside.js';
    file_put_contents($manifestPath, json_encode($unsafe));
    $unknown = wsprrypiUiIdentityStatus($root, $manifestPath);
    requireTrue($unknown['installed_state'] === 'unknown', 'unsafe manifest must be unknown');
    requireTrue(is_string($unknown['installed_ui_build_id']), 'installed identity should remain available');
    requireTrue($unknown['packaged_ui_build_id'] === null, 'unsafe packaged identity must not be trusted');

    unlink($manifestPath);
    $missingManifest = wsprrypiUiIdentityStatus($root, $manifestPath);
    requireTrue($missingManifest['installed_state'] === 'unknown', 'missing manifest must be unknown');
    requireTrue(
        is_string($missingManifest['installed_ui_build_id']),
        'missing manifest must not suppress installed identity'
    );
} finally {
    removeTree($root);
}

echo "ui_identity_test passed\n";
