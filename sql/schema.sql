BEGIN;

-- Tables 

CREATE TABLE stratcon.loading_dock_check_s (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamptz NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL,
    PRIMARY KEY(sid,id,whence)
);

CREATE TABLE stratcon.loading_dock_status_s (
    sid integer NOT NULL,
    whence timestamptz NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text,
    PRIMARY KEY(sid,whence)
);

CREATE TABLE stratcon.loading_dock_status_s_change_log (
    sid integer NOT NULL,
    whence timestamptz NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text,
    PRIMARY KEY(sid,whence)
);


CREATE TABLE stratcon.loading_dock_metric_text_s (
    sid integer NOT NULL,
    whence timestamptz NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s_change_log (
    sid integer NOT NULL,
    whence timestamptz NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_numeric_s (
    sid integer NOT NULL,
    whence timestamptz NOT NULL,
    name text NOT NULL,
    value numeric,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.rollup_matrix_numeric_5m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamptz NOT NULL,
    count_rows integer,
    avg_value numeric
    PRIMARY KEY (rollup_time,sid,name)
);

CREATE TABLE stratcon.rollup_matrix_numeric_20m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamptz NOT NULL,
    count_rows integer,
    avg_value numeric
    PRIMARY KEY (rollup_time,sid,name)
);

CREATE OR REPLACE FUNCTION stratcon.date_hour(timestamptz) 
 RETURNS timestamptz as $BODY$
 SELECT date_trunc('hour',$1);
 $BODY$ 
 language 'sql' 
 IMMUTABLE STRICT;

CREATE INDEX idx_rollup_matrix_numeric_20m_rollup_time 
              ON stratcon.rollup_matrix_numeric_20m(date_hour(rollup_time));

CREATE TABLE stratcon.rollup_matrix_numeric_60m(
   sid integer not null,
   name text not null, 
   rollup_time timestamptz not null, 
   count_rows integer,
   avg_value numeric 
   PRIMARY KEY(rollup_time,sid,name));
   
CREATE TABLE stratcon.rollup_matrix_numeric_6hours(
   sid integer not null,
   name text not null, 
   rollup_time timestamptz not null, 
   count_rows integer,
   avg_value numeric 
   PRIMARY KEY(rollup_time6,sid,name));   

CREATE TABLE stratcon.rollup_matrix_numeric_12hours(
   sid integer not null,
   name text not null, 
   rollup_time timestamptz not null, 
   count_rows integer,
   avg_value numeric ,
   PRIMARY KEY(rollup_time12,sid,name));      
 
CREATE TABLE stratcon.map_uuid_to_sid (
    id uuid NOT NULL,
    sid integer NOT NULL,
    PRIMARY KEY(id,sid)
);

CREATE TABLE stratcon.log_whence_s (
    whence timestamptz NOT NULL,
    interval varchar2(20,
    PRIMARY KEY(whence,interval)
);

CREATE TABLE stratcon.rollup_runner (
  rollup_table character varying(100),
  runner character varying(22)
);

CREATE TABLE stratcon.metric_name_summary (
  sid integer NOT NULL,
  metric_name text NOT NULL,
  metric_type character varying(22),
  active boolean default 'true',
  PRIMARY KEY (sid,metric_name,metric_type)
);

CREATE TABLE stratcon.current_node_config (
  remote_address inet not null, 
  node_type text not null, 
  whence timestamptz not null, 
  config xml not null,
  PRIMARY KEY (remote_address,node_type)
);

CREATE TABLE stratcon.current_node_config_changelog (
  remote_address inet not null, 
  node_type text not null, 
  whence timestamptz not null, 
  config xml not null,
  PRIMARY KEY (remote_address,node_type)
);

-- Schema Sequence 

CREATE SEQUENCE stratcon.seq_sid
    START WITH 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;



-- GRANTS 

 GRANT SELECT,INSERT ON stratcon.loading_dock_status_s TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.loading_dock_status_s_change_log TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_check_s TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.loading_dock_metric_numeric_s TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.loading_dock_metric_text_s_change_log TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.log_whence_s TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.loading_dock_metric_text_s TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_60m TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_5m TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_20m TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_6hours TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_12hours TO stratcon;
 GRANT SELECT,INSERT ON stratcon.map_uuid_to_sid TO stratcon;
 GRANT SELECT,INSERT,UPDATE,DELETE ON stratcon.rollup_runner TO stratcon;
 GRANT SELECT,INSERT,UPDATE,DELETE ON stratcon.metric_name_summary TO stratcon;
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

-- Trigger Function to log dock status Changes 

CREATE  TRIGGER loading_dock_status_s_change_log
    AFTER INSERT ON stratcon.loading_dock_status_s
    FOR EACH ROW
    EXECUTE PROCEDURE stratcon.loading_dock_status_s_change_log();


CREATE OR REPLACE FUNCTION stratcon.loading_dock_status_s_change_log() RETURNS trigger
    AS $$
DECLARE
    v_state CHAR(1);
    v_avail CHAR(1);
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT state,availability FROM  stratcon.loading_dock_status_s WHERE sid = NEW.sid 
        AND WHENCE = (SELECT max(whence) FROM stratcon.loading_dock_status_s_change_log 
                        WHERE  SID=NEW.sid and  WHENCE <> NEW.whence )
    INTO v_state,v_avail;

    IF v_state IS DISTINCT FROM NEW.state OR v_avail IS DISTINCT FROM NEW.availability THEN

        INSERT INTO stratcon.loading_dock_status_s_change_log (sid,whence,state,availability,duration,status)
            VALUES (NEW.sid,NEW.whence,NEW.state,NEW.availability,NEW.duration,NEW.status); 

    END IF;

ELSE
        RAISE EXCEPTION 'Something wrong with stratcon.loading_dock_status_s_change_log';
END IF;

    RETURN NULL;

END
$$
    LANGUAGE plpgsql;


-- Trigger Function to log node config change

CREATE   TRIGGER current_node_config_changelog
    AFTER INSERT ON stratcon.current_node_config
    FOR EACH ROW
    EXECUTE PROCEDURE stratcon.current_node_config_changelog();


CREATE OR REPLACE FUNCTION stratcon.current_node_config_changelog() RETURNS trigger
    AS $$
DECLARE
    v_config xml;
    
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT config FROM  stratcon.current_node_config WHERE remote_address = NEW.remote_address AND node_type= NEW.node_type
        AND whence = (SELECT max(whence) FROM stratcon.current_node_config_changelog
                        WHERE  remote_address = NEW.remote_address AND node_type= NEW.node_type and  whence <> NEW.whence )
    INTO v_config;

    IF v_config IS DISTINCT FROM NEW.config THEN

        INSERT INTO stratcon.current_node_config_changelog (remote_address,node_type,whence,config)
            VALUES (NEW.remote_address,NEW.node_type,NEW.whence,NEW.config); 

    END IF;

ELSE
        RAISE EXCEPTION 'Something wrong with stratcon.current_node_config_changelog';
END IF;

    RETURN NULL;

END
$$
    LANGUAGE plpgsql;  


-- Trigger Function to log Metrix Text Changes 

CREATE TRIGGER loading_dock_metric_text_s_change_log
    AFTER INSERT ON stratcon.loading_dock_metric_text_s
    FOR EACH ROW
    EXECUTE PROCEDURE stratcon.loading_dock_metric_text_s_change_log();


CREATE OR REPLACE FUNCTION stratcon.loading_dock_metric_text_s_change_log() RETURNS trigger
    AS $$
DECLARE
    v_oldvalue text;
    v_sid integer;
    v_name text;
    v_value text;
    v_whence timestamptz;
    v_old_whence timestamptz;
    v_old_name text;
    v_old_sid integer;
    v_old_value text;
    v_max_whence timestamptz;
BEGIN

IF TG_OP = 'INSERT' THEN

SELECT max(whence) FROM stratcon.loading_dock_metric_text_s WHERE whence <> NEW.whence and sid=NEW.sid and name = NEW.name 
        INTO v_max_whence;
 
 IF NEW.whence < v_max_whence THEN            
 
    INSERT INTO stratcon.loading_dock_metric_text_s_change_log (sid,whence,name,value)
                 VALUES (NEW.sid,NEW.whence, NEW.name, NEW.value); 
                 
       SELECT  whence,name,value FROM  stratcon.loading_dock_metric_text_s_change_log WHERE whence > NEW.whence and sid=NEW.sid and name=NEW.name order by whence  limit 1 
        INTO v_whence,v_sid,v_name,v_value;
          IF FOUND  THEN
            IF v_value IS  DISTINCT FROM NEW.value THEN
               NULL;
            ELSE
                DELETE from  stratcon.loading_dock_metric_text_s_change_log  WHERE whence=v_whence and sid=v_sid and name=v_name;
            END IF;
          END IF;
       
         
       SELECT whence,sid,name,value from stratcon.loading_dock_metric_text_s where whence> NEW.whence and sid=NEW.sid and name=NEW.name and value!=NEW.value order by whence limit 1
         INTO v_whence,v_sid,v_name,v_value;
          IF FOUND  THEN
             SELECT  whence,sid,name,value FROM  stratcon.loading_dock_metric_text_s_change_log WHERE whence =v_whence and sid=v_sid and name=v_name and value=v_value
                 INTO v_old_whence,v_old_sid,v_old_name,v_old_value;
              IF FOUND THEN
                 NULL;
              ELSE
                INSERT INTO stratcon.loading_dock_metric_text_s_change_log (sid,whence,name,value)
                 VALUES (v_sid,v_whence, v_name, v_value); 
               END IF;
        END IF;

  ELSE
	     SELECT value FROM  stratcon.loading_dock_metric_text_s WHERE sid = NEW.sid AND name = NEW.name 
	         AND WHENCE = (SELECT max(whence) FROM stratcon.loading_dock_metric_text_s_change_log 
	                         WHERE WHENCE <> NEW.WHENCE and sid=NEW.sid and name=NEW.name )
		     INTO v_oldvalue;

		    IF v_oldvalue IS DISTINCT FROM NEW.value THEN
		
		        INSERT INTO stratcon.loading_dock_metric_text_s_change_log (sid,whence,name,value)
		            VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value); 
		    END IF;
  END IF;	    


SELECT sid,metric_name FROM stratcon.metric_name_summary WHERE sid=NEW.sid  and metric_name=NEW.name
        INTO v_sid,v_name;
     IF NOT FOUND THEN
          INSERT INTO  stratcon.metric_name_summary(sid,metric_name,metric_type)  VALUES(NEW.sid,NEW.name,'text');
     END IF;

ELSE
        RAISE EXCEPTION 'something wrong with stratcon.loading_dock_metric_text_s_change_log ';
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
v_whence_5 timestamptz;
v_sid integer;
v_name text;
BEGIN
IF TG_OP = 'INSERT' THEN
 
 v_whence_5:=date_trunc('H',NEW.WHENCE) + (round(extract('minute' from NEW.WHENCE)/5)*5) * '1 minute'::interval;
 
   SELECT whence FROM stratcon.log_whence_s WHERE whence=v_whence_5 and interval='5 minutes'
     INTO v_whence;
     
   IF NOT FOUND THEN
      BEGIN
       INSERT INTO  stratcon.log_whence_s VALUES(v_whence_5,'5 minutes');
       EXCEPTION
        WHEN UNIQUE_VIOLATION THEN
        -- do nothing 
      END;
    END IF;

   SELECT sid,metric_name FROM stratcon.metric_name_summary WHERE sid=NEW.sid  and metric_name=NEW.name
     INTO v_sid,v_name;
   IF NOT FOUND THEN
       INSERT INTO  stratcon.metric_name_summary VALUES(NEW.sid,NEW.name,'numeric');
    END IF;

END IF;
    RETURN NULL;
END
$$    LANGUAGE plpgsql;

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
 v_nrunning INT;
 v_self VARCHAR(22);
 whenceint RECORD;
BEGIN

  SELECT COUNT(1) INTO v_nrunning
    from stratcon.rollup_runner t, pg_stat_activity a
   where rollup_table ='rollup_matrix_numeric_5m'
     and runner = procpid || '.' || date_part('epoch',backend_start);

  IF v_nrunning > 0 THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_5m already running';
    RETURN ;
  END IF;

  SELECT INTO v_self procpid || '.' || date_part('epoch',backend_start)
    FROM pg_stat_activity
   WHERE procpid = pg_backend_pid();

  IF v_self IS NULL THEN
    RAISE EXCEPTION 'stratcon.rollup_matrix_numeric_5m cannot self-identify';
  END IF;

  v_sql = 'update stratcon.rollup_runner set runner = ''' || v_self || ''' where rollup_table = ''rollup_matrix_numeric_5m''';

  EXECUTE v_sql;

FOR whenceint IN SELECT * FROM stratcon.log_whence_s WHERE interval='5 minutes' LOOP
        

 SELECT MIN(whence) FROM stratcon.log_whence_s WHERE interval='5 minutes'
        INTO v_min_whence;
        
 SELECT MAX(rollup_time) FROM  stratcon.rollup_matrix_numeric_5m 
         INTO v_max_rollup_5;        
 
 -- Insert Log for 20 minutes rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) + (round(extract('minute' from v_min_whence)/20)*20) * '1 minute'::interval and interval='20 minutes'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence) + (round(extract('minute' from v_min_whence)/20)*20) * '1 minute'::interval,'20 minutes');
   END IF;
   
 IF v_min_whence <= v_max_rollup_5 THEN

   DELETE FROM stratcon.rollup_matrix_numeric_5m 
                WHERE rollup_time = v_min_whence;
 
 END IF;

 FOR rec IN 
                SELECT sid , name,v_min_whence as rollup_time,
                      COUNT(1) as count_rows ,AVG(value) as avg_value
                      FROM stratcon.loading_dock_metric_numeric_s
                      WHERE WHENCE <= v_min_whence AND WHENCE > v_min_whence -'5 minutes'::interval
                GROUP BY rollup_time,sid,name
 
       LOOP
    
        
        INSERT INTO stratcon.rollup_matrix_numeric_5m
         (sid,name,rollup_time,count_rows,avg_value) VALUES 
         (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value);
        
   END LOOP;

  -- Delete from whence log table
  
  DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='5 minutes';
 
 v_min_whence:= NULL;
 v_max_rollup_5:= NULL;
 
 END LOOP;
 
  UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_5m';
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_5m';
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
         RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


