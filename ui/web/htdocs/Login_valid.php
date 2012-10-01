<?php

   require_once "db.php";
     
   session_start();
   $email=$_POST['email'];
   $password = $_POST['password'];
   $token = $_POST['token'];
		
				
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
		   $pwd=hash('sha256',$password);
	           $msg=login($email,$pwd);
						
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
