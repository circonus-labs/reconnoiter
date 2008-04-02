BEGIN;

-- Tables 

CREATE TABLE stratcon.loading_dock_check_s (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp with time zone NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL,
    PRIMARY KEY(sid,id,whence)
);

CREATE TABLE stratcon.loading_dock_status_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text,
    PRIMARY KEY(sid,whence)
);

CREATE TABLE stratcon.loading_dock_metric_numeric_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value numeric,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s_change_log (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);


CREATE TABLE stratcon.rollup_matrix_numeric_60m(
   sid integer not null,
   name text not null, 
   rollup_time timestamp not null, 
   count_rows integer,
   avg_value numeric ,
   stddev_value numeric,
   min_value numeric ,
   max_value numeric ,
   PRIMARY KEY(rollup_time,sid,name));
   
CREATE TABLE stratcon.rollup_matrix_numeric_6hours(
   sid integer not null,
   name text not null, 
   rollup_time6 timestamp not null, 
   count_rows integer,
   avg_value numeric ,
   stddev_value numeric,
   min_value numeric ,
   max_value numeric ,
   PRIMARY KEY(rollup_time6,sid,name));   

CREATE TABLE stratcon.rollup_matrix_numeric_12hours(
   sid integer not null,
   name text not null, 
   rollup_time12 timestamp not null, 
   count_rows integer,
   avg_value numeric ,
   stddev_value numeric,
   min_value numeric ,
   max_value numeric ,
   PRIMARY KEY(rollup_time12,sid,name));      
 
CREATE TABLE stratcon.rollup_matrix_numeric_5m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    stddev_value numeric,
    min_value numeric,
    max_value numeric,
    PRIMARY KEY (rollup_time,sid,name)
);

CREATE TABLE stratcon.map_uuid_to_sid (
    id uuid NOT NULL,
    sid integer NOT NULL,
    PRIMARY KEY(id,sid)
);

CREATE TABLE stratcon.log_whence_s (
    whence timestamp with time zone NOT NULL,
    interval varchar2(20,
    PRIMARY KEY(whence,interval)
);

-- Schema Sequence 

CREATE SEQUENCE stratcon.seq_sid
    START WITH 50
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;



-- GRANTS 

 GRANT SELECT,INSERT ON stratcon.loading_dock_status_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_check  TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_status TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_numeric TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_text TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_check_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_numeric_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_text_s_change_log TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.log_whence_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_text_s TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_60m TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_5m TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_6hours TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_12hours TO stratcon;
 GRANT SELECT,INSERT ON stratcon.map_uuid_to_sid TO stratcon;
 ALTER TABLE stratcon.seq_sid OWNER TO stratcon;
 
 
-- Function To generate SID from ID 

CREATE OR REPLACE FUNCTION stratcon.generate_sid_from_id(v_in_id uuid)
RETURNS integer
AS $$
DECLARE
   v_ex_sid integer;
   v_new_sid integer;
 
BEGIN

SELECT sid FROM stratcon.map_uuid_to_sid WHERE id=v_in_id
  INTO v_ex_sid;

 IF NOT FOUND THEN
    SELECT nextval('stratcon.seq_sid') 
    INTO v_new_sid;

    INSERT INTO stratcon.map_uuid_to_sid(id,sid) VALUES (v_in_id,v_new_sid);
   
    RETURN v_new_sid;
 ELSE
      RETURN v_ex_sid;
 END IF;

END
$$ LANGUAGE plpgsql;

-- Trigger Function to change Metrix Text Changes 

CREATE TRIGGER loading_dock_metric_text_s_change_log
    AFTER INSERT ON loading_dock_metric_text_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_text_s_change_log();

CREATE FUNCTION stratcon.loading_dock_metric_text_s_change_log() RETURNS trigger
    AS $$
DECLARE
    v_oldvalue TEXT;
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT value FROM  stratcon.loading_dock_metric_text_s WHERE sid = NEW.sid AND name = NEW.name 
        AND WHENCE = (SELECT max(whence) FROM stratcon.loading_dock_metric_text_s_change_log 
                        WHERE WHENCE <> NEW.WHENCE and sid=NEW.sid and name=NEW.name )
    INTO v_oldvalue;

    IF v_oldvalue IS DISTINCT FROM NEW.value THEN

        INSERT INTO stratcon.loading_dock_metric_text_s_change_log (sid,whence,name,value)
            VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value); 

    END IF;

ELSE
        RAISE EXCEPTION 'Non-INSERT DML operation attempted on INSERT only table';
END IF;

    RETURN NULL;

END
$$
    LANGUAGE plpgsql;
    
-- Trigger on Metrix Numeric to log last inserted timestamp 

CREATE OR REPLACE FUNCTION stratcon.loading_dock_metric_numeric_s_whence_log() 
RETURNS trigger
AS $$
DECLARE
v_whence timestamptz;
BEGIN
IF TG_OP = 'INSERT' THEN
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',NEW.WHENCE) + (round(extract('minute' from NEW.WHENCE)/5)*5) * '1 minute'::interval and interval='5 minutes'
     INTO v_whence;
   IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',NEW.WHENCE) + (round(extract('minute' from NEW.WHENCE)/5)*5) * '1 minute'::interval,'5 minutes');
    END IF;
