<?php

require_once 'Reconnoiter_DataContainer.php';

class Reconnoiter_amCharts_Driver extends Reconnoiter_DataContainer {
  function graph_attrs() {
    return array(
      'axis', 'gid','title','color','fill_color','fill_alpha','color_hover',
      'balloon_color','balloon_alpha','balloon_text_color','balloon_text',
      'bullet','bullet_size','bullet_color','visible_in_legend','selected'
    );
  }
  function __construct($start, $end, $cnt) {
    parent::__construct($start, $end, $cnt);
  }
  function defaultDataSetAttrs($uuid, $name, $derive, $attrs) {
    if(!isset($attrs['gid'])) $attrs['gid'] = "$uuid-$name";
    if(!isset($attrs['title'])) $attrs['title'] = "$uuid-$name";
    if(!isset($attrs['fill_alpha'])) $attrs['fill_alpha'] = "20";
    if(!isset($attrs['selected'])) $attrs['selected'] = "false";
    return parent::defaultDataSetAttrs($uuid, $name, $derive, $attrs);
  }
  function defaultChangeSetAttrs($uuid, $name, $derive, $attrs) {
    if(!isset($attrs['gid'])) $attrs['gid'] = "$uuid-$name";
    if(!isset($attrs['title'])) $attrs['title'] = "$uuid-$name";
    if(!isset($attrs['balloon_text'])) $attrs['balloon_text'] = "{description}";
    if(!isset($attrs['balloon_color'])) $attrs['balloon_color'] = "#000000";
    if(!isset($attrs['balloon_alpha'])) $attrs['balloon_alpha'] = "70";
    if(!isset($attrs['balloon_text_color'])) $attrs['balloon_text_color'] = "#ffffff";
    if(!isset($attrs['bullet_size'])) $attrs['bullet_size'] = "12";
    if(!isset($attrs['bullet_color'])) $attrs['bullet_color'] = "#000000";
    if(!isset($attrs['bullet'])) $attrs['bullet'] = "round_outlined";
    if(!isset($attrs['visible_in_legend'])) $attrs['visible_in_legend'] = "false";
    return parent::defaultChangeSetAttrs($uuid, $name, $derive, $attrs);
  }
  function graphsXML() {
    print "<graphs>\n";
    $this->eachGraphXML();
    print "</graphs>\n";
  }
  function eachGraphXML() {
    foreach($this->sets as $name => $set) {
      $this->graphXML($set, $this->sets_config[$name]);
    }
  }
  function graphXML($set, $config) {
    print "<graph";
    foreach ($this->graph_attrs() as $attr) {
      if(isset($config[$attr])) print " $attr=\"".$config[$attr]."\"";
    }
    print ">\n";
    $this->graphDataXML($set, $config);
    print "</graph>\n";
  }
  // Each amChart expects a subtley different XML for the graph.
  // graphDataXML(Reconnoiter_DataSet) is implemented by the subclass
  function guidesXML() {
    print "<guides>\n";
    print "<max_min>true</max_min>\n";

    $this->eachGuideXML();
    print "</guides>\n";
  }
  function eachGuideXML() {
    foreach($this->guides as $name => $value) {
      print "<guide>\n";
      $this->guideXML($value, $this->guides_config[$name]);
      print "</guide>\n";
    }
  }
  // So far, guides seems to be consistently implemented (or ignored)
  function guideXML($value, $config) {
    if($value != "") {
      if(isset($config['expression'])) {
        $expr = $config['expression'];
        eval("\$value = $expr;");
      }
    }
    print "<start_value>$value</start_value>\n";
    if(isset($config['title'])) {
      $expr = $config['title'];
      eval("\$title = $expr;");
      print "<title>$title</title>\n";
    } else {
      print "<title>$value</title>\n";
    }
    if(isset($config['color'])) {
      $expr = $config['color'];
      eval ("\$color = $expr;");
      print "<color>$color</color>\n";
    } else {
      print "<color>#ff0000</color>\n";
    }
    print "<inside>true</inside>\n";
  }
}