-- 20 minutes rollup

CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_20m()
RETURNS void
AS $$
DECLARE
 
 rec stratcon.rollup_matrix_numeric_20m%rowtype;
 v_sql TEXT;
 v_min_whence TIMESTAMPTZ;
 v_max_rollup_20 TIMESTAMPTZ;
 v_whence TIMESTAMPTZ;
 rows INT;
 v_nrunning INT;
 v_self VARCHAR(22);
 whenceint RECORD;
BEGIN

  SELECT COUNT(1) INTO v_nrunning
    from stratcon.rollup_runner t, pg_stat_activity a
   where rollup_table ='rollup_matrix_numeric_20m'
     and runner = procpid || '.' || date_part('epoch',backend_start);

  IF v_nrunning > 0 THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_20m already running';
    RETURN ;
  END IF;

  SELECT INTO v_self procpid || '.' || date_part('epoch',backend_start)
    FROM pg_stat_activity
   WHERE procpid = pg_backend_pid();

  IF v_self IS NULL THEN
    RAISE EXCEPTION 'stratcon.rollup_matrix_numeric_20m cannot self-identify';
  END IF;

  v_sql = 'update stratcon.rollup_runner set runner = ''' || v_self || ''' where rollup_table = ''rollup_matrix_numeric_20m''';

  EXECUTE v_sql;

