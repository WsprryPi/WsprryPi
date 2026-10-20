<?php require_once __DIR__ . '/html_cache_headers.php'; ?>
<!DOCTYPE html>
<html lang="en" data-bs-theme="auto">

<head>
    <!-- Bootswatch, Boostrap, and Fontawesome, included here: -->
    <?php require_once 'header.php'; ?>

    <!-- Template css -->
    <!-- Add page-specific CSS here -->
</head>

<?php
$cardClass = 'template-card';
require_once 'page_shell_start.php';
?>
            <?php
            $cardTitleId = 'cardTitle';
            $cardTitleText = 'Card Title';
            require_once 'card_header.php';
            ?>

            <!-- Card Body -->
            <div class="card-body tab-content bg-body">
                <!-- Card body goes here -->
            </div>
<?php require_once 'page_shell_end.php'; ?>

    <!-- Static page footer -->
    <?php require_once 'footer.php'; ?>

    <!-- Template JavaScript -->
    <!-- Add page-specific JS here -->
</body>

</html>
