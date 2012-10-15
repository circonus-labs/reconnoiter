<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">

<?php
    session_start();
    require_once('db.php');
    $token_login = Signup_token();   
?>



<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>Reconnoiter</title>

<link href="css/style.css" rel="stylesheet" type="text/css" />
<link rel="icon" type="image/vnd.microsoft.icon" href="images/favicon.ico" />
<link href="js/jquery-ui-1.7.2/themes/base/ui.all.css" rel="stylesheet" type="text/css" />
<link rel="stylesheet" type="text/css" href="css/Login.css" />
<script type="text/javascript" src="js/jquery-1.3.2.min.js"></script>

<script type="text/javascript">
$(document).ready(function()
{
	
	$("#login").click(function() {
	
		var action = $("#form1").attr('action');
		var form_data = {
			username: $("#username").val(),
			password: $("#password").val(),
			email: $("#email").val(),
                        token_login: $("#token_login").val(),
                        
			is_ajax: 1
		};
		
		$.ajax({
			type: "POST",
			url: action,
			data: form_data,
			success: function(response)
			{

				if(response == 'success')
				 
					{
						alert("User Registered Successfully");
					        window.location="http://localhost/Login.php";
					}
					
			        else if(response == 'incorrect')
			                {
		                                alert("Invalid Email id");
				        }

				else if(response == 'user_empty')
				        {
					        alert("Enter Username Field value");
					}
						
				else if(response == 'pass_empty')
				        {
						alert("Enter Password Field values");
				        }

				else if(response == 'mail_empty')
				        {
   				                 alert("Enter Mail Id");
				        }	

				else if(response == 'no_mail')
				        {
					         alert("Mail Id already registered");
				        }	
			}
		});
		
		return false;
	});
	
});
</script>


body>
<div id="header">

	<h1><a href="#">Reconnoiter</a></h1>
	<ul>
		<li><a href="https://labs.omniti.com/docs/reconnoiter/">Documentation</a></li>
		<li><a href="https://labs.omniti.com/trac/reconnoiter/">Support</a></li>
		<!--
                <li><a href="#">Username</a></li>
		<li class="xx"><a href="#">Logout</a></li>
                -->

                <!--  <li><a href="https://www.google.co.in/">UserName</a></li>-->
                <li><a href="http://localhost/Login.php">Login</a></li>
     
	</ul>
</div><!-- end header -->

 <div id="content2">

  <h1>Registartion Form</h1>
  <p>&nbsp;</p> 
  <form id="form1" name="form1" action="sign_reg.php" method="post">
    
    <p>
      <label for="username">Username: </label>
      <input type="text" name="username" id="username" />
    </p>
    
    <p>
      <label for="password">Password: </label>
      <input type="password" name="password" id="password" />
    </p>
    
    <p>
      <label for="mail">E-mail ID: </label>
      <input type="text" name="email" id="email" />
    </p>
    <p>
      <input type="submit" id="login" name="login" value="Login" />
    </p>
  </form>
 </div>	
</body>
</html>
