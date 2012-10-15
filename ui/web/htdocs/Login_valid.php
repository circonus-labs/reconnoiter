<?php
	   
	   require_once "db.php";     
           session_start();
	      
	   $email= pg_escape_string($_POST['email']);
	   $password = pg_escape_string($_POST['password']);
	   $token = pg_escape_string($_POST['token']);
		
				
	   if(empty($_POST['email']))
	   {
		echo "mail_empty";
	   }
		
	   else if(empty($_POST['password']))
	   {
		echo "pass_empty";
	   }
		    
	   else
	   {
	      if ($_POST['token'] == $_SESSION['token'])
		 {
		   connect();
		   $msg=login($email,$password);
						
                   if($msg=="success")
                   {
        	      echo "success";
                   }
                   else 
                   {
        	      echo "failure";
                   }
		 }
	      else
		 {
		     echo "failure";
		 }		 	   
	    }
	 
?>
