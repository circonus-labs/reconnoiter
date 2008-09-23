<script type="text/javascript">
<!--
function perform_datapoint_search(string, offset, limit) {
  $.getJSON('json/datapoint/search',
            { 'q' : string, 'o' : offset, 'l' : limit },
            function(r) {
              $("#searchresults > li").remove();
              for(var i=0; i<r.results.length; i++) {
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
<p id="searchsummary"></p>
<div id="datapoint-searchform-container" style="margin:.5em 0 2em 0;padding-bottom:1em;border-bottom: solid 1px #e6e4e5;">
<form id="datapoint-searchform">
  <fieldset>
  <legend style="display:none;">Search</legend>
  <label for="keyword" style="display:none;">Keyword</label> <input type="text" id="searchinput" style="width:220px;" /> <input type="submit" value="Search" />
  </fieldset>
</form>
</div>
<div id="searchlist">
  <ul id="searchresults">
  </ul>
</div>
