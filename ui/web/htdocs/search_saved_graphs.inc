<script type="text/javascript">
<!--
function perform_graph_search(string, offset, limit) {
  $.getJSON('json/graph/search',
            { 'q' : string, 'o' : offset, 'l' : limit },
            function(r) {
              var summary = r.count + ' graph' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
              if(r.error) summary = 'Error: ' + htmlentities(r.error);
              $("#graphlist > p.graph-search-summary").html(summary).fadeIn('fast');
              var c = new Number(r.count);
              var l = new Number(r.limit);
              var o = new Number(r.offset);
              var page = $("#graphlist > p.paginate");
              page.html('');
              if(c > l) {
                if(o > 0) {
                  var po = Math.max(o-l, 0);
                  $('<a/>').html( (po+1) + ' - ' + (po+l) )
                           .click(function() {
                             perform_datapoint_search(string,po,r.limit);
                             return false;
                           }).appendTo(page);
                }
                page.append($('<span/>').html((o+1) + '-' + (o+l)).addClass('searchselect'));
                if(o + l < c) {
                  var po = o + l;
                  $('<a/>').html( (po + 1) + '-' + (po+l) )
                           .click(function() {
                             perform_datapoint_search(string,po,r.limit);
                             return false;
                           }).appendTo(page);
                }
                page.slideDown('fast');
              }
              else page.slideUp('fast');
              $("#graph-searchresults > li").remove();
              for(var i=0; r.results && i<r.results.length; i++) {
                var g = r.results[i];
                var a = $('<a href="#"/>');
                a.html('Edit').addClass('addtows');
                a.click(
                  (function(graphid) {
                      return function() {
                        set_current_graph_id(graphid);
                        return false;
                      }
                   })(g.graphid)
                );
                var li = $('<li/>').append($('<a/>').html(g.title))
                                   .append($('<ul/>').append($('<li/>').html(g.last_update))
                                                     .append($('<li/>').append(a)));
                $("#graph-searchresults").append(li);
              }
            });
}
$(document).ready(function(){
        $("#graph-searchform").submit(function() {
                perform_graph_search($("#graph-searchinput").val(), 0, 25);
                return false;
        });
});
-->
</script>

</script>
<h4 id="graph-searchterms"></h4>
<div id="graph-searchform-container" style="margin:.5em 0 2em 0;padding-bottom:1em;border-bottom: solid 1px #e6e4e5;">
<form id="graph-searchform">
  <fieldset>
  <legend style="display:none;">Search</legend>
  <label for="keyword" style="display:none;">Keyword</label> <input type="text" id="graph-searchinput" style="width:220px;" /> <input type="submit" value="Search" />
  </fieldset>
</form>
</div>
<div id="graphlist">
  <p class="graph-search-summary"></p>
  <ul id="graph-searchresults">
  </ul>
</div>
