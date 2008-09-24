<script type="text/javascript">
<!--
function perform_datapoint_search(string, offset, limit) {
  $.getJSON('json/datapoint/search',
            { 'q' : string, 'o' : offset, 'l' : limit },
            function(r) {
              var summary = 'Found ' + r.count + ' data point' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
              if(r.error) summary = 'Error: ' + htmlentities(r.error);
              $("#searchlist > p.search-summary").html(summary).fadeIn('fast');
              var c = new Number(r.count);
              var l = new Number(r.limit);
              var o = new Number(r.offset);
              var page = $("#searchlist > p.paginate");
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
              $("#searchresults > li").remove();
              for(var i=0; r.results && i<r.results.length; i++) {
                var ds = r.results[i];
                var a = $('<a href="#"/>');
                var li = $('<li/>');
                a.html(ds.target + '`' + ds.name + '`' + ds.metric_name);
                a.click(
                  (function(ds_c) {
                      return function() {
                        graph_add_datapoint({'sid':ds_c.sid,
                                             'name':ds_c.target + '`' + ds_c.metric_name,
                                             'metric_name':ds_c.metric_name,
                                             'metric_type':ds_c.metric_type
                                            });
                        return false;
                      }
                   })(ds)
                );
                if(ds.metric_type == 'text') li.addClass('txt');
                $("#searchresults").append(li.append(a));
              }
            });
}
$(document).ready(function(){
        $("#datapoint-searchform").submit(function() {
                perform_datapoint_search($("#searchinput").val(), 0, 25);
                return false;
        });
});
-->
</script>
<h4 id="searchterms"></h4>
<div id="datapoint-searchform-container" style="margin:.5em 0 2em 0;padding-bottom:1em;border-bottom: solid 1px #e6e4e5;">
<form id="datapoint-searchform">
  <fieldset>
  <legend style="display:none;">Search</legend>
  <label for="keyword" style="display:none;">Keyword</label> <input type="text" id="searchinput" style="width:220px;" /> <input type="submit" value="Search" />
  </fieldset>
</form>
</div>
<div id="searchlist">
  <p class="search-summary"/>
  <p class="paginate"/>
  <ul id="searchresults">
  </ul>
</div>
