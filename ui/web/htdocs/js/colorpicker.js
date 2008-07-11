jQuery(function($) {
	
	$('.ct a').each(function() {
		$(this).css('background-color', $(this).attr('rel'));		
	});

	$('.ct').toggle();

	$('.ct a').click(function() {
		$(this).parent().siblings('.cpb').css('background-color', $(this).attr('rel'));	
		$(this).parent().siblings('.cf').val($(this).attr('rel'));
		$(this).parent().toggle();
		return false;
	});

});

function drawCP(field_id) {
	
	var colors = new Array(128);
	colors = ['FF0000', 'FFFF00', '00FF00', '00FFFF', '0000FF', 'FF00FF', 'FFFFFF', 'EBEBEB',
			  'E1E1E1', 'D7D7D7', 'CCCCCC', 'C2C2C2', 'B7B7B7', 'ACACAC', 'A0A0A0', '959595',
			  'ED1C24', 'FFF200', '00A650', '00ADEF', '2E3092', 'EC00BC', '898989', '7C7C7C',
			  '707070', '626262', '545454', '464646', '363636', '252525', '111111', '000000',
			  'F69679', 'F9AD81', 'FDC689', 'FFF799', 'C4DF9B', 'A2D39C', '82CA9C', '7ACCC8',
			  '6DCFF6', '7DA7D8', '8393CA', '8781BD', 'A186BE', 'BC8CBF', 'F49AC1', 'F5989D',
			  'F26C4F', 'F68E56', 'F68E56', 'FFF468', 'ACD373', '7CC576', '3CB878', '1CBBB4',
			  '00BFF3', '448CCA', '5574B9', '605CA8', '8560A8', 'A863A8', 'F06EA9', 'F26D7D',
			  'ED1C24', 'F26522', 'F7941D', 'FFF200', '8DC63F', '39B44A', '00A650', '00A99D',
			  '00ADEF', '0072BC', '0054A6', '2E3092', '662D91', '92278F', 'EC008C', 'ED145A',
			  '9D0A0E', 'A0410D', 'A36109', 'ABA000', '598527', '197A30', '007236', '00736A',
			  '0076A3', '004A80', '003471', '1B1464', '440E62', '620460', '9E005D', '9D0039',
			  '790000', '7B2E00', '7D4900', '3F6618', '005E20', '005E20', '005825', '005951',
			  '005B7F', '003663', '002157', '0D004C', '32004B', '4B0049', '7B0046', '790026',
			  'C7B299', '988675', '736257', '534741', '362F2D', 'C69C6D', 'A57C52', '8C6239',
			  '754C24', '603913' ];
	
	
	document.write('<div class="cp">');
	//document.write('	<input type="text" id="'+field_id+'" class="cf" size="7">');
	document.write('	<a href="#" onclick="$(this).siblings(\'.ct\').toggle(); return false;" class="cpb">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</a>');
	document.write('	<div class="ct">');
	
	for (var i=0; i<colors.length; i++) {
		document.write('		<a href="#" rel="#'+colors[i]+'"><img src="images/blank.gif" width="13" height="13" alt="#'+colors[i]+'"></a>');
	};
	
	document.write('	</div>');
	document.write('</div>');
}