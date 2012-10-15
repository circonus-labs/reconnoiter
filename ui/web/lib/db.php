<?php

function Login_token()
{
    session_start();
    $token = md5(uniqid(rand(), TRUE));
    $_SESSION['token']=$token;
    return $token;
}


function Signup_token()
{
    session_start();
    $token_login = md5(uniqid(rand(), TRUE));
    $_SESSION['token_login']=$token_login;
    return $token_login;
}



function connect()
{
	    	
		   $db = pg_connect('host=localhost dbname=reconnoiter user=login password=authenticate');
}


function sign_in($username,$password,$email)
{
	     
	   
	    $query="select * from login.register where e_mail= '".$email."'";
 	    $result = pg_query($query);
				
		 if(pg_num_rows($result)>0)
			{
				echo "no_mail";
			}
		else
			{
		
	  	     try
		      {
					
				$salt = substr(md5(uniqid(rand(), true)), 0, SALT_LENGTH);
                $pwd=hash('sha256', $salt.$password);
			    $sql='INSERT INTO login.register VALUES ($1,$2,$3,$4)';
                $result = pg_query_params($sql, array($username,$password,$email,$salt));		
			    echo "success";
		      }
		catch(PDOException $e)
		{
			echo $e->getMessage();
		}
		
	  }
	
}

function login($email,$password)
{

  try
    {
            
            $query = "SELECT * FROM login.register where e_mail='$email'";
            $result = pg_query($query);
            
            if (!$result) 
            {
	         echo "Problem with query " . $query . "<br/>";
	         echo pg_last_error();
	         exit();
            }
       
	   
	        while($myrow = pg_fetch_row($result)) 
            {
            $msg="failure";
      			
											
			$pword=$myrow[1];
			$salt1= $myrow[3];
		    $check=hash('sha256', $salt1.$password);
			      	   	
      	   	if($check==$pword)
        	{
        		$msg="success";
				break;
        	}
			
        }
		
		return $msg;

    }

   catch(PDOException $e)
		{
			echo $e->getMessage();
		}
}

?>
