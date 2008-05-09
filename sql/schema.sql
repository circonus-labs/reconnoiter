BEGIN;

-- Tables 

CREATE TABLE stratcon.loading_dock_check_s (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL,
    PRIMARY KEY(sid,id,whence)
);

CREATE TABLE stratcon.loading_dock_status_s (
    sid integer NOT NULL,
    whence timestamp NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text,
    PRIMARY KEY(sid,whence)
);

CREATE TABLE stratcon.loading_dock_metric_numeric_s (
    sid integer NOT NULL,
    whence timestamp NOT NULL,
    name text NOT NULL,
    value numeric,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s (
    sid integer NOT NULL,
    whence timestamp NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s_change_log (
    sid integer NOT NULL,
    whence timestamp NOT NULL,
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
   rollup_time timestamp not null, 
   count_rows integer,
   avg_value numeric ,
   stddev_value numeric,
   min_value numeric ,
   max_value numeric ,
   PRIMARY KEY(rollup_time6,sid,name));   

CREATE TABLE stratcon.rollup_matrix_numeric_12hours(
   sid integer not null,
   name text not null, 
   rollup_time timestamp not null, 
   count_rows integer,
   avg_value numeric ,
   stddev_value numeric,
   min_value numeric ,
   max_value numeric ,
   PRIMARY KEY(rollup_time12,sid,name));      
 
CREATE TABLE stratcon.rollup_matrix_numeric_5m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp NOT NULL,
    count_rows integer,
    avg_value numeric,
    stddev_value numeric,
    min_value numeric,
    max_value numeric,
    PRIMARY KEY (rollup_time,sid,name)
);

CREATE TABLE stratcon.rollup_matrix_numeric_20m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp NOT NULL,
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
    whence timestamp NOT NULL,
    interval varchar2(20,
    PRIMARY KEY(whence,interval)
);

CREATE TABLE stratcon.rollup_runner (
  rollup_table character varying(100),
  runner character varying(22)
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
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_20m TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_6hours TO stratcon;
 GRANT SELECT,INSERT,DELETE ON stratcon.rollup_matrix_numeric_12hours TO stratcon;
 GRANT SELECT,INSERT ON stratcon.map_uuid_to_sid TO stratcon;
 GRANT SELECT,INSERT,UPDATE,DELETE ON stratcon.rollup_runner TO stratcon;
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
 v_min_whence TIMESTAMP;
 v_max_rollup_5 TIMESTAMP;
 v_whence TIMESTAMP;
 rows INT;
 v_nrunning INT;
 v_self VARCHAR(22);
 
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
 
  UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_5m';
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_5m';
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_5m';
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
 v_min_whence TIMESTAMP;
 v_max_rollup_20 TIMESTAMP;
 v_whence TIMESTAMP;
 rows INT;
 v_nrunning INT;
 v_self VARCHAR(22);

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
                       SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		       stddev(stddev_value) as stddev_value,
		       MIN(min_value) as min_value ,MAX(max_value) as max_value
		       FROM stratcon.rollup_matrix_numeric_5m
                      WHERE rollup_time<= v_min_whence AND rollup_time > v_min_whence -'20 minutes'::interval
                GROUP BY rollup_time,sid,name
 
       LOOP
    
        
        INSERT INTO stratcon.rollup_matrix_numeric_20m
         (sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES 
         (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
        
   END LOOP;

  -- Delete from whence log table
  
  DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='20 minutes';
 
  UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
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
  v_min_whence TIMESTAMP;
  v_max_rollup_60 TIMESTAMP;
  v_whence TIMESTAMP;
  v_nrunning INT;
  v_self VARCHAR(22);

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

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='1 hour'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_60m 
         INTO v_max_rollup_60;    

-- Insert Log for 6 Hour rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='6 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'6 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_60 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_60m 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,date_trunc('hour',rollup_time) as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		         stddev(stddev_value) as stddev_value,
		         MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_20m
		           WHERE date_trunc('hour',rollup_time)= date_trunc('hour',v_min_whence)
                   GROUP BY date_trunc('hour',rollup_time),sid,name
        LOOP
      
          INSERT INTO stratcon.rollup_matrix_numeric_60m
          (sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='1 hour';

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_60m';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_60m';
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_60m';
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
  v_min_whence TIMESTAMP;
  v_max_rollup_6 TIMESTAMP;
  v_whence TIMESTAMP;
  v_nrunning INT;
  v_self VARCHAR(22);
    
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

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='6 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_6hours 
         INTO v_max_rollup_6;    

-- Insert Log for 12 Hours rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='12 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'12 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_6 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_6hours 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		         STDDEV(stddev_value) as  stddev_value,
		         MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_60m
		           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'6 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_6hours
          (sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='6 hours';

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_6hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_6hours'; 
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_6hours';
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
  v_min_whence TIMESTAMP;
  v_max_rollup_12 TIMESTAMP;
  v_whence TIMESTAMP;
  v_nrunning INT;
  v_self VARCHAR(22);
 
 
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

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='12 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_12hours 
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
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
		         STDDEV(stddev_value) as stddev_value,
		         MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_6hours
		           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'12 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_12hours
          (sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.stddev_value,rec.min_value,rec.max_value);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='12 hours';

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_12hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
      UPDATE stratcon.rollup_runner set runner = '' where rollup_table = 'rollup_matrix_numeric_12hours';
      RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      UPDATE stratcon.rollup_runner set runner = '' where rollup_table = 'rollup_matrix_numeric_12hours';
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


create or replace function
stratcon.fetch_varset(in_check uuid,
                       in_name text,
                       in_start_time timestamp,
                       in_end_time timestamp,
                       in_hopeful_nperiods int)
returns setof stratcon.loading_dock_metric_text_s_change_log as
$$
declare
  v_sid int;
  v_target record;
  v_start_adj timestamp;
  v_start_text text;
  v_next_text text;
  v_end_adj timestamp;
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
stratcon.choose_window(in_start_time timestamp,
                       in_end_time timestamp,
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
                       in_start_time timestamp,
                       in_end_time timestamp,
                       in_hopeful_nperiods int,
                       derive boolean)
returns setof stratcon.rollup_matrix_numeric_5m as
$$
declare
  v_sql text;
  v_sid int;
  v_target record;
  v_interval numeric;
  v_start_adj timestamp;
  v_end_adj timestamp;
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

  if not found then
    raise exception 'no target table';
    return;
  end if;

  v_sql := 'select ' || v_sid || ' as sid, ' || quote_literal(in_name) || ' as name, ' ||
           's.rollup_time, d.count_rows, d.avg_value, ' ||
           'd.stddev_value, d.min_value, d.max_value ' ||
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
        v_r_rollup_row.stddev_value :=
          (v_rollup_row.stddev_value - v_l_rollup_row.stddev_value) / v_interval;
        v_r_rollup_row.min_value :=
          (v_rollup_row.min_value - v_l_rollup_row.min_value) / v_interval;
        v_r_rollup_row.max_value :=
          (v_rollup_row.max_value - v_l_rollup_row.max_value) / v_interval;
      else
        v_r_rollup_row.count_rows = NULL;
        v_r_rollup_row.avg_value = NULL;
        v_r_rollup_row.stddev_value = NULL;
        v_r_rollup_row.min_value = NULL;
        v_r_rollup_row.max_value = NULL;
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

COMMIT;
