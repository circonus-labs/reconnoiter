<?php
	
	
                session_start();

		$email=$_REQUEST['email'];
		$password = $_REQUEST['password'];
		
		
		if(empty($_REQUEST['email']))
		{
			echo "mail_empty";
		}
		
		else if(empty($_REQUEST['password']))
		{
			echo "pass_empty";
		}
		
		
	    
	    else
	     {
		try
		{
			$db = pg_connect('host=localhost dbname=reconnoiter user=stratcon password=stratcon');
                        $query = "SELECT * FROM stratcon.login";
                        $result = pg_query($query);

                        if (!$result) 
                        {
                  	   alert("Problem with query " . $query); 
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
                           
        			$_SESSION["Username"]=$uname;
            		        $msg="success";
        			break;
        		   }
        	         }
        	
                       }
				
                        if($msg=="success")
                         {
        	          echo "success";
                         }
                         else 
                         {
        	          echo "failure";
                         }
	   
	        }
		catch(PDOException $e)
		{
			echo $e->getMessage();
		}
		
		
	     }
	 
			
?>
