<?php
     require_once 'db.php';
     session_start();
	
		$username = pg_escape_string($_POST['username']);
		$password = pg_escape_string($_POST['password']);
		$email= pg_escape_string($_POST['email']);
		
		
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
	     	
		   {
	    	     connect();
		     $msg=sign_in($username,$password,$email);
		     echo $msg;
		   }
			
			
		
}	
?>
