/*
 * Async Treeview 0.1 - Lazy-loading extension for Treeview
 *
 * http://bassistance.de/jquery-plugins/jquery-plugin-treeview/
 *
 * Copyright (c) 2007 Jörn Zaefferer
 *
 * Dual licensed under the MIT and GPL licenses:
 *   http://www.opensource.org/licenses/mit-license.php
 *   http://www.gnu.org/licenses/gpl.html
 *
 * Revision: $Id$
 *
 */

(function($) {

  // In keeping (I hope) with your convention of storing IDs/Classes
  // in object literals
  var IDS = {
    status: 'msg_status',
    placeholder: 'placeholder'
  };

  var CLASSES = {
    hasChildren: 'hasChildren',
    placeholder: 'placeholder'
  };

  function load(settings, root, child, container)
  {
    // row-by-row URL params from JSON or fall back to default
    var params = settings.params[root] ? settings.params[root].params : {root: root};

    // status message (loading could take a bit)
    child.html('<span id="'+IDS.status+'">Loading....</span>');

    $.getJSON(settings.url, params, function (response)
    {
      $.each(response, createNode, [child]);
      $(container).treeview({add: child});
      // kill the status message
      $('#'+IDS.status).remove();
    });

    // Moved this out of the callback.  Easier (for me) to read and
    // saw no benefit to redefining it each on each XHR
    function createNode(parent)
    {
      var current = $("<li/>")
	.attr("id", this.id || "")
	.html("<span>" + this.text + "</span>")
	.appendTo(parent);

      // Object, indexed by id, of url params
      // Current approach allows for multiple params to be passed to
      //   server as well as different params/values per JSON row.
      // Pretty sure this approach won't work if you wanted a less
      //   granular approach (ie, all first level nodes get X=Y), but,
      //   IMO, the server can handle that logic and construct the
      //   appropriate JSON response (still return X=Y for each row).
      // Not married to this implementation, just needed a way for
      //   getJSON (by way of load()) to access the URL params and
      //   the settings variable seemed appropriate
      if (this.params)  { settings.params[this.id] = {params: this.params}; }

      if (this.classes) { current.children("span").addClass(this.classes); }
      if (this.expanded){ current.addClass("open"); }

      if (this.hasChildren || this.children && this.children.length) {
	var branch = $("<ul/>").appendTo(current);
	if (this.hasChildren){
	  current.addClass(CLASSES.hasChildren);
	  createNode.call({text: CLASSES.placeholder,
			   id: IDS.placeholder,
			   children: []
			  }, branch);
	}
	if (this.children && this.children.length) {
	  $.each(this.children, createNode, [branch]);
	}
      }
    } // createNode

  } // load

  var proxied = $.fn.treeview;
  $.fn.treeview = function(settings)
  {
    if (!settings.url) { return proxied.apply(this, arguments); }

    // not set in the constructor, so create it here
    if (!settings.params){ settings.params={}; }

    var container = this;
    load(settings, "source", this, container);
    var userToggle = settings.toggle;
    return proxied.call(this, $.extend({}, settings, {
      collapsed: true,
      toggle: function()
      {
	var $this = $(this);

	// If you want to get make a new request each time the branch is re-opened
	if (settings.reloadJSON){
	  var childList = $this.find("ul").empty();
	  if ($this.hasClass(proxied.classes.collapsable)){
	    load(settings, this.id, childList, container);
	  }
	} else if ($this.hasClass(CLASSES.hasChildren)) {
	  var childList = $this.removeClass(CLASSES.hasChildren).find("ul").empty();
	  load(settings, this.id, childList, container);
	}

	if (userToggle) { userToggle.apply(this, arguments); }
      }
    })); // return

  }; //treeview

})(jQuery);