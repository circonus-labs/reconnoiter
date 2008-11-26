<?php 

require_once 'Reconnoiter_DB.php';
require_once 'Reconnoiter_GraphTemplate.php';

$db = Reconnoiter_DB::getDB();

//these are the names of the parameters , one of templateid, targetname, or sid
$l1 = $_GET['l1'];
$l2 = $_GET['l2'];
$l3 = $_GET['l3'];

if ($_REQUEST['root'] == 'source'){
  $want = $l1;
}
else if($_REQUEST[$l1]) {
  $want = $l2;
}
else if($_REQUEST[$l2]) {
     $want = $l3;
}
else {
     $want = "we are screwed";
}

$templateid;
$template;

if($_REQUEST[$l1]){
	$templateid = $_REQUEST[$l1];
	$template = new Reconnoiter_GraphTemplate($templateid);
}

$bag = array();

if($want == 'templateid') {
	 foreach($db->get_templates() as $item) {
	 $params = array();
	 $params['templateid'] = $item['templateid'];
	 $params['title'] = $item['title'];
	 $params['json'] = $item['json'];	 

	 $jitem = array ('id' => $item['templateid'],
	 	  	'text' => $item['title'], 
			'hasChildren' => true, 
			'classes' => $want, 
			'params' => $params,
			);
	 $bag[] = $jitem;
	 }
}

else if($want == 'targetname') {         

	 $target_sid_map = array();

	 foreach ($template->sids() as $item) {	
	    foreach ($item as $match) {
	    	    if(!isset($target_sid_map[$match[target]])) {
		    	$target_sid_map[$match[target]] = array();
		    }
		       $target_sid_map[$match[target]][] = $match[sid];
	    }
         }
	 
	 foreach ($target_sid_map as $target_name => $sid_list) {
	    $params = array();
	    $params['thetemplate'] = $templateid;
	    $sidlist = implode(",", $sid_list);
	    $params['targetname'] = $sidlist;
	    $params['num_sids'] = $template->num_sids;
	    $jitem = array ('id' => $target_name,
	    	     	   'text' => $target_name,
			   'hasChildren' => true,
			   'classes' => $want,
			   'params' => $params
			   );
	    $bag[] = $jitem;
	 }
}
else if($want == 'sid') {
     $thetemplateid = $_REQUEST['thetemplate'];
     $num_sids = $_REQUEST['num_sids'];

     $sid_list = explode (",", $_REQUEST[$l2]);
     $ctext = "<form id='template-graph' name='template-graph' action=''>";     
     $ctext.= "<select multiple='multiple' name='sid_select' size='5' id='sid_select' >";
     foreach($sid_list as $sid) {
     	$sname = $db->make_name_for_template($sid);
	$ctext.="<option value='$sid'> $sname</option><br />";
     }

     $ctext.="</select><br>Select ".$num_sids." from above.";     
     $ctext .= "<br>Graph Title:";
     $ctext.="<input type='text' size='15' name='graph_name' id='graph_name'><br>";
     $ctext.="<span class='CreateGraphButton'>";
     $link=<<<LINK
<a href="javascript:CreateGraphFromTemplate('$thetemplateid', '$num_sids')">
LINK;
     $ctext.=$link;
     $ctext.="<b>Create Graph</b></a></span></form>";
     $ctext.="<span class='CreateGraph'></span>";
     $ctext.= <<<JS
<script type='text/javascript'>

function CreateGraphFromTemplate(templateid, num_sids){

var graph_name = $("#graph_name").val();
var sids = [];

$("#sid_select :selected").each(function(i, selected){
	 sids[i] = $(selected).val();
});

if(sids.length==num_sids){
	var dataString = 'templateid='+templateid+'&graph_name='+graph_name+'&sids='+sids;

	$.ajax({
		type: "POST",
	url: "template_graph.php",
	data: dataString,
	success: function() { }
	});
	$(".CreateGraph").html('Graph Saved').fadeIn('slow');
	$(".CreateGraph").html('Graph Saved').fadeOut('slow');
}
else {
     alert("Did not specify correct number of metrics!")
}
}

</script>
JS;

     $jitem = array ('id' => '',
	    	     	   'text' => $ctext,
			   'hasChildren' => false,
			   'classes' => 'templatesid',
			   'params' => array(),
			   );
    $bag[] = $jitem;
}
	 

echo json_encode($bag);
