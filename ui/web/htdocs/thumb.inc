<script type="text/javascript" src="js/swfobject.js"></script>
<?php

function thumb_flash($id) {
?>
	<div id="<?php print $id ?>" style="display:inline; width: 290px;">
		<strong>You need to upgrade your Flash Player</strong>
	</div>

	<script type="text/javascript">
		// <![CDATA[		
		var so = new SWFObject("amcharts/amline/amline.swf", "amline", "280", "100", "8", "#FFFFFF");
		so.addVariable("path", "amcharts/amline/");
		so.addVariable("settings_file", escape("thumb_settings.php?id=<?php print $id ?>&start=<?php print $_GET['start'] ?>&end=<?php print $_GET['end'] ?>&cnt=100"));
		so.addVariable("preloader_color", "#999999");
		so.write("<?php print $id ?>");
		// ]]>
	</script>
<?php
}
?>
