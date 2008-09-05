<?php
global $graph_settings;

$fill = 30;
$colors[] = '#33aa33';
$colors[] = '#4a00dc';
$colors[] = '#caac00';
$colors[] = '#3377cc';
$colors[] = '#00acfa';
$colors[] = '#cc3333';

function fake_fill($color, $fill) {
  if(preg_match('/#([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})/',
                $color, $octets)) {
    $o0 = (255 - hexdec($octets[1])) * $fill / 100;
    $o1 = (255 - hexdec($octets[2])) * $fill / 100;
    $o2 = (255 - hexdec($octets[3])) * $fill / 100;
    return sprintf("#%02x%02x%02x", 255 - $o0, 255 - $o1, 255 - $o2);
  }
  return $color;
}

for($i=0; $i<100; $i++) {
  $settings = array();
  $settings['color_hover'] = fake_fill($colors[$i % count($colors)], 90);
  $settings['color'] = fake_fill($colors[$i % count($colors)], 80);
  $settings['fill_color'] = $colors[$i % count($colors)];
  $settings['fill_alpha'] = $fill;
  $graph_settings[] = $settings;
}

