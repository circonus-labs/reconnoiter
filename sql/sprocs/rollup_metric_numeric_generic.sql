CREATE OR REPLACE FUNCTION stratcon.rollup_metric_numeric_generic(in_roll text)
RETURNS int AS
$$
DECLARE  
 v_rec stratcon.metric_numeric_rollup_segment%rowtype;
 v_segment stratcon.metric_numeric_rollup_segment%rowtype;
 v_conf RECORD; 
 v_sql TEXT;
 v_min_whence TIMESTAMPTZ;
 v_max_rollup TIMESTAMPTZ;
 v_whence TIMESTAMPTZ;
 whenceint RECORD;
 v_taskid int;
 v_locked boolean;
 v_this_roll text; 
 v_stored_rollup timestamptz; 
 v_offset integer; 
 v_init boolean := FALSE ; 
 v_i smallint;
BEGIN

v_this_roll := 'rollup_metric_numeric_'||in_roll;
SELECT id FROM tasklock WHERE name = v_this_roll INTO v_taskid;
IF v_taskid IS NULL THEN
    INSERT INTO tasklock (id, name) VALUES (nextval('tasklock_id_seq'), v_this_roll)
      RETURNING id into v_taskid;
END IF;

SELECT pg_try_advisory_lock(43191, v_taskid) INTO v_locked;

IF v_locked = false THEN
    RAISE NOTICE 'rollup for metric numeric (%) already running', in_roll;
    RETURN 0;
END IF;

SELECT * FROM stratcon.metric_numeric_rollup_config WHERE rollup = in_roll INTO v_conf; 

LOOP
    IF v_i > 10 THEN
        RETURN 1;
    END IF; 

    v_sql := 'SELECT MIN(whence) FROM metric_numeric_rollup_queue WHERE interval='||in_roll; 
    EXECUTE v_sql INTO v_min_whence;
        
    v_sql := 'SELECT MAX(rollup_time) FROM numeric_metric_rollup_'||in_roll;
    EXECUTE v_sql INTO v_max_rollup;        

/* 
 -- Insert Log for Hourly rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('H',v_min_whence) and interval='1 hour'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('H',v_min_whence),'1 hour');
      END IF;
*/
 
    IF v_min_whence <= v_max_rollup THEN
        v_sql := 'DELETE FROM numeric_metric_rollup_'||in_roll||' WHERE rollup_time = '||quote_ident(v_min_whence);  
        EXECUTE v_sql; 
 
    END IF;

/* THIS V_SQL NEEDS TO BE REWRITTEN TO GET THE VALUE FROM ARRAY BASED TABLES */ 
    v_sql := 'SELECT sid, name, '|| v_min_whence || ' as rollup_time, 
                     SUM(1) as count_rows ,(SUM(avg_value*1)/SUM(1)) as avg_value, 
                     (SUM(counter_dev*1)/SUM(1)) as counter_dev 
              FROM metric_numeric_rollup_'||v_conf.dependent_on||' 
              WHERE rollup_time<= '|| v_min_whence ||' AND rollup_time > ' || v_min_whence - v_conf.seconds * '1 second'::interval || '
              GROUP BY sid, name';

    FOR v_rec IN EXECUTE v_sql LOOP
            v_stored_rollup := floor(extract('epoch' from v_rec.rollup_time)/v_conf.span)+vconf.window; 
            v_offset := floor( (extract('epoch' from v_rec.rollup_time) - v_stored_rollup) / v_conf.seconds );
 
            --v_offset := ( 12*(extract('hour' from v_info.rollup_time))+floor(extract('minute' from v_info.rollup_time)/5) );
            --v_stored_rollup := v_info.rollup_time::date;
            -- RAISE NOTICE 'sid %, name %, rollup_time %, offset %', v_rec.sid, v_rec.name, v_stored_rollup, v_offset;

            v_sql := 'SELECT * FROM metric_numeric_rollup_'||in_roll||' WHERE rollup_time = '||quote_literal(v_stored_rollup);
            v_sql := v_sql ||' and sid='||v_rec.sid||' and name = '|| quote_literal(v_rec.name);

            EXECUTE v_sql INTO v_segment;
            IF v_segment IS NOT NULL THEN
            	v_sql := 'SELECT * FROM stratcon.init_metric_numeric_rollup_segment('||quote_literal(in_roll)||')';
                EXECUTE v_sql INTO v_segment; 
                v_init := true; 
                RAISE NOTICE 'didnt find sid %, name %, rollup_time %, offset %', v_rec.sid, v_rec.name, v_stored_rollup, v_offset; 
            END IF;
   
            v_segment.sid := v_rec.sid;
            v_segment.name := v_rec.name;
            v_segment.count_rows[v_offset] := v_rec.count_rows;
            v_segment.avg_value[v_offset] := v_rec.avg_value;
            v_segment.counter_dev[v_offset] := v_rec.counter_dev;

            IF v_init THEN
                v_sql := 'INSERT INTO metric_numeric_rollup_'||in_roll||' (sid,name,rollup_time,count_rows,avg_value,counter_dev) 
                    VALUES ('|| v_segment.sid||','||quote_literal(v_segment.name)||','||quote_literal(v_stored_rollup)||','||v_segment.count_rows
                    ||','||v_segment.avg_value||','||v_segment.counter_dev||')';
                EXECUTE v_sql; 
                v_init := false;
            ELSE
                v_sql := 'UPDATE metric_numeric_rollup_'||in_roll;
                v_sql := v_sql || 'SET (count_rows,avg_value,counter_dev) = ('||v_rec.count_rows||','||v_rec.avg_value||','||v_rec.counter_dev||')'; 
                v_sql := v_sql || 'WHERE rollup_time = '||v_stored_rollup||'  AND sid = '||v_info.sid||'  AND name = '||quote_literal(v_info.name);
            END IF;

    v_i := v_i + 1; 
    END LOOP; 

  -- Delete from whence log table
  
  DELETE FROM metric_numeric_rollup_queue WHERE WHENCE=v_min_whence AND INTERVAL=in_roll;
 
  v_min_whence := NULL;
  v_max_rollup := NULL;

 END LOOP;

  perform pg_advisory_unlock(43191, v_taskid);

  RETURN 0;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       perform pg_advisory_unlock(43191, v_taskid);
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
       perform pg_advisory_unlock(43191, v_taskid);
       RAISE NOTICE '%', SQLERRM;
END
$$ LANGUAGE plpgsql; 