END IF;

    RETURN NULL;
END
$$
    LANGUAGE plpgsql;

-- 5 minutes rollup

CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_5m()
RETURNS void
AS $$
DECLARE
 
 rec stratcon.rollup_matrix_numeric_5m%rowtype;
 v_sql TEXT;
 v_min_whence TIMESTAMPTZ;
 v_max_rollup_5 TIMESTAMPTZ;
 v_whence TIMESTAMPTZ;
 rows INT;
 
BEGIN

 SELECT MIN(whence) FROM stratcon.log_whence_s WHERE interval='5 minutes'
        INTO v_min_whence;
        
 SELECT MAX(rollup_time) FROM  stratcon.rollup_matrix_numeric_5m 
         INTO v_max_rollup_5;        
 
 -- Insert Log for Hourly rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='1 hour'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'1 hour');
   END IF;
   
 IF v_min_whence <= v_max_rollup_5 THEN

   DELETE FROM stratcon.rollup_matrix_numeric_5m 
                WHERE rollup_time = v_min_whence;
 
 END IF;

 FOR rec IN 
                SELECT sid , name,v_min_whence as rollup_time,
                      COUNT(1) as count_rows ,AVG(value) as avg_value,STDDEV(value) as stddev_value ,MIN(value) as min_value ,MAX(value) as max_value
                      FROM stratcon.loading_dock_metric_numeric_s
                      WHERE WHENCE <= v_min_whence AND WHENCE > v_min_whence -'5 minutes'::interval
                GROUP BY rollup_time,sid,name
 
       LOOP
    
        
        INSERT INTO stratcon.rollup_matrix_numeric_5m
         (sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES 
         (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
        
   END LOOP;

  -- Delete from whence log table
  
  DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='5 minutes';
 
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;

-- 1 hourl rollup


CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_60m()
RETURNS void
AS $$
DECLARE
  rec stratcon.rollup_matrix_numeric_60m%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_5 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
BEGIN

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='1 hour'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_60m 
         INTO v_max_rollup_5;    

-- Insert Log for 6 Hour rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='6 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'6 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_5 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_60m 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,date_trunc('hour',rollup_time) as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		         SQRT((SUM((count_rows-1)*(POWER(stddev_value,2)+POWER(avg_value,2)))/(SUM(count_rows)-1)))-(power(SUM(avg_value*count_rows)/SUM(count_rows),2)) as stddev_value,
		         MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_5m
		           WHERE date_trunc('hour',rollup_time)= date_trunc('hour',v_min_whence)
                   GROUP BY date_trunc('hour',rollup_time),sid,name
        LOOP
      
          INSERT INTO stratcon.rollup_matrix_numeric_60m
          (sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='1 hour';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


-- 6 hours

CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_6hours()
RETURNS void
AS $$
DECLARE
  rec stratcon.rollup_matrix_numeric_6hours%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_6 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
 
BEGIN

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='6 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time6)) FROM  stratcon.rollup_matrix_numeric_6hours 
         INTO v_max_rollup_6;    

-- Insert Log for 12 Hours rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='12 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'12 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_6 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_6hours 
       WHERE rollup_time6= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time6,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		         SQRT((SUM((count_rows-1)*(POWER(stddev_value,2)+POWER(avg_value,2)))/(SUM(count_rows)-1)))-(power(SUM(avg_value*count_rows)/SUM(count_rows),2)) as stddev_value,
		         MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_60m
		           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'6 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_6hours
          (sid,name,rollup_time6,count_rows,avg_value,stddev_value,min_value,max_value) VALUES
          (rec.sid,rec.name,rec.rollup_time6,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='6 hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


-- 12 hours

CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_12hours()
RETURNS void
AS $$
DECLARE
  rec stratcon.rollup_matrix_numeric_12hours%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_12 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
 
BEGIN

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='12 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time12)) FROM  stratcon.rollup_matrix_numeric_12hours 
         INTO v_max_rollup_12;    

/*-- Insert Log for 24 Hours rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='24 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'24 hours');
   END IF;
   */
   
  IF v_min_whence <= v_max_rollup_12 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_12hours 
       WHERE rollup_time12= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time12,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		         SQRT((SUM((count_rows-1)*(POWER(stddev_value,2)+POWER(avg_value,2)))/(SUM(count_rows)-1)))-(power(SUM(avg_value*count_rows)/SUM(count_rows),2)) as stddev_value,
		         MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_6hours
		           WHERE rollup_time6<= v_min_whence and rollup_time6> v_min_whence-'12 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_12hours
          (sid,name,rollup_time12,count_rows,avg_value,stddev_value,min_value,max_value) VALUES
          (rec.sid,rec.name,rec.rollup_time12,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='12 hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


COMMIT;
