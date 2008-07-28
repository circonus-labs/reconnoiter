			<p><a href="">username</a> / <a href="#">worksheet</a></p>
			<h2>NGM - My shot disk usage</h2>
			
			<div id="datetool">
				<div id="zoom">
					<dl>
						<dt>Zoom:</dt>
						<dd><a href="" class="first" onclick="disp_alert()">2d</a></dd>
						<dd><a href="" onclick="disp_alert()">1w</a></dd>
						<dd><a href="" class="selected" onclick="disp_alert()">2w</a></dd>
						<dd><a href="" onclick="disp_alert()">1m</a></dd>
						<dd><a href="" onclick="disp_alert()">1y</a></dd>
					</dl>
				</div>
				<div id="range">
					<dl>
						<dt>Date Range:</dt>
						<dd><a href="" class="btn-slide">YYYY/MM/DD - YYYY/MM/DD</a></dd>
					</dl>
				</div>
				<div id="panel">calendar here</div>
			</div>
			
			<!-- confirm box -->
			<div id="confirm" style="display:none">
				<a href="#" title="Close" class="modalCloseX modalClose">x</a>
				<div class="header"><span>Confirm</span></div>
				<p class="message"></p>
				<div class="buttons">
					<div class="no modalClose">No</div><div class="yes">Yes</div>
				</div>
			</div>
			
			<div>
				<script type="text/javascript" src="http://postgres83dev.office.omniti.com/reconnoiter/js/swfobject.js"></script>
				<script type="text/javascript">
					function mainGraphData(settings) {
						var mg = document.getElementById("maingraph");
						mg.reloadSettings(settings);
					}
				</script>
				<div id="flashcontent">
					<strong>You need to upgrade your Flash Player</strong>
				</div>
				<script type="text/javascript">
					// <![CDATA[		
					var so = new SWFObject("amcharts/amline/amline.swf", "maingraph", "697", "400", "8", "#FFFFFF");
					so.addVariable("path", "amcharts/amline/");
					so.addVariable("settings_file", escape("/reconnoiter/blank.xml"));
					so.addVariable("preloader_color", "#999999");
					so.addVariable("chart_id", "maingraph");
					so.addParam('wmode','transparent');//added for confirm box
					so.write("flashcontent");
					// ]]>
				</script>
				<form action="#" name="form4" id="form4" style="margin:1em 0;text-align:center;">
				<fieldset>
				<legend style="display:none;">View</legend>
				<label for="std_view"><input type="radio" name="std_view" id="std_view" value="Standard View" checked="checked" /> Standard View</label> &nbsp;&nbsp;&nbsp;
				<label for="stacked_view"><input type="radio" name="stacked_vieww" id="stacked_view" value="Stacked View" /> Stacked View</label>
				</fieldset>
				</form>
				
			</div>
			
			<table id="gtool">
				<tr>
					<th></th>
					
					<th class="data">Data Points</th>
					<th>Color</th>
					<th>Derivative</th>
					<th>Axis</th>
					<th></th>
					<th></th>
				</tr>
				<tbody>
				<tr>
					<td><input name="" type="checkbox" value="view" checked /></td>
					<td class="data">sample data point 1</td>
					<td><script type="text/javascript">drawCP('cp1');</script></td>
					<td><input name="" type="checkbox" value="view" checked /></td>
					<td><span class="axisoff"><a href="">L</a></span> <span class="axison"><a href="">R</a></span></td>
					<td><a href="#" id="delete"><span>delete</span></a></td>
					<td class="math">math</td>
				</tr>
				<tr class="mathbox">
					<td colspan="7">
					<div>
						<label for="math">Math 1 (Units conversion)</label> <input type="text" id="keyword2" value="no math" style="width:320px;" /> <input type="submit" value="Apply" /> <input type="submit" value="Edit Math" disabled="disabled" /><br />
					</div>
					<div>	
						<label for="math">Math 2</label> <input type="text" id="keyword2" value="no math" style="width:320px;" /> <input type="submit" value="Apply" /> <input type="submit" value="Edit Math" disabled="disabled" />
					</div>
					</td>
				</tr>
				<tr>
					<td><input name="" type="checkbox" value="view" checked /></td>
					<td class="data">sample data point 2</td>
					<td><script type="text/javascript">drawCP('cp2');</script></td>
					<td><input name="" type="checkbox" value="view" checked /></td>
					<td><span class="axison"><a href="">L</a></span> <span class="axisoff"><a href="">R</a></span></td>
					<td><a href="#" id="delete"><span>delete</span></a></td>
					<td class="math">math</td>
				</tr>
				<tr class="mathbox">
					<td colspan="7">
					<div>
						<label for="math">Math 1 (Units conversion)</label> <input type="text" id="keyword2" value="no math" style="width:320px;" /> <input type="submit" value="Apply" /> <input type="submit" value="Edit Math" disabled="disabled" /><br />
					</div>
					<div>	
						<label for="math">Math 2</label> <input type="text" id="keyword2" value="no math" style="width:320px;" /> <input type="submit" value="Apply" /> <input type="submit" value="Edit Math" disabled="disabled" />
					</div>
					</td>
				</tr>
				<tr>
					<td><input name="" type="checkbox" value="view" checked /></td>
					<td class="data">sample data point 3</td>
					<td><script type="text/javascript">drawCP('cp3');</script></td>
					<td><input name="" type="checkbox" value="view" checked /></td>
					<td><span class="axison"><a href="">L</a></span> <span class="axisoff"><a href="">R</a></span></td>
					<td><a href="#" id="delete"><span>delete</span></a></td>
					<td class="math">math</td>
				</tr>
				<tr class="mathbox">
					<td colspan="7">
					<div>
						<label for="math">Math 1 (Units conversion)</label> <input type="text" id="keyword2" value="no math" style="width:320px;" /> <input type="submit" value="Apply" /> <input type="submit" value="Edit Math" disabled="disabled" /><br />
					</div>
					<div>	
						<label for="math">Math 2</label> <input type="text" id="keyword2" value="no math" style="width:320px;" /> <input type="submit" value="Apply" /> <input type="submit" value="Edit Math" disabled="disabled" />
					</div>
					</td>
				</tr>
				</tbody>
			</table>
			
			