FOR whenceint IN SELECT * FROM stratcon.log_whence_s WHERE interval='20 minutes' LOOP

 SELECT MIN(whence) FROM stratcon.log_whence_s WHERE interval='20 minutes'
        INTO v_min_whence;
        
 SELECT MAX(rollup_time) FROM  stratcon.rollup_matrix_numeric_20m 
         INTO v_max_rollup_20;        
 
 -- Insert Log for Hourly rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='1 hour'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'1 hour');
   END IF;
   
 IF v_min_whence <= v_max_rollup_20 THEN

   DELETE FROM stratcon.rollup_matrix_numeric_20m 
                WHERE rollup_time = v_min_whence;
 
 END IF;

 FOR rec IN 
                SELECT sid , name,v_min_whence as rollup_time,
                       SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value
		       FROM stratcon.rollup_matrix_numeric_5m
                      WHERE rollup_time<= v_min_whence AND rollup_time > v_min_whence -'20 minutes'::interval
                GROUP BY sid,name
 
       LOOP
    
        
        INSERT INTO stratcon.rollup_matrix_numeric_20m
         (sid,name,rollup_time,count_rows,avg_value) VALUES 
         (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value);
        
   END LOOP;

  -- Delete from whence log table
  
  DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='20 minutes';
 
  v_min_whence:= NULL;
  v_max_rollup_20:= NULL;

 END LOOP;
 
  UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


