CREATE OR REPLACE FUNCTION stratcon.rollup_metric_numeric_generic(in_roll text) RETURNS int AS $$
DECLARE
    v_rec           stratcon.metric_numeric_rollup_segment%rowtype;
    v_segment       stratcon.metric_numeric_rollup_segment%rowtype;
    v_conf          RECORD;
    v_sql           TEXT;
    v_min_whence    TIMESTAMPTZ;
    v_max_rollup    TIMESTAMPTZ;
    v_whence        TIMESTAMPTZ;
    v_taskid        INT;
    v_locked        BOOLEAN;
    v_this_roll     TEXT;
    v_stored_rollup TIMESTAMPTZ;
    v_offset        INTEGER;
    v_init          BOOLEAN := FALSE;
    v_i             SMALLINT;
    v_temprec       RECORD;
BEGIN
    -- Get rollup config based on given name, and fail if its wrong name.
    SELECT * FROM stratcon.metric_numeric_rollup_config WHERE rollup = in_roll INTO v_conf;
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
        IF v_i > 10 THEN
            RETURN 1;
        END IF;

        SELECT MIN(whence) FROM metric_numeric_rollup_queue WHERE "interval" = in_roll;
        EXIT WHEN NOT FOUND;

        v_sql := 'SELECT MAX(rollup_time) FROM metric_numeric_rollup_' || in_roll;
        EXECUTE v_sql INTO v_max_rollup;

        IF v_min_whence <= v_max_rollup THEN
            v_sql := 'DELETE FROM metric_numeric_rollup_'||in_roll||' WHERE rollup_time = '||quote_literal(v_min_whence);
            EXECUTE v_sql;
        END IF;

        -- now() in following query is just a placeholder to get named field (use_whence) in temprec.
        FOR v_temprec IN SELECT *, now() as use_whence FROM noit.metric_numeric_rollup_config WHERE dependent_on = in_roll LOOP
            -- Following formula gives equivalent of date_trunc(..) but working on basically any unit - like "10 minutes"
            -- The unit has to be given in seconds, AND provided as v_temprec.seconds
            v_temprec.use_whence := 'epoch'::timestamptz + '1 second'::INTERVAL * v_temprec.seconds * floor(extract( epoch FROM now() ) / v_temprec.seconds);

            -- Poor mans UPSERT :)
            INSERT INTO metric_numeric_rollup_queue ("interval", whence)
                SELECT v_temprec.rollup, v_temprec.use_whence
                WHERE NOT EXISTS (
                    SELECT * FROM metric_numeric_rollup_queue WHERE ( "INTERVAL", whence ) = ( v_temprec.rollup, v_temprec.use_whence )
                );
        END LOOP;

        v_sql := 'SELECT sid, name, $2 as rollup_time, SUM(1) as count_rows, (SUM(avg_value*1)/SUM(1)) as avg_value, (SUM(counter_dev*1)/SUM(1)) as counter_dev
                  FROM  stratcon.unroll_metric_numeric( $2, $1, $3)
                  GROUP BY sid, name';

        FOR v_rec IN EXECUTE v_sql USING v_min_whence - v_conf.seconds * '1 second'::INTERVAL, v_min_whence, v_conf.dependent_on LOOP
            v_stored_rollup := floor( extract('epoch' from v_rec.rollup_time) / v_conf.span ) + v_conf.window;
            v_offset        := floor( ( extract('epoch' from v_rec.rollup_time) - v_stored_rollup) / v_conf.seconds );

            --v_offset := ( 12*(extract('hour' from v_info.rollup_time))+floor(extract('minute' from v_info.rollup_time)/5) );
            --v_stored_rollup := v_info.rollup_time::date;
            -- RAISE NOTICE 'sid %, name %, rollup_time %, offset %', v_rec.sid, v_rec.name, v_stored_rollup, v_offset;

            v_sql := 'SELECT * FROM metric_numeric_rollup_'||in_roll||' WHERE rollup_time = '||quote_literal(v_stored_rollup);
            v_sql := v_sql ||' and sid='||v_rec.sid||' and name = '|| quote_literal(v_rec.name);

            EXECUTE v_sql INTO v_segment;
            IF v_segment IS NOT NULL THEN
                v_segment := stratcon.init_metric_numeric_rollup_segment( in_roll );
                v_init := true;
                RAISE NOTICE 'didnt find sid %, name %, rollup_time %, offset %', v_rec.sid, v_rec.name, v_stored_rollup, v_offset;
            END IF;

            v_segment.sid                   := v_rec.sid;
            v_segment.name                  := v_rec.name;
            v_segment.count_rows[v_offset]  := v_rec.count_rows;
            v_segment.avg_value[v_offset]   := v_rec.avg_value;
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
