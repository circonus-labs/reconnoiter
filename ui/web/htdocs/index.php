<?php

require_once 'Reconnoiter_DB.php';

$db = Reconnoiter_DB::getDB();

$targets = $db->get_targets();

?>
<html>
<body>
<?php require_once('thumb.inc'); ?>
<ul>
<?php foreach($targets as $target) { ?>
  <li><?php print $target ?></li>
  <ul>
  <?php foreach($db->get_checks($target) as $name => $check) { ?>
    <li>
      <a href="bw_graph.php?id=<?php print $check['sid'] ?>"><?php print $name ?></a><br />
      <?php  0 && thumb_flash($id) ?>
      <ul>
        <?php foreach($check['numeric'] as $n) { ?>
        <li><a href="generic_graph.php?metric=nl-<?php print $check['sid'] ?>-<?php print $n ?>&cnt=1400"><?php print $n ?></a></li>
        <?php } ?>
      </ul>
      <ul>
        <?php foreach($check['text'] as $n) { ?>
        <li>Text: <?php print $n ?></li>
        <?php } ?>
      </ul>
    </li>
  <?php } ?>
  </ul>
<?php } ?>
</ul>
</body>
</html>
