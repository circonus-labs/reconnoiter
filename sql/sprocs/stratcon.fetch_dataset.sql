
CREATE OR REPLACE FUNCTION stratcon.fetch_dataset
(in_uuid uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) 
RETURNS SETOF stratcon.metric_numeric_rollup_segment
AS $$
DECLARE
  v_sid int;
  v_record stratcon.metric_numeric_rollup_segment%rowtype;
BEGIN
  SELECT sid FROM stratcon.map_uuid_to_sid WHERE id = in_uuid INTO v_sid;
  IF NOT FOUND THEN
    RETURN;
  END IF;

/* i think this was only needed for < 8.4
    for v_record in  select sid, name, rollup_time, count_rows, avg_value, counter_dev from stratcon.fetch_dataset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods, derive) loop
    return next v_record; 
    end loop;
*/

  RETURN QUERY SELECT sid, name, rollup_time, count_rows, avg_value 
                   FROM stratcon.fetch_dataset(v_sid, in_name, in_start_time, in_end_time, in_hopeful_nperiods, derive);

  RETURN;
END
$$
LANGUAGE plpgsql
SECURITY DEFINER
;



CREATE OR REPLACE FUNCTION stratcon.fetch_dataset
(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) 
RETURNS SETOF stratcon.metric_numeric_rollup_segment
AS $_$
DECLARE
    v_sql           text;
    v_sql_subtable  text;
    v_target        record;
    v_interval      numeric;
    v_start_adj     timestamptz;
    v_end_adj       timestamptz;
    v_l_rollup_row  stratcon.metric_numeric_rollup_segment%rowtype;
    v_rollup_row    stratcon.metric_numeric_rollup_segment%rowtype;
    v_r_rollup_row  stratcon.metric_numeric_rollup_segment%rowtype;
BEGIN
    SELECT *, extract('epoch' FROM period) as epoch_period INTO v_target
        FROM stratcon.choose_window(in_start_time, in_end_time, in_hopeful_nperiods);
    IF NOT FOUND THEN
        RAISE EXCEPTION 'no target table';
        RETURN;
    END IF;

    -- round start and end timestamps to period precision (i.e. to 5 minutes, or 1 hour, or ...)
    v_start_adj := ( 'epoch'::timestamp + v_target.period * floor( extract('epoch' from in_start_time) / v_target.epoch_period ) ) AT TIME ZONE 'UTC';
    v_end_adj   := ( 'epoch'::timestamp + v_target.period * floor( extract('epoch' from in_end_time)   / v_target.epoch_period ) ) AT TIME ZONE 'UTC';

    -- build sql using placeholders ([something]) to make it more readable than using ' || ... || ' all the time.
    v_sql_subtable := $SQL$
        select *
        from stratcon.[tablename]
        where
            sid = [in_sid]
            and "name" = [in_name]
            and rollup_time between [v_start_adj]::timestamp AND [v_end_adj]::timestamp
    $SQL$;
    IF v_target.tablename = 'metric_numeric_rollup_5m' THEN

        v_sql_subtable := $SQL$
            SELECT *
            FROM stratcon.unroll_metric_numeric_5m(
                [in_sid],
                [in_name],
                [v_start_adj]::timestamp,
                [v_end_adj]::timestamp
            )
        $SQL$;

    END IF;

    v_sql := $SQL$
        select
            [in_sid] as sid,
            [in_name] as name,
            s.rollup_time,
            d.count_rows,
            d.avg_value,
            d.counter_dev 
        from 
            (
                select [v_start_adj]::timestamp + t * [period]::interval as rollup_time
                from generate_series(1, [nperiods]) t
            ) s 
            left join ( [subtable] ) d using(rollup_time)
    $SQL$;
    -- change placeholders to real values.
    v_sql := replace( v_sql, '[subtable]',    v_sql_subtable::TEXT              ); -- this one has to be the first, as it might contain other placeholder
    v_sql := replace( v_sql, '[in_sid]',      in_sid::TEXT                      );
    v_sql := replace( v_sql, '[in_name]',     quote_literal( in_name )          );
    v_sql := replace( v_sql, '[v_start_adj]', quote_literal( v_start_adj )      );
    v_sql := replace( v_sql, '[v_end_adj]',   quote_literal( v_end_adj )        );
    v_sql := replace( v_sql, '[period]',      quote_literal( v_target.period )  );
    v_sql := replace( v_sql, '[nperiods]',    v_target.nperiods::TEXT           );
    v_sql := replace( v_sql, '[tablename]',   quote_ident( v_target.tablename ) );

    FOR v_rollup_row IN EXECUTE v_sql LOOP
        IF derive IS TRUE THEN
            v_r_rollup_row := v_rollup_row;
            IF v_l_rollup_row.count_rows  IS NOT NULL AND
                v_rollup_row.count_rows   IS NOT NULL THEN
                v_interval                := extract('epoch' from v_rollup_row.rollup_time) - extract('epoch' from v_l_rollup_row.rollup_time);
                v_r_rollup_row.count_rows := (v_l_rollup_row.count_rows + v_rollup_row.count_rows) / 2;
                v_r_rollup_row.avg_value  := (v_rollup_row.avg_value - v_l_rollup_row.avg_value) / v_interval;
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
$_$
    LANGUAGE plpgsql;

