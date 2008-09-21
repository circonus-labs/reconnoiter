<?php

class Reconnoiter_UUID {
  // Are you kidding me?  PHP doesn't have uuid_generate built-in?
  function generate($serverID='', $ip='') {
    if(!$serverID) $serverID=mt_rand(0,0xffff);
    if(!$ip) $ip=mt_rand(0,0xffffffff);
    $t=gettimeofday();
    return sprintf( '%08x-%04x-%04x-%04x-%08x%04x',
        $t['sec'] & 0xffffffff, $t['usec'] & 0xffff,
        mt_rand(0,0xffff), mt_rand(0,0xffff),
        $ip, $serverID);
  }
}

