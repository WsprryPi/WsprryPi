<?php

$query = $_GET;
$query['page'] = 'spots';
$location = 'index.php?' . http_build_query($query);

header('Location: ' . $location, true, 302);
exit;
