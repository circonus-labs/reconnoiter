		<h3 id="graph_controls">Graph Controls</h3>
		<span class="search">
			<div class="tabs">
				<ul class="tabNavigation">
					<li class="first"><a href="#first">Graphs</a></li>
					<li class="second"><a href="#second">Templates</a></li>
					<li class="third"><a href="#third">Find Data</a></li>
					<li class="fourth"><a href="#fourth">Browse Data</a></li>
				</ul><br clear="all" />
		
				<div id="first"><!-- search -->
				<?php include('search_saved_graphs.inc') ?>
				</div>
				<div id="second"><!-- browse -->
				<?php include('search_templates.inc') ?>
				</div>
				<div id="third"><!-- saved graphs -->
				<?php include('search_simple.inc') ?>
				</div>
				<div id="fourth"><!-- browse -->
				<?php include('search_browser.inc') ?>
				</div>
			</div>
		</span>
