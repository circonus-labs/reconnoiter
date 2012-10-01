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
		$query = "INSERT INTO login.register VALUES ('$username','$password','$email')";
                $result = pg_query($query);
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
            $query = "SELECT * FROM login.register";
            $result = pg_query($query);
            if (!$result) 
            {
	         echo "Problem with query " . $query . "<br/>";
	         echo pg_last_error();
	         exit();
            }
       
	   
	   while($myrow = pg_fetch_row($result)) 
           {
    	      	$uname=$myrow[0];
         	$p_word=$myrow[1];
         	$e_mail=$myrow[2];
       	    
                $msg="failure";
      	   
      	   	if($email==$e_mail)
        	{
        		if($password==$p_word)
        		{
        			$_SESSION["shet"]=$uname;
            		$msg="success";
        			break;	        		
        		}
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
