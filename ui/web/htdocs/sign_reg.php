<?php
	
		$username = $_REQUEST['username'];
		$password = $_REQUEST['password'];
		$email=$_REQUEST['email'];
		
		if(empty($_REQUEST['username']))
		{
			echo "user_empty";
		}
		
		else if(empty($_REQUEST['password']))
		{
			echo "pass_empty";
		}
		else if(empty($_REQUEST['email']))
		{
			echo "mail_empty";
		}
		else if(!preg_match("/^[_a-z0-9-]+(.[_a-z0-9-]+)*@[a-z0-9-]+(.[a-z0-9-]+)*(.[a-z]{2,3})$/", $email))
		
		{
			echo "incorrect";
		}
	        else
	        {
	     	
	     	        $db = pg_connect('host=localhost dbname=reconnoiter user=stratcon password=stratcon');
                        $query="select * from stratcon.login where e_mail= '".$email."'";
			$result = pg_query($query);
			if(pg_num_rows($result)>0)
			{
				echo "no_mail";
			
			}
			else
			{
                          try
		          {
			   $query = "INSERT INTO stratcon.login VALUES ('$username','$password','$email')";
                           $result = pg_query($query);
  			   echo "success";
		          }
		
                          catch(PDOException $e)
		          {
			    echo $e->getMessage();
		          }
		        }
                }
	
?>
