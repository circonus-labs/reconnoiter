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
    PRIMARY KEY(whence)
);

-- Schema Sequence 

CREATE SEQUENCE stratcon.seq_sid
    START WITH 50
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;


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

CREATE TRIGGER loading_dock_metric_numeric_s_whence_log
    AFTER INSERT ON loading_dock_metric_numeric_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_numeric_s_whence_log();

    
CREATE FUNCTION stratcon.loading_dock_metric_numeric_s_whence_log() RETURNS trigger
    AS $$
DECLARE
v_whence timestamptz;
BEGIN
IF TG_OP = 'INSERT' THEN
    SELECT whence FROM stratcon.log_whence_s WHERE whence=NEW.whence
     INTO v_whence;
   IF NOT FOUND THEN
       UPDATE stratcon.log_whence_s SET whence=NEW.whence;
   END IF;
ELSE
        RAISE EXCEPTION 'Non-INSERT DML operation attempted on INSERT only table';
END IF;

    RETURN NULL;
END
$$
    LANGUAGE plpgsql;




-- Generic rollup function (under progress)



CREATE OR REPLACE FUNCTION stratcon.generic_rollup_metrix_numeric()
RETURNS void
AS $$

DECLARE

v_min_whence TIMESTAMPTZ;
v_max_rollup_5 TIMESTAMPTZ;
v_max_rollup_60 TIMESTAMPTZ;

BEGIN

  select min(whence) from stratcon.log_whence_s 
         INTO v_min_whence;
         
  select max(rollup_time) from  stratcon.rollup_matrix_numeric_5m 
         INTO v_max_rollup_5;         
 
  select max(rollup_time) from  stratcon.rollup_matrix_numeric_60m 
         INTO v_max_rollup_60; 
         
 IF v_max_rollup_5 IS NULL  THEN
   v_max_rollup_5:=timestamp '2008-01-01 00:00:00';
 END IF;
 
 IF v_max_rollup_60 IS NULL  THEN
    v_max_rollup_60:=timestamp '2008-01-01 00:00:00';
  END IF;
 
         
  IF v_min_whence > v_max_rollup_5 THEN
  
  -- 5 MINUTES ROLLUP
  
     PERFORM stratcon.rollup_matrix_numeric_5m(v_min_whence);
     
     -- HOURLY ROLLUP
     
     IF  date_trunc('H',v_min_whence)!= date_trunc('H',v_max_rollup_60) THEN
     
       PERFORM stratcon.rollup_matrix_numeric_60m(v_min_whence);
     
     END IF;
  
  -- DELETE FROM LOG TABLE
  
   DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence;
  
  ELSIF v_min_whence < v_max_rollup_5 THEN
  
   -- 5 MINUTES ROLLUP

     PERFORM stratcon.rollup_matrix_numeric_5m_odd(v_min_whence ,v_max_rollup_5);
     
  -- HOURLY ROLLUP
   
   DELETE FROM stratcon.rollup_matrix_numeric_60m 
       WHERE date_trunc('hour',rollup_time) = date_trunc('hour',v_min_whence);
       
         PERFORM stratcon.rollup_matrix_numeric_60m(v_min_whence);
         
  -- DELETE FROM LOG TABLE

      DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence;
      
  ELSE
  
      RETURN;
 
  END IF;
 
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


--- 5 minutes rollup 


CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_5m(v_min_whence timestamptz)
RETURNS voidddddd
AS $$
DECLARE
 
 rec stratcon.rollup_matrix_numeric_5m%rowtype;
 v_sql TEXT;
 
