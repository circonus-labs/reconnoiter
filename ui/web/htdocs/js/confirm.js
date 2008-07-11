/*
 * SimpleModal Confirm Modal Dialog
 * http://www.ericmmartin.com/projects/simplemodal/
 * http://code.google.com/p/simplemodal/
 *
 * Copyright (c) 2008 Eric Martin - http://ericmmartin.com
 *
 * Licensed under the MIT license:
 *   http://www.opensource.org/licenses/mit-license.php
 *
 * Revision: $Id: confirm.js 132 2008-05-23 16:05:17Z emartin24 $
 *
 */

$(document).ready(function () {
	//$('#confirmDialog input:eq(0)').click(function (e) {
	$('#searchlist li a').click(function (e) {
		e.preventDefault();

		// example of calling the confirm function
		// you must use a callback function to perform the "yes" action
		confirm("Blend with current graph?", function () {
			window.location.href = 'http://';
		});
	});
});

function confirm(message, callback) {
	$('#confirm').modal({
		close:false, 
		overlayId:'confirmModalOverlay',
		containerId:'confirmModalContainer', 
		onShow: function (dialog) {
			dialog.data.find('.message').append(message);

			// if the user clicks "yes"
			dialog.data.find('.yes').click(function () {
				// call the callback
				if ($.isFunction(callback)) {
					callback.apply();
				}
				// close the dialog
				$.modal.close();
			});
		}
	});
}