-- 1 hour rollup


CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_60m()
RETURNS void
AS $$
DECLARE
  rec stratcon.rollup_matrix_numeric_60m%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_60 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
  v_nrunning INT;
  v_self VARCHAR(22);
  whenceint RECORD;
BEGIN

  SELECT COUNT(1) INTO v_nrunning
    from stratcon.rollup_runner t, pg_stat_activity a
   where rollup_table ='rollup_matrix_numeric_60m'
     and runner = procpid || '.' || date_part('epoch',backend_start);

  IF v_nrunning > 0 THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_60m already running';
    RETURN ;
  END IF;

  SELECT INTO v_self procpid || '.' || date_part('epoch',backend_start)
    FROM pg_stat_activity
   WHERE procpid = pg_backend_pid();

  IF v_self IS NULL THEN
    RAISE EXCEPTION 'stratcon.rollup_matrix_numeric_60m cannot self-identify';
  END IF;

  v_sql = 'update stratcon.rollup_runner set runner = ''' || v_self || ''' where rollup_table = ''rollup_matrix_numeric_60m''';

  EXECUTE v_sql;

FOR whenceint IN SELECT * FROM stratcon.log_whence_s WHERE interval='1 hour' LOOP
           
  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='1 hour'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_60m 
         INTO v_max_rollup_60;    