BEGIN

 FOR rec IN 
                SELECT sid , name, date_trunc('H',whence) + (round(extract('minute' from whence)/5)*5) * '1 minute'::interval as rollup_time,
                      COUNT(1) as count_rows ,AVG(value) as avg_value,STDDEV(value) as stddev_value ,MIN(value) as min_value ,MAX(value) as max_value
                      FROM stratcon.loading_dock_metric_numeric_s
                      WHERE WHENCE < date_trunc('minutes',v_min_whence) AND WHENCE >= date_trunc('minutes',v_min_whence)-'5 minutes'::interval
                GROUP BY rollup_time,sid,name
 
       LOOP
 
        v_sql:= 'INSERT INTO stratcon.rollup_matrix_numeric_5m'||                        
        '(sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES '||
        '('||rec.sid||', '||quote_literal(rec.name)||', '||quote_literal(rec.rollup_time)||', '||rec.count_rows||', '||rec.avg_value||', '||coalesce(rec.stddev_value,0)||
        ', '||rec.min_value||', '||rec.max_value||')';


          EXECUTE v_sql;

 END LOOP;

 
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;



CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_5m_odd(v_min_whence timestamptz,v_max_rollup_5 timestamptz)
RETURNS void
AS $$
DECLARE
 
 rec stratcon.rollup_matrix_numeric_5m%rowtype;
 v_sql TEXT;
 
BEGIN

 DELETE FROM stratcon.rollup_matrix_numeric_5m 
 WHERE rollup_time >= date_trunc('minutes',v_min_whence);
 
 FOR rec IN 
                 SELECT sid , name, date_trunc('H',whence) + (round(extract('minute' from whence)/5)*5) * '1 minute'::interval as rollup_time,
                       COUNT(1) as count_rows ,AVG(value) as avg_value,STDDEV(value) as stddev_value ,MIN(value) as min_value ,MAX(value) as max_value
                       FROM stratcon.loading_dock_metric_numeric_s
                       WHERE WHENCE >= date_trunc('minutes',v_min_whence) AND WHENCE <= date_trunc('minutes',v_max_rollup_5)
                 GROUP BY rollup_time,sid,name
  
        LOOP
 
    
         v_sql:= 'INSERT INTO stratcon.rollup_matrix_numeric_5m'||                        
         '(sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES '||
         '('||rec.sid||', '||quote_literal(rec.name)||', '||quote_literal(rec.rollup_time)||', '||rec.count_rows||', '||rec.avg_value||', '||coalesce(rec.stddev_value,0)||
         ', '||rec.min_value||', '||rec.max_value||')';
 
 
           EXECUTE v_sql;
 
  END LOOP;
 

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;






--- Hourly rollup


CREATE OR REPLACE FUNCTION stratcon.rollup_matrix_numeric_60m(v_min_whence timestamptz)
RETURNS void
AS $$
DECLARE
  rec stratcon.rollup_matrix_numeric_60m%rowtype;
 v_sql TEXT;
 
BEGIN
    FOR rec IN 
                SELECT sid , name,date_trunc('hour',rollup_time) as rollup_time,SUM (count_rows) as count_rows ,AVG(avg_value) as avg_value,
		         STDDEV(stddev_value) as stddev_value ,MIN(min_value) as min_value ,MAX(max_value) as max_value
		         FROM stratcon.rollup_matrix_numeric_5m
		           WHERE date_trunc('hour',rollup_time)= date_trunc('hour',v_min_whence)
                   GROUP BY rollup_time,sid,name
        LOOP
        v_sql:= 'INSERT INTO stratcon.rollup_matrix_numeric_60m'||                        
        '(sid,name,rollup_time,count_rows,avg_value,stddev_value,min_value,max_value) VALUES '||
        '('||rec.sid||', '||quote_literal(rec.name)||', '||quote_literal(rec.rollup_time)||', '||rec.count_rows||', '||rec.avg_value||', '||coalesce(rec.stddev_value,0)||
        ', '||rec.min_value||', '||rec.max_value||')';

          EXECUTE v_sql;
     END LOOP;
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql;


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
 GRANT SELECT,INSERT ON stratcon.map_uuid_to_sid TO stratcon;
 ALTER TABLE stratcon.seq_sid OWNER TO stratcon;

COMMIT;

