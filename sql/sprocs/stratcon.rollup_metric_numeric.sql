CREATE OR REPLACE FUNCTION stratcon.rollup_metric_numeric(in_roll text) RETURNS int AS $$
DECLARE
    v_rec           stratcon.metric_numeric_rollup_segment%rowtype;
    v_segment       stratcon.metric_numeric_rollup%rowtype;
    v_conf          RECORD;
    v_sql           TEXT;
    v_min_whence    TIMESTAMPTZ;
    v_max_rollup    TIMESTAMPTZ;
    v_whence        TIMESTAMPTZ;
    v_taskid        INTEGER;
    v_locked        BOOLEAN;
    v_this_roll     TEXT;
    v_stored_rollup INTEGER;
    v_stored_rollup_tm TIMESTAMPTZ;
    v_offset        INTEGER;
    v_init          BOOLEAN := FALSE;
    v_i             SMALLINT;
    v_temprec       RECORD;
    v_count         INTEGER;
BEGIN

    -- Get rollup config based on given name, and fail if its wrong name.
    SELECT * FROM metric_numeric_rollup_config WHERE rollup = in_roll INTO v_conf;
    IF NOT FOUND THEN
        raise exception 'Given rollup name is invalid! [%]', in_roll;
    END IF;

    -- Get task id - used for locking - based on given roll name
    v_this_roll := 'rollup_metric_numeric_'||in_roll;
    SELECT id FROM tasklock WHERE "name" = v_this_roll INTO v_taskid;
    IF v_taskid IS NULL THEN
        INSERT INTO tasklock (id, "name") VALUES (nextval('tasklock_id_seq'), v_this_roll) RETURNING id into v_taskid;
    END IF;

    -- Try to lock task_id - to make sure only one stratcon.rollup_metric_numeric_generic() runs at a time for this particular in_roll.
    SELECT pg_try_advisory_lock(43191, v_taskid) INTO v_locked;
    IF v_locked = false THEN
        RAISE NOTICE 'rollup for metric numeric (%) already running', in_roll;
        RETURN 0;
    END IF;

    LOOP
        IF v_i > 12 THEN
            RETURN 1;
        END IF;

        SELECT MIN(whence) FROM metric_numeric_rollup_queue WHERE "interval" = in_roll INTO v_min_whence;
        EXIT WHEN NOT FOUND OR v_min_whence IS NULL;

        v_sql := 'SELECT MAX(rollup_time) FROM metric_numeric_rollup_' || in_roll;
        EXECUTE v_sql INTO v_max_rollup;

        -- now() in following query is just a placeholder to get named field (use_whence) in temprec.
        FOR v_temprec IN SELECT *, now() as use_whence FROM noit.metric_numeric_rollup_config WHERE dependent_on = in_roll LOOP
            -- Following formula gives equivalent of date_trunc(..) but working on basically any unit - like "10 minutes"
            -- The unit has to be given in seconds, AND provided as v_temprec.seconds
            v_temprec.use_whence := 'epoch'::timestamptz + '1 second'::INTERVAL * v_temprec.seconds * floor(extract( epoch FROM now() ) / v_temprec.seconds);

RAISE NOTICE '(%,%)',v_temprec.rollup, v_temprec.use_whence; 
            -- Poor mans UPSERT :)
            INSERT INTO metric_numeric_rollup_queue ("interval", whence)
                SELECT v_temprec.rollup, v_temprec.use_whence
                WHERE NOT EXISTS (
                    SELECT * FROM metric_numeric_rollup_queue WHERE ( "interval", whence ) = ( v_temprec.rollup, v_temprec.use_whence )
                );
        END LOOP;

        IF in_rollup = '5m' THEN
            v_sql := 'SELECT * FROM stratcon.window_robust_derive('||quote_literal(v_min_whence)||')';
        ELSE
            v_sql := 'SELECT sid, name, '||quote_literal(v_min_whence)||' as rollup_time, SUM(1) as count_rows, (SUM(avg_value*1)/SUM(1)) as avg_value,';
            v_sql := v_sql || ' (SUM(counter_dev*1)/SUM(1)) as counter_dev FROM stratcon.unroll_metric_numeric('||quote_literal(v_min_whence)||',';
            v_sql := v_sql || quote_literal(v_min_whence + (v_conf.seconds - 1) * '1 second'::interval) || ',' || quote_literal(v_conf.dependent_on) ||')';
            v_sql := v_sql || ' GROUP BY sid, name';  

       --   v_sql := 'SELECT sid, name, $1 as rollup_time, SUM(1) as count_rows, (SUM(avg_value*1)/SUM(1)) as avg_value, (SUM(counter_dev*1)/SUM(1)) as counter_dev
       --             FROM  stratcon.unroll_metric_numeric( $1, $2, $3)
       --             GROUP BY sid, name';
        END IF;
RAISE NOTICE 'v_sql was (%),v_sql; 

        FOR v_rec IN EXECUTE v_sql LOOP 
            v_stored_rollup := floor( extract('epoch' from v_rec.rollup_time) / v_conf.span ) * v_conf.span;
            v_stored_rollup_tm := 'epoch'::timestamptz + v_stored_rollup * '1 second'::interval;
            v_offset        := floor( ( extract('epoch' from v_rec.rollup_time) - v_stored_rollup) / v_conf.seconds );

            v_sql := 'SELECT * FROM metric_numeric_rollup_'||in_roll||' WHERE rollup_time = '||quote_literal(v_stored_rollup_tm);
            v_sql := v_sql ||' and sid='||v_rec.sid||' and name = '|| quote_literal(v_rec.name);

            EXECUTE v_sql INTO v_segment;
            GET DIAGNOSTICS v_count = ROW_COUNT;
            IF v_count = 0 THEN
                v_segment := stratcon.init_metric_numeric_rollup( in_roll );
                v_init := true;
                RAISE NOTICE 'didnt find sid %, name %, rollup_time %, offset %', v_rec.sid, v_rec.name, v_stored_rollup_tm, v_offset;
            END IF;

            v_segment.sid                   := v_rec.sid;
            v_segment.name                  := v_rec.name;
            v_segment.count_rows[v_offset]  := v_rec.count_rows;
            v_segment.avg_value[v_offset]   := v_rec.avg_value;
            v_segment.counter_dev[v_offset] := v_rec.counter_dev;

            IF v_init THEN
                v_sql := 'INSERT INTO metric_numeric_rollup_'||in_roll||' (sid,name,rollup_time,count_rows,avg_value,counter_dev)
                    VALUES ($1,$2,$3,$4,$5,$6)';
                EXECUTE v_sql USING v_segment.sid, v_segment.name, v_stored_rollup_tm, v_segment.count_rows, v_segment.avg_value, v_segment.counter_dev;
                v_init := false;
            ELSE
                v_sql := 'UPDATE metric_numeric_rollup_'||in_roll;
                v_sql := v_sql || ' SET (count_rows,avg_value,counter_dev) = ($1,$2,$3)';
                v_sql := v_sql || ' WHERE rollup_time = $4  AND sid = $5 AND name = $6';
                EXECUTE v_sql USING v_segment.count_rows, v_segment.avg_value, v_segment.counter_dev, v_stored_rollup_tm, v_segment.sid, v_segment.name; 
            END IF;

        v_i := v_i + 1;
        END LOOP;

        -- Delete from whence log table
        DELETE FROM metric_numeric_rollup_queue WHERE whence=v_min_whence AND "interval"=in_roll;

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
$$ LANGUAGE plpgsql
SECURITY DEFINER
;
