$(document).ready(function(){
  $(".accordion h3:first").addClass("active");
  $(".accordion span").slideUp();
  $(".accordion span:first").slideDown();

  $("div#content > div:first").siblings().slideUp();

  $(".accordion h3").click(function(){
      $(this).next("span").slideToggle("normal")
      .siblings("span:visible").slideUp("normal");
      $(this).toggleClass("active");
      $(this).siblings("h3").removeClass("active");
      $(this).siblings("h3").each(function(e) {
        $("#" + $(this).attr("id") + "_panel").slideUp("fast");       });
      if($(this).hasClass("active"))
        $("#" + $(this).attr("id") + "_panel").slideDown("fast");
      else
        $("#" + $(this).attr("id") + "_panel").slideUp("fast");
  });

  var tabContainers = $('div.tabs > div');
  tabContainers.hide().filter(':first').show();

  $('div.tabs ul.tabNavigation a').click(function () {
          tabContainers.hide();
          tabContainers.filter(this.hash).show();
          $('div.tabs ul.tabNavigation a').removeClass('selected');
          $(this).addClass('selected');
          return false;
  }).filter(':first').click();
  var wstabContainers = $('div.ws-tabs > div');
  wstabContainers.hide().filter(':first').show();
   
  $('div.ws-tabs ul.tabNavigation a').click(function () {
    wstabContainers.hide();
    wstabContainers.filter(this.hash).show();
    $('div.ws-tabs ul.tabNavigation a').removeClass('selected');
    $(this).addClass('selected');
    return false;
  }).filter(':first').click();

  $("#ws-searchform").submit(function() {
    perform_ws_search_edit({ domid: '#worksheetlist',
                             search: $("#ws-searchinput").val(),
                             offset: 0,
                             limit: 25 });
    return false;
  });
  $("#ws-graph-searchform").submit(function() {
    perform_graph_search_add({ 'domid': '#ws-graphlist',
                               'search': $("#ws-graph-searchinput").val(),
                               'offset': 0,
                               'limit': 25 });
    return false;
  });

  perform_ws_search_edit({ domid: '#worksheetlist',
                           search: '',
                           offset: 0,
                           limit: 25 });

  $("#templates").treeview({
    url: "json/templates/templateid/targetname/sid",
    params: {}
  });
  $("#targets").treeview({
    url: "json/ds/remote_address/target/name/metric_name",
    params: {} // will become hash, indexed by id, of url params
  });

  var state = false;

  $("h2#worksheetTitle").editable(function(value, settings) {
    wsinfo.title = value;
    worksheet.update();
    return(value);
  }, { });

  $(".editWorksheet").click(function() {
    if(worksheet.islocked()){
      worksheet.unlock();
      $(".editWorksheet").html('Editing!').fadeIn('slow');
    }
    else {
      worksheet.lock();
      $(".editWorksheet").html('Edit Worksheet').fadeIn('slow');
    }
  });


  $(".blankWorksheet").click(function() {
    worksheet.load();
  });

  $("#ws_datetool .btn-slide").click(function(){
    $("#ws_widgetCalendar").stop().animate({
      height: state ? 0 :
                $('#ws_widgetCalendar div.datepicker').get(0).offsetHeight
    }, 500);
    state = !state;
    $(this).toggleClass("active");
    return false;
  });
  $("#ws_datetool .datechoice").click(function(){
    $("#ws_datetool .range a.btn-slide").html("YYYY/MM/DD - YYYY/MM/DD");
    $("#ws_widgetCalendar").slideUp("slow");
    $("#ws_datetool .datechoice").removeClass("selected");
    $(this).addClass("selected");
    worksheet.display_info(time_window_to_seconds($(this).html()), '');
    //time_windows[$(this).html()], '');
    worksheet.refresh();
    return false;
  });
  $('#ws_widgetCalendar').DatePicker({
    flat: true,
    format: 'Y/m/d',
    date: [new Date(), new Date()],
    calendars: 3,
    mode: 'range',
    starts: 1,
    onChange: function(formated) {
      var dates;
      dates = formated[0].split('/');
      var start = new Date(dates[0], dates[1]-1, dates[2], 0, 0, 0);
      dates = formated[1].split('/');
      var end = new Date((new Date(dates[0], dates[1]-1, dates[2], 0, 0, 0)).getTime() + 86400000);
      worksheet.display_info(start.toUTCString(), end.toUTCString());
      worksheet.refresh();
      $("#ws_datetool .datechoice").removeClass("selected");
      $('#ws_datetool .range a.btn-slide').get(0).innerHTML = formated.join(' - ');
    }
  });
  $("#ws-tool-error").click(function(){
    $("#ws-tool-error").fadeOut("slow");
  });
  $("#graph-searchform").submit(function() {
    perform_graph_search_edit({ 'domid': '#graphlist',
                                'search': $("#graph-searchinput").val(),
                                'offset': 0,
                                'limit': 25 });
    return false;
  });
  $("#datapoint-searchform").submit(function() {
    perform_datapoint_search_add({ domid: '#searchlist',
                                   search: $("#searchinput").val(),
                                   offset: 0,
                                   limit: 25 });
    return false;
  });

});

