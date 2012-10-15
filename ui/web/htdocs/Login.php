<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">

<?php
     session_start();
     require_once('db.php');
     $token = Login_token();
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
$(document).ready(function() {
	
	$("#login").click(function() {
	
		var action = $("#form1").attr('action');
		var form_data = {
			email: $("#email").val(),
			password: $("#password").val(),
                        token: $("#token").val(),
			
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
					//$("#message").html("<p class='success'>You have Logged in successfully!</p>");

                                       alert("You have Logged in successfully!");
                                       window.location="http://localhost:80";
					}
				
				else if(response == 'failure')
				{
					alert("Invalid Email Id or Password");
					$("#message").html("<p class='error'>Invalid Email Id or Password</p>");
					
					
				}
				
				else if(response == 'mail_empty')
				{
					alert("Enter value for Email id field");
					$("#message").html("<p class='error'>Enter value for Email id field</p>");
				}

				else if(response == 'pass_empty')
				{
					alert("Enter value for Password field");
					$("#message").html("<p class='error'>Enter value for Password field</p>");
				}
				
			}
		});
		
		return false;
	});
	
});
</script>
</head>

<body>
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
                <li><a href="http://localhost/Sign_up.php">Sign Up</a></li>
        
       </ul>
</div><!-- end header -->

 <div id="content1">
  <h1>Login Form</h1>
  <p>&nbsp;</p>
  <form id="form1" name="form1" action="Login_valid.php" method="post">
    
    <p>
      <label for="mail">E-mail ID: </label>
      <input type="text" name="email" id="email" />
    </p>
    
    <p>
      <label for="password">Password: </label>
      <input type="password" name="password" id="password" />
    </p>
    <input type="hidden" name="token" id="token" value="<?php echo $token;?>" />
      
    <p>
      <input type="submit" id="login" name="login" value="Login" />
    </p>
  </form>
</div>
</body>
</html>
