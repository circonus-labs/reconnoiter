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
     $sidvars = array();
     $textvars = array();

     $vs = $template->variables();
     foreach($vs as $v => $d) {
       if(isset($d['SID'])) {
         $sidvars[] = $v;          	  
        }
	else if(isset($d['TEXT'])) {
	  $textvars[] = $v;
	}
      }
	 
	 $valid_sids = $template->sids();

	 foreach ($sidvars as $sv) {
        	 $sidtext = "<span class='sidvarspan'>$sv</span>";
		 $jitem = array ('id' => 'sidvar',
	    	     	   'text' => $sidtext,
			   'hasChildren' => false,
			   'classes' => 'sidvar',
			   'params' => array(),
			   );
	         $bag[] = $jitem;

		 $target_sid_map = array();

		 foreach ($valid_sids[$sv] as $match) {
  	    	   if(!isset($target_sid_map[$match[target]])) {
	    	     $target_sid_map[$match[target]] = array();
	           }
	           $target_sid_map[$match[target]][] = $match[sid];
                 }

	 	 foreach ($target_sid_map as $target_name => $sid_list) {
	 	    $params = array();
	            $params['thetemplate'] = $templateid;
        	    $sidlist = implode(",", $sid_list);
        	    $params['targetname'] = $sidlist;
        	    $params['num_sids'] = $template->num_sids;
		    $params['thetarget'] = $target_name;
		    $params['sidvarlist'] = implode(",", $sidvars);
		    $params['textvarlist'] = implode(",", $textvars);

        	    $jitem = array ('id' => $target_name,
	    	     	   'text' => $target_name,
			   'hasChildren' => true,
			   'classes' => $sv,	
			   'params' => $params
			   );
        	    $bag[] = $jitem;
        	 }
         }

	 foreach($textvars as $tv) {
	   $params = array();
	   $ctext = $tv.":";
	   $ctext.="<input type='text' size='15' name='".$tv."' id='textvar'>";
	   $jitem = array ('id' => 'textvar',
	    	     	   'text' => $ctext,
			   'hasChildren' => false,
			   'classes' => $tv,
			   'params' => array(),
			   );
	   $bag[] = $jitem;
         }

     $ctext ="<span class='CreateGraphButton'>";
     $link=<<<LINK
<a href="javascript:CreateGraphFromTemplate('$templateid')">
LINK;
     $ctext.=$link;
     $ctext.="<b>Create Graph</b></a></span>";
     $ctext.="<span class='CreateGraph'></span>";
     $ctext.= <<<JS
<script type='text/javascript'>

function CreateGraphFromTemplate(templateid){

var sids = [];
var textvals = "";
var sidvals = "";

textvars = [];
sidvars =[];

//if we have selected atleast one sid for each SID placeholder
//and set values for TEXT placeholder, make the graphs
var have_req_txtvals = false;
var have_req_sidvals = false;


var template_e = $("#"+templateid);

template_e.find("input[@id='textvar']").each ( function (i) {
        textvars[i] = $(this).attr('name');
	if($(this).val() != '') {
	  have_req_txtvals = true;
          textvals+=  "&"+$(this).attr('name')+"="+$(this).val();
        }
	else have_req_txtvals = false;
});

template_e.find(".sidvar").each(function(i) {
    sidvars[i] = $(this).text();
});

var sidvar_sid_map = Array();
//this code to determine the sidvar is kind of hacky...
template_e.find(".sid_select :selected").each(function(i, selected) {
	 target_id = $(selected).attr("class");
	 sidvarclass = $(selected).parents(".collapsable:eq(0)").find("span").attr("class");
	 if(sidvar_sid_map[sidvarclass] == undefined) sidvar_sid_map[sidvarclass] = Array();
	 sval = parseInt($(selected).val());
	 sids[i] = sval;
	 sidvar_sid_map[sidvarclass].push(sval);
});

sidvars.sort();
textvars.sort();

for (i=0; i<sidvars.length; i++){
  if(sidvar_sid_map[sidvars[i]]) {
    sidvar_sid_map[sidvars[i]].sort();
    have_req_sidvals=true;
    sidvals+="&"+sidvars[i]+"="+sidvar_sid_map[sidvars[i]].join(",");
  }
  else {
    have_req_sidvals=false;
  }
}

if(have_req_txtvals && have_req_sidvals){
	var dataString = 'templateid='+templateid+'&textvars='+textvars.join(',')+'&sidvars='+sidvars.join(',')+textvals+sidvals;
	$.ajax({
		type: "POST",
	url: "template_graph.php",
	data: dataString,
	success: function() { }
	});
	template_e.find(".CreateGraph").html('Graph Saved').fadeIn('slow');
	template_e.find(".CreateGraph").html('Graph Saved').fadeOut('slow');
}
else {
     modal_warning("Graph creation error", "You need to select atleast one value for each SID placeholder ("+sidvars.join(",")+"), and each TEXT place holder ("+textvars.join(",")+")!");
}
}

</script>
JS;
$jitem = array ('id' => 'graphcreatebutton',
	    	     	   'text' => $ctext,
			   'hasChildren' => false,
			   'classes' => '',
			   'params' => array(),
			   );
$bag[] = $jitem;


}

else if($want == 'sid') {
     $thetemplateid = $_REQUEST['thetemplate'];
     $num_sids = $_REQUEST['num_sids'];
    
     $sid_list = explode (",", $_REQUEST[$l2]);
     $thetarget =  $_REQUEST['thetarget'];
     
     $ctext = "<select multiple='multiple' name='sid_select' size='5' id='sid_select' class='sid_select'>";
     $snames = $db->make_names_for_template($sid_list);
     
     $i=0;
     foreach ($snames as $tuple) {
        $ctext.="<option value='".$tuple['sid']."' class='".$thetarget."'> ".$tuple['option']."</option><br />";
     }

     $jitem = array ('id' => '',
	    	     	   'text' => $ctext,
			   'hasChildren' => false,
			   'classes' => 'templatesid',
			   'params' => array(),
			   );
    $bag[] = $jitem;
}
	 

echo json_encode($bag);
