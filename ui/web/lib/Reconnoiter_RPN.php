<?php

class Reconnoiter_RPN {
  function rpn_eval($value, $expr) {
    $s = array();
    $ops = explode(",", $expr);
    array_unshift($s, $value);
    foreach($ops as $op) {
      switch($op) {
        case 'ln':
          $v = array_shift($s);
          array_unshift($s, is_null($v) ? $v : log($v)); break;
        case 'round':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, is_null($l) ? $l : round($l,$r));
          break;
        case 'floor':
          $v = array_shift($s);
          array_unshift($s, is_null($v) ? $v : floor($v)); break;
        case 'ceil':
          $v = array_shift($s);
          array_unshift($s, is_null($v) ? $v : ceil($v)); break;
        case 'log':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, is_null($l) ? $l : log($l,$r));
          break;
        case 'e':
          array_unshift($s, exp(1)); break;
        case 'pi':
          array_unshift($s, pi()); break;
        case '^':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, (is_null($r) || is_null($l)) ? NULL : pow($l,$r));
          break;
        case '-':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, (is_null($r) || is_null($l)) ? NULL : ($l-$r));
          break;
        case '/':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, (is_null($r) || is_null($l) || $r == 0) ? NULL : ($l/$r));
          break;
        case '.':
          array_unshift($s, $s[array_shift($s)]); break;
        case '+':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, (is_null($r) || is_null($l)) ? NULL : ($l+$r));
          break;
        case '*':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, (is_null($r) || is_null($l)) ? NULL : ($l*$r));
          break;
        case 'auto':
          $v = array_shift($s);
          array_unshift($s, is_null($v) ? $v : $this->autounits($v)); break;
        case 'min':
          $r = array_shift($s);
          $l = array_shift($s);
          if(is_null($r)) array_unshift($s,$l);
          else if(is_null($l)) array_unshift($s,$r);
          else array_unshift($s, min($r,$l));
          break;
        case 'max':
          $r = array_shift($s);
          $l = array_shift($s);
          if(is_null($r)) array_unshift($s,$l);
          else if(is_null($l)) array_unshift($s,$r);
          else array_unshift($s, max($r,$l));
          break;
        default:
          if(preg_match('/^-?\d+$/', $op)) {
            array_unshift($s, $op);
          }
      }
    }
    $value = array_shift($s);
    return $value;
  }
}
