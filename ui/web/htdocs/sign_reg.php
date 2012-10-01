<?php
    require_once 'db.php';
    session_start();

    $username = $_POST['username'];
    $password = $_POST['password'];
    $email=$_POST['email'];
		
	
    if(empty($_POST['username']))
       {
	 echo "user_empty";
       }
		
    else if(empty($_POST['password']))
       {
	 echo "pass_empty";
       }
    else if(empty($_POST['email']))
       {
	 echo "mail_empty";
       }
		
    else if(!preg_match("/^[_a-z0-9-]+(.[_a-z0-9-]+)*@[a-z0-9-]+(.[a-z0-9-]+)*(.[a-z]{2,3})$/", $email))
       {
	 echo "incorrect";
       }
	    
    else
       {
    	 if ($_POST['token_login'] == $_SESSION['token_login'])
	   {
	   	connect();
		$pwd=hash('sha256', $password);
		$msg=sign_in($username,$pwd,$email);
		echo $msg;
   	   }
       }	
?>