-- Insert Log for 6 Hour rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/6)*6) * '1 hour'::interval and interval='6 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/6)*6) * '1 hour'::interval,'6 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_60 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_60m 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,date_hour(rollup_time) as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value
		         FROM stratcon.rollup_matrix_numeric_20m
		           WHERE date_hour(rollup_time)= v_min_whence
                   GROUP BY date_hour(rollup_time),sid,name
        LOOP
      
          INSERT INTO stratcon.rollup_matrix_numeric_60m
          (sid,name,rollup_time,count_rows,avg_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='1 hour';

v_min_whence := NULL;
v_max_rollup_60 := NULL;

END LOOP;

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_60m';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_60m';
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
  v_nrunning INT;
  v_self VARCHAR(22);
  whenceint RECORD;  
BEGIN

  SELECT COUNT(1) INTO v_nrunning
    from stratcon.rollup_runner t, pg_stat_activity a
    where rollup_table ='rollup_matrix_numeric_6hours'
     and runner = procpid || '.' || date_part('epoch',backend_start);

  IF v_nrunning > 0 THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_6hours already running';
    RETURN ;
  END IF;

  SELECT INTO v_self procpid || '.' || date_part('epoch',backend_start)
    FROM pg_stat_activity
     WHERE procpid = pg_backend_pid();

  IF v_self IS NULL THEN
    RAISE EXCEPTION 'stratcon.rollup_matrix_numeric_6hours cannot self-identify';
   END IF;

   v_sql = 'update stratcon.rollup_runner set runner = ''' || v_self || ''' where rollup_table = ''rollup_matrix_numeric_6hours''';

  EXECUTE v_sql;

FOR whenceint IN SELECT * FROM stratcon.log_whence_s WHERE interval='6 hours' LOOP

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='6 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_6hours 
         INTO v_max_rollup_6;    

-- Insert Log for 12 Hours rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/12)*12) * '1 hour'::interval and interval='12 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/12)*12) * '1 hour'::interval,'12 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_6 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_6hours 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value
		         FROM stratcon.rollup_matrix_numeric_60m
		           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'6 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_6hours
          (sid,name,rollup_time,count_rows,avg_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='6 hours';
v_min_whence := NULL;
v_max_rollup_6 := NULL;

END LOOP;

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_6hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_6hours'; 
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
  v_nrunning INT;
  v_self VARCHAR(22);
  whenceint RECORD; 
 
BEGIN

  SELECT COUNT(1) INTO v_nrunning
    from stratcon.rollup_runner t, pg_stat_activity a
    where rollup_table ='rollup_matrix_numeric_12hours'
     and runner = procpid || '.' || date_part('epoch',backend_start);

  IF v_nrunning > 0 THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_12hours already running';
    RETURN ;
  END IF;

  SELECT INTO v_self procpid || '.' || date_part('epoch',backend_start)
    FROM pg_stat_activity
     WHERE procpid = pg_backend_pid();

  IF v_self IS NULL THEN
    RAISE EXCEPTION 'stratcon.rollup_matrix_numeric_12hours cannot self-identify';
   END IF;

   v_sql = 'update stratcon.rollup_runner set runner = ''' || v_self || ''' where rollup_table = ''rollup_matrix_numeric_12hours''';

  EXECUTE v_sql;

 FOR whenceint IN  SELECT * FROM stratcon.log_whence_s WHERE interval='12 hours' LOOP
 
  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='12 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_12hours 
         INTO v_max_rollup_12;    

/*-- Insert Log for 24 Hours rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/24)*24) * '1 hour'::interval and interval='24 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/24)*24) * '1 hour'::interval,'24 hours');
   END IF;
   */
   
  IF v_min_whence <= v_max_rollup_12 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_12hours 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value
		         FROM stratcon.rollup_matrix_numeric_6hours
		           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'12 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_12hours
          (sid,name,rollup_time,count_rows,avg_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='12 hours';

v_min_whence := NULL;
v_max_rollup_12 := NULL;

END LOOP;

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_12hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
      UPDATE stratcon.rollup_runner set runner = '' where rollup_table = 'rollup_matrix_numeric_12hours';
      RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


create or replace function
stratcon.fetch_varset(in_check uuid,
                       in_name text,
                       in_start_time timestamptz,
                       in_end_time timestamptz,
                       in_hopeful_nperiods int)
returns setof stratcon.loading_dock_metric_text_s_change_log as
$$
declare
  v_sid int;
  v_target record;
  v_start_adj timestamptz;
  v_start_text text;
  v_next_text text;
  v_end_adj timestamptz;
  v_change_row stratcon.loading_dock_metric_text_s_change_log%rowtype;
begin
  -- Map out uuid to an sid.
  select sid into v_sid from stratcon.map_uuid_to_sid where id = in_check;
  if not found then
    return;
  end if;

  select * into v_target from stratcon.choose_window(in_start_time, in_end_time, in_hopeful_nperiods);

  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_start_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_start_adj;

  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_end_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_end_adj;

  for v_change_row in
    select sid, 'epoch'::timestamp +
         ((floor(extract('epoch' from whence) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval as whence,
           name, value
      from stratcon.loading_dock_metric_text_s_change_log
     where sid = v_sid
       and name = in_name
       and whence <= v_start_adj
  order by 'epoch'::timestamp +
         ((floor(extract('epoch' from whence) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval desc
     limit 1
  loop
    v_start_text := coalesce(v_change_row.value, '[unset]');
  end loop;

  for v_change_row in
    select v_sid as sid, whence, in_name as name, value from
--    (select v_start_adj::timestamp + t * v_target.period::interval as whence
--      from generate_series(1, v_target.nperiods) t) s 
-- left join
    (select 'epoch'::timestamp +
         ((floor(extract('epoch' from whence) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval as whence,
           coalesce(value, '[unset]') as value
      from stratcon.loading_dock_metric_text_s_change_log
     where sid = v_sid
       and name = in_name
       and whence > v_start_adj
       and whence <= v_end_adj) d
--    using (whence)
  order by whence asc
  loop
    v_next_text := v_change_row.value;
    if v_change_row.value is not null and
       v_start_text != v_change_row.value then
      v_change_row.value := coalesce(v_start_text, '[unset]') || ' -> ' || coalesce(v_change_row.value, '[unset]');
    else
      v_change_row.value := v_start_text;
    end if;
    if v_next_text is not null then
      v_start_text := v_next_text;
    end if;
    return next v_change_row;
  end loop;

  return;
end
$$ language 'plpgsql';


create or replace function
stratcon.choose_window(in_start_time timestamptz,
                       in_end_time timestamptz,
                       in_hopeful_nperiods int,
                       out tablename text,
                       out period interval,
                       out nperiods int)
returns setof record as
$$
declare
  window record;
begin
  -- Figure out which table we should be looking in
  for window in
    select atablename, aperiod, anperiods
    from (select aperiod, iv/isec as anperiods, atablename,
                 abs(case when iv/isec - in_hopeful_nperiods < 0
                          then 10 * (in_hopeful_nperiods - iv/isec)
                          else iv/isec - in_hopeful_nperiods
                           end) as badness
            from (select extract('epoch' from in_end_time) -
                         extract('epoch' from in_start_time) as iv
                 ) i,
                 (   select 5*60 as isec, '5 minutes'::interval as aperiod,
                            'rollup_matrix_numeric_5m' as atablename
                  union all
                     select 20*60 as isec, '20 minutes'::interval as aperiod,
                            'rollup_matrix_numeric_20m' as atablename
                  union all
                     select 60*60 as isec, '1 hour'::interval as aperiod,
                            'rollup_matrix_numeric_60m' as atablename
                  union all
                     select 6*60*60 as isec, '6 hours'::interval as aaperiod,
                            'rollup_matrix_numeric_6hours' as atablename
                  union all
                     select 12*60*60 as isec, '12 hours'::interval as aperiod,
                            'rollup_matrix_numeric_12hours' as atablename
                 ) ivs
         ) b
 order by badness asc
  limit 1
  loop
    tablename := window.atablename;
    period := window.aperiod;
    nperiods := window.anperiods;
    return next;
  end loop;
  return;
end
$$ language 'plpgsql';

create or replace function
stratcon.fetch_dataset(in_check uuid,
                       in_name text,
                       in_start_time timestamptz,
                       in_end_time timestamptz,
                       in_hopeful_nperiods int,
                       derive boolean)
returns setof stratcon.rollup_matrix_numeric_5m as
$$
declare
  v_sql text;
  v_sid int;
  v_target record;
  v_interval numeric;
  v_start_adj timestamptz;
  v_end_adj timestamptz;
  v_l_rollup_row stratcon.rollup_matrix_numeric_5m%rowtype;
  v_rollup_row stratcon.rollup_matrix_numeric_5m%rowtype;
  v_r_rollup_row stratcon.rollup_matrix_numeric_5m%rowtype;
begin

  -- Map out uuid to an sid.
  select sid into v_sid from stratcon.map_uuid_to_sid where id = in_check;
  if not found then
    return;
  end if;

  select * into v_target from stratcon.choose_window(in_start_time, in_end_time, in_hopeful_nperiods);

  if not found then
    raise exception 'no target table';
    return;
  end if;

  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_start_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_start_adj;

  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_end_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_end_adj;

  v_sql := 'select ' || v_sid || ' as sid, ' || quote_literal(in_name) || ' as name, ' ||
           's.rollup_time, d.count_rows, d.avg_value ' ||
           ' from ' ||
           '(select ' || quote_literal(v_start_adj) || '::timestamp' ||
                  ' + t * ' || quote_literal(v_target.period) || '::interval' ||
                       ' as rollup_time' ||
             ' from generate_series(1,' || v_target.nperiods || ') t) s ' ||
           'left join ' ||
           '(select * from stratcon.' || v_target.tablename ||
           ' where sid = ' || v_sid ||
             ' and name = ' || quote_literal(in_name) ||
             ' and rollup_time between ' || quote_literal(v_start_adj) || '::timestamp' ||
                                 ' and ' || quote_literal(v_end_adj) || '::timestamp) d' ||
           ' using(rollup_time)';

  for v_rollup_row in execute v_sql loop
    if derive is true then
      v_r_rollup_row := v_rollup_row;
      if v_l_rollup_row.count_rows is not null and
         v_rollup_row.count_rows is not null then
        v_interval := extract('epoch' from v_rollup_row.rollup_time) - extract('epoch' from v_l_rollup_row.rollup_time);
        v_r_rollup_row.count_rows := (v_l_rollup_row.count_rows + v_rollup_row.count_rows) / 2;
        v_r_rollup_row.avg_value :=
          (v_rollup_row.avg_value - v_l_rollup_row.avg_value) / v_interval;
      else
        v_r_rollup_row.count_rows = NULL;
        v_r_rollup_row.avg_value = NULL;
        
      end if;
    else
      v_r_rollup_row := v_rollup_row;
    end if;
    return next v_r_rollup_row;
    v_l_rollup_row := v_rollup_row;
  end loop;
  return;
end
$$ language 'plpgsql';


-- Remove Metric based on UUID and Metric_Name

CREATE OR REPLACE FUNCTION stratcon.remove_metric   (in_uuid uuid,
                                                     in_metric_name text,
                                                     v_debug text,
                                                     OUT v_out text)
RETURNS text
AS $$
DECLARE
	v_del_sid INT;
	v_del_metric_name TEXT;
	v_del_metric_type TEXT;
	deleted_t INT;
	deleted_tc INT;
	deleted_n INT;
	deleted_5 INT;
	deleted_20 INT;
	deleted_60 INT;
	deleted_6h INT;
	deleted_12h INT;
	deleted_sum INT;

BEGIN
	  SELECT s.sid,m.metric_name,m.metric_type 
	    FROM
	             stratcon.map_uuid_to_sid s,
	             stratcon.metric_name_summary m 
	       WHERE s.id=in_uuid
	             and s.sid=m.sid
	             and m.metric_name=in_metric_name
	  INTO v_del_sid,v_del_metric_name,v_del_metric_type;
		IF NOT FOUND THEN
		   IF v_debug = 'DEBUG' THEN
		     RAISE NOTICE 'Given UUID can not map to SID,Metric Name: %,%',in_uuid,in_metric_name;
		   END IF;
		   v_out:='Please Supply Valid UUID,Metric Name Combination :'||in_uuid||','||in_metric_name;
		 RETURN;
		END IF;
IF v_debug = 'DEBUG' THEN
        RAISE NOTICE 'Delete In Progress For: %,%,%',v_del_sid,v_del_metric_name,v_del_metric_type;
END IF;

-- Check of Text or Numeric Type
IF v_del_metric_type ='text' THEN
 -- Delete from Metrix Tex table 
  DELETE FROM stratcon.loading_dock_metric_text_s WHERE sid=v_del_sid AND name=v_del_metric_name;
     GET DIAGNOSTICS deleted_t = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
           RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_text_s : %',deleted;
     END IF;
 -- Delete from Metrix Change Log table 
  DELETE FROM stratcon.loading_dock_metric_text_s_change_log WHERE sid=v_del_sid AND name=v_del_metric_name;
     GET DIAGNOSTICS deleted_tc = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
          RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_text_s_change_log : %',deleted;
     END IF;
 ELSE
  -- Delete from Metrix Numeric table
   DELETE FROM stratcon.loading_dock_metric_numeric_s WHERE sid=v_del_sid AND name=v_del_metric_name;
   GET DIAGNOSTICS deleted_n = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
         RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_numeric_s : %',deleted;
     END IF;
  -- Delete from Rollup tables
   DELETE FROM stratcon.rollup_matrix_numeric_5m WHERE sid=v_del_sid AND name=v_del_metric_name;
   GET DIAGNOSTICS deleted_5 = ROW_COUNT;   
     IF v_debug = 'DEBUG' THEN
         RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_5m : %',deleted;
     END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_20m WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_20= ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_20m : %',deleted;
        END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_60m WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_60 = ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_60m : %',deleted;
        END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_6hours WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_6h = ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_6hours : %',deleted;
        END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_12hours WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_12h = ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_12hours : %',deleted;
        END IF;
END IF;
  -- Delete from metrix summary table
   DELETE FROM stratcon.metrix_name_summary WHERE sid=v_del_sid AND metric_name=v_del_metric_name;
      GET DIAGNOSTICS deleted_sum= ROW_COUNT;     
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM metric_name_summary : %',deleted;
        END IF; 
 v_out:='Deleted Rows for Metric_Text, Metrix_Text_change_log,Metric_Numeric,Rollup_5m,Rollup_20m,Rollup_1hour,Rollup_6hours,Rollup_12hours,Metric_Summary:'||deleted_t||','||deleted_tc||','||deleted_n||','||deleted_5||','||deleted_20||','||deleted_60||','||deleted_6h||','||deleted_12h||','||deleted_sum;
RETURN;
   EXCEPTION
	    WHEN RAISE_EXCEPTION THEN
	            RAISE EXCEPTION '%', SQLERRM;
	    WHEN OTHERS THEN
	            RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;

COMMIT;
