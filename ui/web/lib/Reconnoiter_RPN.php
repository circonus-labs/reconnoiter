<?php

class Reconnoiter_RPN {
  function rpn_eval($value, $expr) {
    $s = array();
    $ops = explode(",", $expr);
    array_unshift($s, $value);
    foreach($ops as $op) {
      switch($op) {
        case 'ln':
          array_unshift($s, log(array_shift($s))); break;
        case 'round':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, round($l,$r));
          break;
        case 'floor':
          array_unshift($s, floor(array_shift($s))); break;
        case 'ceil':
          array_unshift($s, ceil(array_shift($s))); break;
        case 'log':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, log($l,$r));
          break;
        case 'e':
          array_unshift($s, exp(1)); break;
        case 'pi':
          array_unshift($s, pi()); break;
        case '^':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, pow($l,$r));
          break;
        case '-':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, $l-$r);
          break;
        case '/':
          $r = array_shift($s);
          $l = array_shift($s);
          array_unshift($s, $l/$r);
          break;
        case '.':
          array_unshift($s, $s[array_shift($s)]); break;
        case '+':
          array_unshift($s, array_shift($s) + array_shift($s)); break;
        case '*':
          array_unshift($s, array_shift($s) * array_shift($s)); break;
        case 'auto':
          array_unshift($s, $this->autounits(array_shift($s))); break;
        case 'min':
          array_unshift($s, min(array_shift($s),array_shift($s))); break;
        case 'max':
          array_unshift($s, max(array_shift($s),array_shift($s))); break;
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
