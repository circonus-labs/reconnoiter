--
-- PostgreSQL database dump
--

SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

--
-- Name: prism; Type: SCHEMA; Schema: -; Owner: prism
--

CREATE SCHEMA prism;


ALTER SCHEMA prism OWNER TO prism;

--
-- Name: stratcon; Type: SCHEMA; Schema: -; Owner: stratcon
--

CREATE SCHEMA stratcon;


ALTER SCHEMA stratcon OWNER TO stratcon;

SET search_path = prism, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: graph_templates; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE graph_templates (
    templateid uuid NOT NULL,
    title text NOT NULL,
    json text NOT NULL
);


ALTER TABLE prism.graph_templates OWNER TO reconnoiter;

--
-- Name: saved_graphs; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE saved_graphs (
    graphid uuid NOT NULL,
    json text NOT NULL,
    saved boolean DEFAULT false NOT NULL,
    title text,
    last_update timestamp without time zone NOT NULL,
    ts_search_all tsvector,
    graph_tags text[],
    genesis text
);


ALTER TABLE prism.saved_graphs OWNER TO reconnoiter;

--
-- Name: saved_graphs_dep; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE saved_graphs_dep (
    graphid uuid NOT NULL,
    sid integer NOT NULL,
    metric_name text NOT NULL,
    metric_type character varying(22)
);


ALTER TABLE prism.saved_graphs_dep OWNER TO reconnoiter;

--
-- Name: saved_worksheets; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE saved_worksheets (
    sheetid uuid NOT NULL,
    title text,
    saved boolean DEFAULT false,
    ts_search_all tsvector,
    tags text[],
    last_update timestamp with time zone DEFAULT now() NOT NULL
);


ALTER TABLE prism.saved_worksheets OWNER TO reconnoiter;

--
-- Name: saved_worksheets_dep; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE saved_worksheets_dep (
    sheetid uuid NOT NULL,
    ordering integer NOT NULL,
    graphid uuid NOT NULL
);


ALTER TABLE prism.saved_worksheets_dep OWNER TO reconnoiter;

SET search_path = stratcon, pg_catalog;

--
-- Name: check_tags; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE check_tags (
    sid integer NOT NULL,
    tags_array text[]
);


ALTER TABLE stratcon.check_tags OWNER TO reconnoiter;

--
-- Name: current_metric_text; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE current_metric_text (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


ALTER TABLE stratcon.current_metric_text OWNER TO reconnoiter;

--
-- Name: current_node_config; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE current_node_config (
    remote_address inet NOT NULL,
    node_type text NOT NULL,
    whence timestamp with time zone NOT NULL,
    config xml NOT NULL
);


ALTER TABLE stratcon.current_node_config OWNER TO reconnoiter;

--
-- Name: current_node_config_changelog; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE current_node_config_changelog (
    remote_address inet NOT NULL,
    node_type text NOT NULL,
    whence timestamp with time zone NOT NULL,
    config xml NOT NULL
);


ALTER TABLE stratcon.current_node_config_changelog OWNER TO reconnoiter;

--
-- Name: loading_dock_check_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_check_s (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp with time zone NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL
);


ALTER TABLE stratcon.loading_dock_check_s OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_numeric_archive; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_numeric_archive (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value numeric
);


ALTER TABLE stratcon.loading_dock_metric_numeric_archive OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_numeric_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_numeric_s (
    sid integer,
    whence timestamp with time zone,
    name text,
    value numeric
)
INHERITS (loading_dock_metric_numeric_archive);


ALTER TABLE stratcon.loading_dock_metric_numeric_s OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_numeric_s_old; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_numeric_s_old (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value numeric
);


ALTER TABLE stratcon.loading_dock_metric_numeric_s_old OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_text_archive; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_text_archive (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


ALTER TABLE stratcon.loading_dock_metric_text_archive OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_text_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_text_s (
    sid integer,
    whence timestamp with time zone,
    name text,
    value text
)
INHERITS (loading_dock_metric_text_archive);


ALTER TABLE stratcon.loading_dock_metric_text_s OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_text_s_change_log; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_text_s_change_log (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


ALTER TABLE stratcon.loading_dock_metric_text_s_change_log OWNER TO reconnoiter;

--
-- Name: loading_dock_status_archive; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_status_archive (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text
);


ALTER TABLE stratcon.loading_dock_status_archive OWNER TO reconnoiter;

--
-- Name: loading_dock_status_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_status_s (
    sid integer,
    whence timestamp with time zone,
    state character(1),
    availability character(1),
    duration integer,
    status text
)
INHERITS (loading_dock_status_archive);


ALTER TABLE stratcon.loading_dock_status_s OWNER TO reconnoiter;

--
-- Name: loading_dock_status_s_change_log; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_status_s_change_log (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text
);


ALTER TABLE stratcon.loading_dock_status_s_change_log OWNER TO reconnoiter;

--
-- Name: log_whence_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE log_whence_s (
    whence timestamp with time zone NOT NULL,
    "interval" character varying(20) NOT NULL
);


ALTER TABLE stratcon.log_whence_s OWNER TO reconnoiter;

--
-- Name: map_uuid_to_sid; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE map_uuid_to_sid (
    id uuid NOT NULL,
    sid integer NOT NULL
);


ALTER TABLE stratcon.map_uuid_to_sid OWNER TO reconnoiter;

--
-- Name: metric_name_summary; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_name_summary (
    sid integer NOT NULL,
    metric_name text NOT NULL,
    metric_type character varying(22),
    active boolean DEFAULT true,
    ts_search_all tsvector
);


ALTER TABLE stratcon.metric_name_summary OWNER TO reconnoiter;

--
-- Name: metric_tags; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_tags (
    sid integer NOT NULL,
    metric_name text NOT NULL,
    tags_array text[]
);


ALTER TABLE stratcon.metric_tags OWNER TO reconnoiter;

--
-- Name: mv_loading_dock_check_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE mv_loading_dock_check_s (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp with time zone NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL
);


ALTER TABLE stratcon.mv_loading_dock_check_s OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_12hours; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE rollup_matrix_numeric_12hours (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    counter_dev numeric,
    CONSTRAINT rollup_matrix_numeric_12hours_rollup_time_check CHECK (((date_part('hour'::text, timezone('UTC'::text, rollup_time)) = ANY (ARRAY[(0)::double precision, (12)::double precision])) AND (date_part('minute'::text, timezone('utc'::text, rollup_time)) = (0)::double precision)))
);


ALTER TABLE stratcon.rollup_matrix_numeric_12hours OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_20m; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE rollup_matrix_numeric_20m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    counter_dev numeric
);


ALTER TABLE stratcon.rollup_matrix_numeric_20m OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_5m; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE rollup_matrix_numeric_5m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    counter_dev numeric
);


ALTER TABLE stratcon.rollup_matrix_numeric_5m OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_60m; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE rollup_matrix_numeric_60m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    counter_dev numeric
);


ALTER TABLE stratcon.rollup_matrix_numeric_60m OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_6hours; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE rollup_matrix_numeric_6hours (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    counter_dev numeric,
    CONSTRAINT rollup_matrix_numeric_6hours_rollup_time_check CHECK (((date_part('hour'::text, timezone('UTC'::text, rollup_time)) = ANY (ARRAY[(0)::double precision, (6)::double precision, (12)::double precision, (18)::double precision])) AND (date_part('minute'::text, timezone('utc'::text, rollup_time)) = (0)::double precision)))
);


ALTER TABLE stratcon.rollup_matrix_numeric_6hours OWNER TO reconnoiter;

--
-- Name: rollup_runner; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE rollup_runner (
    rollup_table character varying(100),
    runner character varying(22)
);


ALTER TABLE stratcon.rollup_runner OWNER TO reconnoiter;

--
-- Name: tasklocks; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE tasklocks (
    id integer NOT NULL,
    name text NOT NULL
);


ALTER TABLE stratcon.tasklocks OWNER TO reconnoiter;

SET search_path = prism, pg_catalog;

--
-- Name: add_graph_tags(uuid, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION add_graph_tags(in_graphid uuid, in_tags text) RETURNS void
    AS $$
  DECLARE
   v_graphid uuid;
   v_graph_tags text[];
   new_tags_array text[];
   BEGIN
       SELECT graphid,graph_tags into v_graphid,v_graph_tags
         FROM prism.saved_graphs 
           WHERE graphid =in_graphid; 
     IF NOT FOUND THEN
                 RAISE EXCEPTION 'GraphID does not exist in saved graphs table.';
            ELSE 
             new_tags_array:= array_append(v_graph_tags, in_tags);
            UPDATE  prism.saved_graphs SET graph_tags = new_tags_array WHERE graphid=in_graphid;          
      END IF;
    RETURN;
  END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.add_graph_tags(in_graphid uuid, in_tags text) OWNER TO reconnoiter;

--
-- Name: add_tags(integer, text, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION add_tags(in_sid integer, in_metric_name text, in_tags text) RETURNS void
    AS $$
DECLARE
v_sid integer;
v_metric_name text;
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
 BEGIN
     v_tags_array:= string_to_array(in_tags,'');
     SELECT sid into p_sid
      FROM stratcon.metric_tags
      WHERE sid=in_sid AND metric_name=in_metric_name;
     IF NOT FOUND THEN
          SELECT sid,metric_name INTO v_sid, v_metric_name
             FROM stratcon.metric_name_summary
             WHERE sid=in_sid AND metric_name=in_metric_name;
          IF NOT FOUND THEN
               RAISE EXCEPTION 'Metric does not exist in metric_name_summary table';
          ELSE
         INSERT INTO stratcon.metric_tags (sid,metric_name,tags_array) values(v_sid, v_metric_name,v_tags_array);
      END IF;
     ELSE
       SELECT tags_array INTO p_tags_array
          FROM stratcon.metric_tags
          WHERE sid=in_sid AND metric_name=in_metric_name;
             new_tags_array:= array_append(p_tags_array, in_tags);
           UPDATE  stratcon.metric_tags SET tags_array= new_tags_array WHERE sid=in_sid AND metric_name=in_metric_name;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.add_tags(in_sid integer, in_metric_name text, in_tags text) OWNER TO reconnoiter;

--
-- Name: add_tags(integer, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION add_tags(in_sid integer, in_tags text) RETURNS void
    AS $$
DECLARE
v_sid integer;
v_metric_name text;
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
 BEGIN
     v_tags_array:= string_to_array(in_tags,'');
     SELECT sid into p_sid
      FROM stratcon.check_tags
      WHERE sid=in_sid;
     IF NOT FOUND THEN
       INSERT INTO stratcon.check_tags (sid,tags_array) values(in_sid, v_tags_array);
     ELSE
       SELECT tags_array INTO p_tags_array
          FROM stratcon.check_tags
          WHERE sid=in_sid;
             new_tags_array:= array_append(p_tags_array, in_tags);
           UPDATE  stratcon.check_tags SET tags_array= new_tags_array WHERE sid=in_sid;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.add_tags(in_sid integer, in_tags text) OWNER TO reconnoiter;

--
-- Name: check_name_saved_graphs(); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION check_name_saved_graphs() RETURNS trigger
    AS $$
DECLARE
BEGIN
    IF  NEW.saved IS true AND NEW.title IS null THEN
    RAISE EXCEPTION 'You must name graph to save.';
    END IF;
 RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.check_name_saved_graphs() OWNER TO reconnoiter;

--
-- Name: remove_graph_tags(uuid, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION remove_graph_tags(in_graphid uuid, in_tags text) RETURNS void
    AS $$
DECLARE
v_graphid uuid;
v_graph_tags text[];
new_tags_array text[];
i int;
 BEGIN
    SELECT graphid,graph_tags into v_graphid,v_graph_tags
            FROM prism.saved_graphs 
              WHERE graphid =in_graphid; 
     IF NOT FOUND THEN
                    RAISE EXCEPTION 'GraphID does not exist in saved graphs table.';
     ELSE 
        FOR i IN array_lower(v_graph_tags, 1)..array_upper(v_graph_tags, 1) LOOP
           IF NOT v_graph_tags[i] =any(v_graph_tags) THEN
              new_tags_array = array_append(new_tags_array, v_graph_tags[i]);
           END IF;
        END LOOP;
        UPDATE  prism.saved_graphs SET graph_tags = new_tags_array WHERE graphid=in_graphid;           
     END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.remove_graph_tags(in_graphid uuid, in_tags text) OWNER TO reconnoiter;

--
-- Name: remove_tags(integer, text, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION remove_tags(in_sid integer, in_metric_name text, in_tags text) RETURNS void
    AS $$
DECLARE
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
i int;
 BEGIN
   v_tags_array:= string_to_array(in_tags,'');
     SELECT sid,tags_array into p_sid ,p_tags_array
      FROM stratcon.metric_tags
      WHERE sid=in_sid AND metric_name=in_metric_name;
     IF NOT FOUND THEN

               RAISE EXCEPTION 'Metric tags does not found to be removed';

     ELSE
         FOR i IN array_lower(p_tags_array, 1)..array_upper(p_tags_array, 1) LOOP
         IF NOT p_tags_array[i] =any(v_tags_array) THEN
            new_tags_array = array_append(new_tags_array, p_tags_array[i]);
          END IF;
         END LOOP;

           UPDATE  stratcon.metric_tags SET tags_array= new_tags_array WHERE sid=in_sid AND metric_name=in_metric_name;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.remove_tags(in_sid integer, in_metric_name text, in_tags text) OWNER TO reconnoiter;

--
-- Name: remove_tags(integer, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION remove_tags(in_sid integer, in_tags text) RETURNS void
    AS $$
DECLARE
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
i int;
 BEGIN
   v_tags_array:= string_to_array(in_tags,'');
     SELECT sid,tags_array into p_sid ,p_tags_array
      FROM stratcon.check_tags
      WHERE sid=in_sid;
     IF NOT FOUND THEN

               RAISE EXCEPTION 'Check tags does not found to be removed';

     ELSE
         FOR i IN array_lower(p_tags_array, 1)..array_upper(p_tags_array, 1) LOOP
         IF NOT p_tags_array[i] =any(v_tags_array) THEN
            new_tags_array = array_append(new_tags_array, p_tags_array[i]);
          END IF;
         END LOOP;

           UPDATE  stratcon.check_tags SET tags_array= new_tags_array WHERE sid=in_sid;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.remove_tags(in_sid integer, in_tags text) OWNER TO reconnoiter;

--
-- Name: saved_graphs_tsvector(uuid); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION saved_graphs_tsvector(in_graphid uuid) RETURNS tsvector
    AS $$DECLARE
ref_graphid uuid;
ref_graph_tags text;
ref_title text;
v_ts_search_all tsvector;
BEGIN
   SELECT graphid,COALESCE(array_to_string(graph_tags, ' '), ' '),title into ref_graphid,ref_graph_tags,ref_title
               FROM prism.saved_graphs 
              WHERE graphid =in_graphid;
    IF NOT FOUND THEN
        RETURN NULL;
    END IF;
    
    ref_title := coalesce(replace(ref_title, '.', ' '), ' ');
    ref_graph_tags := regexp_replace(ref_graph_tags, E'[_\`/.\\134]', ' ', 'g');
    
    v_ts_search_all=to_tsvector(ref_title || ' ' ||ref_graph_tags);
    
    RETURN v_ts_search_all;
END$$
    LANGUAGE plpgsql STRICT;


ALTER FUNCTION prism.saved_graphs_tsvector(in_graphid uuid) OWNER TO reconnoiter;

--
-- Name: trig_update_tsvector_saved_graphs(); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION trig_update_tsvector_saved_graphs() RETURNS trigger
    AS $$
DECLARE
ref_title text;
 BEGIN
 IF TG_OP = 'UPDATE' THEN
              NEW.ts_search_all=prism.saved_graphs_tsvector(NEW.graphid); 
  ELSE
      ref_title:=coalesce(replace(NEW.title, '.', ' '), ' ');
     NEW.ts_search_all =to_tsvector(ref_title); 
  END IF;  
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.trig_update_tsvector_saved_graphs() OWNER TO reconnoiter;

SET search_path = stratcon, pg_catalog;

--
-- Name: archive_part_maint(text); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION archive_part_maint(in_parent_table text) RETURNS void
    AS $_$
DECLARE
    v_parent_table text;
    v_recent_part date;
BEGIN
  v_parent_table := substring(in_parent_table from E'\\.(.+)');
  IF v_parent_table IS NULL THEN
    v_parent_table := in_parent_table;
  END IF;
  -- we want the "next" month based on the latest current partition
    select ((substring(relname, '_([0-9]{4})') || '-' ||
             substring(relname, '[0-9]{2}$') || '-01')::date + '1 month'::interval)::date
      into v_recent_part
      from pg_inherits
      join pg_class on (pg_class.oid = pg_inherits.inhrelid)
     where inhparent in (select oid
                           from pg_class
                          where relname = v_parent_table)
       and substring(relname, '_([0-9]{6})$') is not null
  order by relname desc
     limit 1;
    IF v_recent_part IS NULL THEN
        select current_date into v_recent_part;
    END IF;
    perform stratcon.archive_part_maint(in_parent_table, v_recent_part);
END
$_$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.archive_part_maint(in_parent_table text) OWNER TO reconnoiter;

--
-- Name: archive_part_maint(text, date); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION archive_part_maint(in_parent_table text, in_start date) RETURNS void
    AS $$
DECLARE
    v_recent_part date;
    v_schema_name text;
    v_table_name text;
    v_constraint_name text;
    v_next_part date;
    v_parent_table text;
    v_rec record;
    v_sql text;
    v_has_perms boolean;
BEGIN
  select (in_start - '1 month'::interval)::date into v_recent_part;
  v_parent_table := substring(in_parent_table from E'\\.(.+)');
  IF v_parent_table IS NULL THEN
    v_parent_table := in_parent_table;
  END IF;
  v_schema_name := substring(in_parent_table from E'^([^.]+)');
  IF v_schema_name IS NULL THEN
    v_schema_name := 'stratcon';
  END IF;

    select date_trunc('month', v_recent_part + '1 month'::interval)::date into v_next_part;

    LOOP
        IF v_next_part > current_date + '1 month'::interval THEN
            EXIT;
        END IF;
        v_table_name := v_parent_table || '_' || extract(YEAR from v_next_part) || 
                        lpad(extract(month from v_next_part)::text, 2, '0');
        v_constraint_name := 'check_' || v_table_name;

        execute 'CREATE TABLE ' || v_schema_name || '.' || v_table_name || '(' ||
                'CONSTRAINT ' || v_constraint_name ||
                E' CHECK (whence >= \'' || v_next_part::text || E' 00:00:00-00\'::timestamptz AND ' ||
                E'        whence < \'' || (v_next_part + '1 month'::interval)::date::text || E' 00:00:00-00\'::timestamptz)' ||
                ') INHERITS (' || in_parent_table || ')';

        RAISE INFO 'created partition %', v_table_name;

        FOR v_rec in
            select replace(indexdef, v_parent_table, v_table_name) as sql
              from pg_indexes
             where tablename = v_parent_table and schemaname = v_schema_name
        LOOP
          RAISE INFO 'recreated parent indexes on %', v_table_name;
          execute v_rec.sql;
        END LOOP;

        -- no public access
        select count(*) > 0 into v_has_perms
          from information_schema.table_privileges
         where table_schema=v_schema_name and table_name=v_parent_table;

        IF v_has_perms THEN
          execute 'REVOKE ALL ON ' || v_schema_name || '.' || v_table_name || ' FROM PUBLIC';
        END IF;

        FOR v_rec in
            select 'GRANT ' || privilege_type || ' ON ' || v_schema_name || '.' || v_table_name ||
                   ' TO ' || grantee as sql
              from information_schema.table_privileges
             where table_schema=v_schema_name and table_name=v_parent_table
        LOOP
          execute v_rec.sql;
        END LOOP;

        FOR v_rec in
            select tgname, tgtype, nspname, proname
              from pg_class as c join pg_trigger as t on(c.oid = t.tgrelid)
              join pg_proc as p on(t.tgfoid = p.oid)
              join pg_namespace as n on(p.pronamespace = n.oid) 
             where relname = v_parent_table
               and proname <> 'parent_empty' LOOP
          v_sql := 'CREATE TRIGGER ' || v_rec.tgname || '';
          IF 0 != (v_rec.tgtype & 2) THEN
            v_sql := v_sql || ' BEFORE ';
          ELSE
            v_sql := v_sql || ' AFTER ';
          END IF;
          IF 0 != (v_rec.tgtype & 4) THEN
            v_sql := v_sql || ' INSERT ';
          END IF;
          IF 0 != (v_rec.tgtype & 8) THEN
            IF 0 != (v_rec.tgtype & 4) THEN
              v_sql := v_sql || ' OR ';
            END IF;
            v_sql := v_sql || ' DELETE ';
          END IF;
          IF 0 != (v_rec.tgtype & 16) THEN
            IF 0 != (v_rec.tgtype & 12) THEN
              v_sql := v_sql || ' OR ';
            END IF;
            v_sql := v_sql || ' UPDATE ';
          END IF;
          v_sql := v_sql || ' ON ' || v_schema_name || '.' || v_table_name;
          IF 0 != (v_rec.tgtype & 1) THEN
            v_sql := v_sql || ' FOR EACH ROW ';
          ELSE
            v_sql := v_sql || ' FOR EACH STATEMENT ';
          END IF;
          v_sql := v_sql || ' EXECUTE PROCEDURE ' || v_rec.nspname || '.' || v_rec.proname || '()';
          execute v_sql;
        END LOOP;

        v_next_part := (v_next_part + '1 month'::interval)::date;
    END LOOP;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.archive_part_maint(in_parent_table text, in_start date) OWNER TO reconnoiter;

--
-- Name: choose_window(timestamp without time zone, timestamp without time zone, integer); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION choose_window(in_start_time timestamp without time zone, in_end_time timestamp without time zone, in_hopeful_nperiods integer, OUT tablename text, OUT period interval, OUT nperiods integer) RETURNS SETOF record
    AS $$
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
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.choose_window(in_start_time timestamp without time zone, in_end_time timestamp without time zone, in_hopeful_nperiods integer, OUT tablename text, OUT period interval, OUT nperiods integer) OWNER TO reconnoiter;

--
-- Name: choose_window(timestamp with time zone, timestamp with time zone, integer); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION choose_window(in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, OUT tablename text, OUT period interval, OUT nperiods integer) RETURNS SETOF record
    AS $$
declare
  window record;
begin
  -- Figure out which table we should be looking in
  for window in
    select atablename, aperiod, anperiods
    from (select aperiod, round(iv/isec) ::integer as anperiods, atablename,
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
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.choose_window(in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, OUT tablename text, OUT period interval, OUT nperiods integer) OWNER TO reconnoiter;

--
-- Name: date_hour(timestamp with time zone); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION date_hour(timestamp with time zone) RETURNS timestamp with time zone
    AS $_$
 SELECT date_trunc('hour',$1);
$_$
    LANGUAGE sql IMMUTABLE STRICT;


ALTER FUNCTION stratcon.date_hour(timestamp with time zone) OWNER TO reconnoiter;

--
-- Name: director_loading_dock_metric_numeric_s(); Type: FUNCTION; Schema: stratcon; Owner: postgres
--

CREATE FUNCTION director_loading_dock_metric_numeric_s() RETURNS trigger
    AS $$
begin
return new;
end
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.director_loading_dock_metric_numeric_s() OWNER TO postgres;

--
-- Name: fetch_dataset(uuid, text, timestamp with time zone, timestamp with time zone, integer, boolean); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION fetch_dataset(in_uuid uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) RETURNS SETOF rollup_matrix_numeric_5m
    AS $$declare
  v_sid int;
  v_record stratcon.rollup_matrix_numeric_5m%rowtype;
begin
  select sid into v_sid from stratcon.map_uuid_to_sid where id = in_uuid;
  if not found then
    return;
  end if;

    for v_record in  select sid, name, rollup_time, count_rows, avg_value, counter_dev from stratcon.fetch_dataset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods, derive) loop
    return next v_record; 
    end loop;

--  return query select sid, name, rollup_time, count_rows, avg_value from stratcon.fetch_dataset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods, derive);
  return;
end
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.fetch_dataset(in_uuid uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) OWNER TO reconnoiter;

--
-- Name: fetch_dataset(integer, text, timestamp with time zone, timestamp with time zone, integer, boolean); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION fetch_dataset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) RETURNS SETOF rollup_matrix_numeric_5m
    AS $$declare
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
  v_sid := in_sid;

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
           's.rollup_time, d.count_rows, d.avg_value, d.counter_dev ' ||
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
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.fetch_dataset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) OWNER TO reconnoiter;

--
-- Name: fetch_varset(uuid, text, timestamp with time zone, timestamp with time zone, integer); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION fetch_varset(in_check uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer) RETURNS SETOF loading_dock_metric_text_s_change_log
    AS $$
declare
  v_sid int;
begin
  -- Map out uuid to an sid.
  select sid into v_sid from stratcon.map_uuid_to_sid where id = in_check;
  if not found then
    return;
  end if;

  return query select * from stratcon.fetch_varset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods);
end
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.fetch_varset(in_check uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer) OWNER TO reconnoiter;

--
-- Name: fetch_varset(integer, text, timestamp with time zone, timestamp with time zone, integer); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION fetch_varset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer) RETURNS SETOF loading_dock_metric_text_s_change_log
    AS $$declare
  v_sid int;
  v_target record;
  v_start_adj timestamptz;
  v_start_text text;
  v_next_text text;
  v_end_adj timestamptz;
  v_change_row stratcon.loading_dock_metric_text_s_change_log%rowtype;
begin
  -- Map out uuid to an sid.
  v_sid := in_sid;

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


  if v_next_text is null then
    -- No rows.
    for v_change_row in
      select v_sid as sid, v_start_adj as whence, in_name as name, v_start_text as value
    loop
      return next v_change_row;
    end loop;
  end if;

  return;
end
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.fetch_varset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer) OWNER TO reconnoiter;

--
-- Name: generate_sid_from_id(uuid); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION generate_sid_from_id(v_in_id uuid) RETURNS integer
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
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.generate_sid_from_id(v_in_id uuid) OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_numeric_s_whence_log(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION loading_dock_metric_numeric_s_whence_log() RETURNS trigger
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
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.loading_dock_metric_numeric_s_whence_log() OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_text_s_change_log(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION loading_dock_metric_text_s_change_log() RETURNS trigger
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

             SELECT value FROM  stratcon.loading_dock_metric_text_s_change_log WHERE sid = NEW.sid AND name = NEW.name
                 AND WHENCE = (SELECT max(whence) FROM stratcon.loading_dock_metric_text_s_change_log
                                 WHERE WHENCE <> NEW.WHENCE and sid=NEW.sid and name=NEW.name )
                     INTO v_oldvalue;

                    IF v_oldvalue IS DISTINCT FROM NEW.value THEN

                        INSERT INTO stratcon.loading_dock_metric_text_s_change_log (sid,whence,name,value)
                            VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value);
                        DELETE FROM stratcon.current_metric_text
                                WHERE sid = NEW.sid and name = NEW.name;
                        INSERT INTO stratcon.current_metric_text (sid,whence,name,value)
                                VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value);
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


ALTER FUNCTION stratcon.loading_dock_metric_text_s_change_log() OWNER TO reconnoiter;

--
-- Name: loading_dock_status_s_change_log(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION loading_dock_status_s_change_log() RETURNS trigger
    AS $$
DECLARE
    v_state CHAR(1);
    v_avail CHAR(1);
    v_whence timestamp with time zone;
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT state,availability,whence FROM  stratcon.loading_dock_status_s_change_log WHERE sid = NEW.sid
        AND WHENCE = (SELECT max(whence) FROM stratcon.loading_dock_status_s_change_log
                        WHERE  SID=NEW.sid and  WHENCE <> NEW.whence )
    INTO v_state,v_avail,v_whence;

    IF NEW.whence > v_whence AND 
       (v_state IS DISTINCT FROM NEW.state OR v_avail IS DISTINCT FROM NEW.availability) THEN

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


ALTER FUNCTION stratcon.loading_dock_status_s_change_log() OWNER TO reconnoiter;

--
-- Name: metric_name_summary_tsvector(integer, text, text); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION metric_name_summary_tsvector(in_sid integer, in_metric_name text, in_metric_type text) RETURNS tsvector
    AS $$DECLARE
ref_sid integer;
ref_module text;
ref_name text;
ref_target text;
ref_tags text;
ref_ctags text;
ref_hostname text;
ref_metric_name text;
ref_alias text;
v_ts_search_all tsvector;
BEGIN
    SELECT sid,module,name,target
      INTO ref_sid,ref_module,ref_name,ref_target
      FROM stratcon.mv_loading_dock_check_s where sid=in_sid;
    IF NOT FOUND THEN
        RETURN NULL;
    END IF;

    SELECT COALESCE(array_to_string(tags_array, ' '), ' ') INTO ref_tags
      FROM stratcon.metric_tags
     WHERE sid=in_sid and metric_name=in_metric_name;
    IF NOT FOUND THEN
        ref_tags:=' ';
    END IF;

    SELECT COALESCE(array_to_string(tags_array, ' '), ' ') INTO ref_ctags
      FROM stratcon.check_tags
     WHERE sid=in_sid;
    IF NOT FOUND THEN
        ref_ctags:=' ';
    END IF;

    SELECT value INTO ref_hostname
      FROM stratcon.current_metric_text mt
      JOIN stratcon.mv_loading_dock_check_s s USING(sid)
     WHERE module='dns' AND s.name='in-addr.arpa' AND target = ref_target;

    SELECT mt.value INTO ref_alias
      FROM stratcon.current_metric_text mt
      JOIN stratcon.mv_loading_dock_check_s s USING(sid)
     WHERE s.module='snmp' AND mt.name='alias' AND s.sid=in_sid;

    ref_hostname := coalesce(replace(ref_hostname, '.', ' '), ' ');
    ref_metric_name := regexp_replace(in_metric_name, E'[_\`/.\\134]', ' ', 'g');
    ref_alias := coalesce(regexp_replace(ref_alias, E'[_\`/.\\134]', ' ', 'g'), ' ');

    v_ts_search_all=to_tsvector(ref_metric_name || ' ' ||
                                ref_module || ' ' ||
                                ref_name || ' ' ||
                                ref_target || ' ' ||
                                ref_hostname || ' ' ||
                                ref_alias || ' ' ||
                                ref_tags || ' ' ||
                                ref_ctags);
    RETURN v_ts_search_all;
END$$
    LANGUAGE plpgsql STRICT;


ALTER FUNCTION stratcon.metric_name_summary_tsvector(in_sid integer, in_metric_name text, in_metric_type text) OWNER TO reconnoiter;

--
-- Name: mv_loading_dock_check_s(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION mv_loading_dock_check_s() RETURNS trigger
    AS $$
DECLARE
    v_remote_address INET;
    v_target TEXT;
    v_module TEXT;
    v_name TEXT;
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT remote_address,target,module,name FROM  stratcon.mv_loading_dock_check_s WHERE sid = NEW.sid AND id=NEW.id 
        INTO v_remote_address,v_target,v_module,v_name;

    IF v_remote_address IS DISTINCT FROM NEW.remote_address OR v_target IS DISTINCT FROM NEW.target OR v_name IS DISTINCT FROM NEW.name   THEN
        
        DELETE from stratcon.mv_loading_dock_check_s WHERE sid = NEW.sid AND id=NEW.id;
        
        INSERT INTO stratcon.mv_loading_dock_check_s (sid,remote_address,whence,id,target,module,name)
            VALUES (NEW.sid,NEW.remote_address,NEW.whence,NEW.id,NEW.target,NEW.module,NEW.name); 

    END IF;

ELSE
        RAISE EXCEPTION 'Something wrong with stratcon.mv_loading_dock_check_s';
END IF;

    RETURN NULL;

END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.mv_loading_dock_check_s() OWNER TO reconnoiter;

--
-- Name: parent_empty(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION parent_empty() RETURNS trigger
    AS $$
BEGIN
    RAISE EXCEPTION 'Cannot insert into parent table';
    RETURN NULL;
END;
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.parent_empty() OWNER TO reconnoiter;

--
-- Name: remove_metric(uuid, text, text); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION remove_metric(in_uuid uuid, in_metric_name text, v_debug text, OUT v_out text) RETURNS text
    AS $$
DECLARE
v_del_sid INT;
v_del_metric_name TEXT;
v_del_metric_type TEXT;
deleted_t INT;
deleted_tc INT;
deleted_ct INT;
deleted_n INT;
deleted_5 INT;
deleted_20 INT;
deleted_60 INT;
deleted_6h INT;
deleted_12h INT;
deleted_sum INT;
deleted_tags INT;

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
 -- Delete from Metric Tex table 
  DELETE FROM stratcon.loading_dock_metric_text_archive WHERE sid=v_del_sid AND name=v_del_metric_name;
     GET DIAGNOSTICS deleted_t = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
           RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_text_archive : %',deleted_t;
     END IF;
 -- Delete from Metric Change Log table 
  DELETE FROM stratcon.loading_dock_metric_text_s_change_log WHERE sid=v_del_sid AND name=v_del_metric_name;
     GET DIAGNOSTICS deleted_tc = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
          RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_text_s_change_log : %',deleted_tc;
     END IF;
  -- Delete from current_metric_text table 
  DELETE FROM stratcon.current_metric_text WHERE sid=v_del_sid AND name=v_del_metric_name;
     GET DIAGNOSTICS deleted_ct = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
          RAISE NOTICE 'DELELTED ROWS FROM current_metric_text : %',deleted_ct;
     END IF;     
 ELSE
  -- Delete from Metrix Numeric table
   DELETE FROM stratcon.loading_dock_metric_numeric_archive WHERE sid=v_del_sid AND name=v_del_metric_name;
   GET DIAGNOSTICS deleted_n = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
         RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_numeric_archive : %',deleted_n;
     END IF;
  -- Delete from Rollup tables
   DELETE FROM stratcon.rollup_matrix_numeric_5m WHERE sid=v_del_sid AND name=v_del_metric_name;
   GET DIAGNOSTICS deleted_5 = ROW_COUNT;   
     IF v_debug = 'DEBUG' THEN
         RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_5m : %',deleted_5;
     END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_20m WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_20= ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_20m : %',deleted_20;
        END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_60m WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_60 = ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_60m : %',deleted_60;
        END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_6hours WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_6h = ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_6hours : %',deleted_6h;
        END IF;
   DELETE FROM stratcon.rollup_matrix_numeric_12hours WHERE sid=v_del_sid AND name=v_del_metric_name;
      GET DIAGNOSTICS deleted_12h = ROW_COUNT;      
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM rollup_matrix_numeric_12hours : %',deleted_12h;
        END IF;
END IF;
  -- Delete from metric summary table
   DELETE FROM stratcon.metric_name_summary WHERE sid=v_del_sid AND metric_name=v_del_metric_name;
      GET DIAGNOSTICS deleted_sum= ROW_COUNT;     
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM metric_name_summary : %',deleted_sum;
        END IF; 
  
  -- Delete from metric tags table
   DELETE FROM stratcon.metric_tags WHERE sid=v_del_sid AND metric_name=v_del_metric_name;
      GET DIAGNOSTICS deleted_tags= ROW_COUNT;     
        IF v_debug = 'DEBUG' THEN
            RAISE NOTICE 'DELELTED ROWS FROM metric_tags : %',deleted_tags;
        END IF; 
        
 v_out:='Deleted Rows for Metric_Text, Metric_Text_change_log,Metric_Numeric,Rollup_5m,Rollup_20m,Rollup_1hour,Rollup_6hours,Rollup_12hours,Metric_Summary:'||deleted_t||','||deleted_tc||','||deleted_n||','||deleted_5||','||deleted_20||','||deleted_60||','||deleted_6h||','||deleted_12h||','||deleted_sum;
 
   EXCEPTION
    WHEN RAISE_EXCEPTION THEN
            RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
            RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.remove_metric(in_uuid uuid, in_metric_name text, v_debug text, OUT v_out text) OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_12hours(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION rollup_matrix_numeric_12hours() RETURNS void
    AS $$DECLARE
  rec stratcon.rollup_matrix_numeric_12hours%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_12 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
  v_nrunning INT;
  v_taskid int;
  v_locked boolean;
  whenceint RECORD; 
 
BEGIN

  SELECT id into v_taskid from stratcon.tasklocks where name = 'rollup_matrix_numeric_12h';
  IF v_taskid IS NULL THEN
    INSERT INTO stratcon.tasklocks (id, name) VALUES (DEFAULT, 'rollup_matrix_numeric_12h')
      RETURNING id into v_taskid;
  END IF;

  select pg_try_advisory_lock(43191, v_taskid) into v_locked;

  IF v_locked = false THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_12h already running';
    RETURN ;
  END IF;

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
                SELECT sid,name,v_min_whence as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
                       (SUM(counter_dev*count_rows)/SUM(count_rows)) as counter_dev
         FROM stratcon.rollup_matrix_numeric_6hours
           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'12 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_12hours
          (sid,name,rollup_time,count_rows,avg_value,counter_dev) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.counter_dev);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='12 hours';

v_min_whence := NULL;
v_max_rollup_12 := NULL;

END LOOP;

perform pg_advisory_unlock(43191, v_taskid);

RETURN;
EXCEPTION
    WHEN RAISE_EXCEPTION THEN
      perform pg_advisory_unlock(43191, v_taskid);
      RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
      perform pg_advisory_unlock(43191, v_taskid);
      RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.rollup_matrix_numeric_12hours() OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_20m(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION rollup_matrix_numeric_20m() RETURNS void
    AS $$DECLARE
 
 rec stratcon.rollup_matrix_numeric_20m%rowtype;
 v_sql TEXT;
 v_min_whence TIMESTAMPTZ;
 v_max_rollup_20 TIMESTAMPTZ;
 v_whence TIMESTAMPTZ;
 rows INT;
 v_nrunning INT;
 whenceint RECORD;
 v_taskid int;
 v_locked boolean;
BEGIN

  SELECT id into v_taskid from stratcon.tasklocks where name = 'rollup_matrix_numeric_20m';
  IF v_taskid IS NULL THEN
    INSERT INTO stratcon.tasklocks (id, name) VALUES (DEFAULT, 'rollup_matrix_numeric_20m')
      RETURNING id into v_taskid;
  END IF;

  select pg_try_advisory_lock(43191, v_taskid) into v_locked;

  IF v_locked = false THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_20m already running';
    RETURN ;
  END IF;

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
                       SUM(1) as count_rows ,(SUM(avg_value*1)/SUM(1)) as avg_value,
                       (SUM(counter_dev*1)/SUM(1)) as counter_dev
       FROM stratcon.rollup_matrix_numeric_5m
                      WHERE rollup_time<= v_min_whence AND rollup_time > v_min_whence -'20 minutes'::interval
                GROUP BY sid,name
 
       LOOP
    
        
        INSERT INTO stratcon.rollup_matrix_numeric_20m
         (sid,name,rollup_time,count_rows,avg_value,counter_dev) VALUES 
         (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.counter_dev);
        
   END LOOP;

  -- Delete from whence log table
  
  DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='20 minutes';
 
  v_min_whence:= NULL;
  v_max_rollup_20:= NULL;

 END LOOP;

  perform pg_advisory_unlock(43191, v_taskid);
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       perform pg_advisory_unlock(43191, v_taskid);
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
       perform pg_advisory_unlock(43191, v_taskid);
        RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.rollup_matrix_numeric_20m() OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_5m(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION rollup_matrix_numeric_5m() RETURNS void
    AS $$DECLARE
 
 rec stratcon.rollup_matrix_numeric_5m%rowtype;
 v_sql TEXT;
 v_min_whence TIMESTAMPTZ;
 v_max_rollup_5 TIMESTAMPTZ;
 v_whence TIMESTAMPTZ;
 rows INT;
 v_nrunning INT;
 v_taskid int;
 v_locked boolean;
 whenceint RECORD;
BEGIN

  SELECT id into v_taskid from stratcon.tasklocks where name = 'rollup_matrix_numeric_5m';
  IF v_taskid IS NULL THEN
    INSERT INTO stratcon.tasklocks (id, name) VALUES (DEFAULT, 'rollup_matrix_numeric_5m')
      RETURNING id into v_taskid;
  END IF;

  select pg_try_advisory_lock(43191, v_taskid) into v_locked;

  IF v_locked = false THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_5m already running';
    RETURN ;
  END IF;

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

    INSERT INTO stratcon.rollup_matrix_numeric_5m
    SELECT * FROM stratcon.window_robust_derive(v_min_whence);
 
  -- Delete from whence log table
  
  DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='5 minutes';
 
 v_min_whence:= NULL;
 v_max_rollup_5:= NULL;
 
 END LOOP;

 perform pg_advisory_unlock(43191, v_taskid); 
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
         perform pg_advisory_unlock(43191, v_taskid); 
         RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
         perform pg_advisory_unlock(43191, v_taskid); 
         RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.rollup_matrix_numeric_5m() OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_60m(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION rollup_matrix_numeric_60m() RETURNS void
    AS $$DECLARE
  rec stratcon.rollup_matrix_numeric_60m%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_60 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
  v_nrunning INT;
  v_taskid int;
  v_locked boolean;
  whenceint RECORD;
BEGIN

  SELECT id into v_taskid from stratcon.tasklocks where name = 'rollup_matrix_numeric_60m';
  IF v_taskid IS NULL THEN
    INSERT INTO stratcon.tasklocks (id, name) VALUES (DEFAULT, 'rollup_matrix_numeric_60m')
      RETURNING id into v_taskid;
  END IF;

  select pg_try_advisory_lock(43191, v_taskid) into v_locked;

  IF v_locked = false THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_60m already running';
    RETURN ;
  END IF;


FOR whenceint IN SELECT * FROM stratcon.log_whence_s WHERE interval='1 hour' LOOP
           
  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='1 hour'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_60m 
         INTO v_max_rollup_60;    

-- Insert Log for 6 Hour rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('day', v_min_whence::timestamptz at time zone 'utc') + (floor(extract('hour' from v_min_whence::timestamptz at time zone 'utc')/6)*6) * '1 hour'::interval and interval='6 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('day', v_min_whence::timestamptz at time zone 'utc') + (floor(extract('hour' from v_min_whence::timestamptz at time zone 'utc')/6)*6) * '1 hour'::interval,'6 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_60 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_60m 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,date_hour(rollup_time) as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
                       (SUM(counter_dev*count_rows)/SUM(count_rows)) as counter_dev
         FROM stratcon.rollup_matrix_numeric_20m
           WHERE date_hour(rollup_time)= v_min_whence
                   GROUP BY date_hour(rollup_time),sid,name
        LOOP
      
          INSERT INTO stratcon.rollup_matrix_numeric_60m
          (sid,name,rollup_time,count_rows,avg_value,counter_dev) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.counter_dev);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='1 hour';

v_min_whence := NULL;
v_max_rollup_60 := NULL;

END LOOP;

    perform pg_advisory_unlock(43191, v_taskid); 

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
        perform pg_advisory_unlock(43191, v_taskid); 
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        perform pg_advisory_unlock(43191, v_taskid); 
        RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.rollup_matrix_numeric_60m() OWNER TO reconnoiter;

--
-- Name: rollup_matrix_numeric_6hours(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION rollup_matrix_numeric_6hours() RETURNS void
    AS $$DECLARE
  rec stratcon.rollup_matrix_numeric_6hours%rowtype;
  v_sql TEXT;
  v_min_whence TIMESTAMPTZ;
  v_max_rollup_6 TIMESTAMPTZ;
  v_whence TIMESTAMPTZ;
  v_nrunning INT;
  v_taskid int;
  v_locked boolean;
  whenceint RECORD;  
BEGIN

  SELECT id into v_taskid from stratcon.tasklocks where name = 'rollup_matrix_numeric_6h';
  IF v_taskid IS NULL THEN
    INSERT INTO stratcon.tasklocks (id, name) VALUES (DEFAULT, 'rollup_matrix_numeric_6h')
      RETURNING id into v_taskid;
  END IF;

  select pg_try_advisory_lock(43191, v_taskid) into v_locked;

  IF v_locked = false THEN
    RAISE NOTICE 'stratcon.rollup_matrix_numeric_6h already running';
    RETURN ;
  END IF;

FOR whenceint IN SELECT * FROM stratcon.log_whence_s WHERE interval='6 hours' LOOP

  SELECT min(whence) FROM stratcon.log_whence_s WHERE interval='6 hours'
         INTO v_min_whence;
         
  SELECT max(date_trunc('H',rollup_time)) FROM  stratcon.rollup_matrix_numeric_6hours 
         INTO v_max_rollup_6;    

-- Insert Log for 12 Hours rollup
   
   SELECT whence FROM stratcon.log_whence_s WHERE whence=date_trunc('day', v_min_whence) + (floor(extract('hour' from v_min_whence)/12)*12) * '1 hour'::interval and interval='12 hours'
           INTO v_whence;
      IF NOT FOUND THEN
       INSERT INTO  stratcon.log_whence_s VALUES(date_trunc('day', v_min_whence::timestamptz at time zone 'utc') + (floor(extract('hour' from v_min_whence::timestamptz at time zone 'utc')/12)*12) * '1 hour'::interval ,'12 hours');
   END IF;
   
   
  IF v_min_whence <= v_max_rollup_6 THEN
  
  DELETE FROM stratcon.rollup_matrix_numeric_6hours 
       WHERE rollup_time= v_min_whence;

  END IF;
  
    FOR rec IN 
                SELECT sid,name,v_min_whence as rollup_time,SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
                       (SUM(counter_dev*count_rows)/SUM(count_rows)) as counter_dev
         FROM stratcon.rollup_matrix_numeric_60m
           WHERE rollup_time<= v_min_whence and rollup_time> v_min_whence-'6 hour'::interval
                   GROUP BY sid,name
        LOOP
      
       
          INSERT INTO stratcon.rollup_matrix_numeric_6hours
          (sid,name,rollup_time,count_rows,avg_value,counter_dev) VALUES
          (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.counter_dev);
          
     END LOOP;


DELETE FROM stratcon.log_whence_s WHERE WHENCE=v_min_whence AND INTERVAL='6 hours';
v_min_whence := NULL;
v_max_rollup_6 := NULL;

END LOOP;

perform pg_advisory_unlock(43191, v_taskid); 

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       perform pg_advisory_unlock(43191, v_taskid);
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
       perform pg_advisory_unlock(43191, v_taskid); 
       RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.rollup_matrix_numeric_6hours() OWNER TO reconnoiter;

--
-- Name: snapshot_maker(text, text); Type: FUNCTION; Schema: stratcon; Owner: postgres
--

CREATE FUNCTION snapshot_maker(v_parent text, v_pattern text) RETURNS void
    AS $_$
DECLARE
    v_interval text;
    v_segment text;
    v_name text;
    v_match text;

    v_checker_sql text;
BEGIN
    IF v_pattern = 'daily' then 
        v_interval := '1 days';  
        v_segment := 'day';  
        v_name := 'YYYY_MMDD'; 
        v_match := '_p[0-9]{4}_[0-9]{4}$';    
    ELSEIF v_pattern = 'weekly' then 
        v_interval = '7 days';  
        v_segment = 'week';  
        v_name = 'YYYY_MMDD'; 
        v_match = '_p[0-9]{4}_[0-9]{4}$';   
    ELSEIF v_pattern = 'monthly' then 
        v_interval = '1 month'; 
        v_segment = 'month';
        v_name = 'YYYY_MM';
        v_match = '_p[0-9]{4}_[0-9]{2}$'; 
    END IF; 

    v_checker_sql := 'SELECT * FROM 
        (SELECT '|| quote_literal(v_parent) || ' ||''_p''|| to_char( date_trunc('||quote_literal(v_segment)||', now() + (n * '||quote_literal(v_interval)||'::interval)), '||quote_literal(v_name)||') as part,
                date_trunc('||quote_literal(v_segment)||', now() + (n * '||quote_literal(v_interval)||'::interval)) ::timestamp without time zone as lower_bound,
                date_trunc('||quote_literal(v_segment)||', now() + ((n+1) * '||quote_literal(v_interval)||'::interval)) ::timestamp without time zone as upper_bound
           FROM generate_series(-3,3) n) a
        WHERE part not in (SELECT tablename from pg_tables WHERE tablename ~ ''^'||v_parent||v_match||''')';

    RAISE NOTICE '%', v_checker_sql;

END
$_$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.snapshot_maker(v_parent text, v_pattern text) OWNER TO postgres;

--
-- Name: test_dataset(integer, text, timestamp with time zone, timestamp with time zone, integer, boolean); Type: FUNCTION; Schema: stratcon; Owner: omnidba
--

CREATE FUNCTION test_dataset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) RETURNS SETOF rollup_matrix_numeric_5m
    AS $$declare
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
  v_sid := in_sid;

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

 RAISE NOTICE 'start_adj: %',v_start_adj;
 
  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_end_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_end_adj;

RAISE NOTICE 'end_adj: %',v_end_adj;

  v_sql := 'select ' || v_sid || ' as sid, ' || quote_literal(in_name) || ' as name, ' ||
           's.rollup_time, d.count_rows, d.avg_value, d.counter_dev ' ||
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

RAISE NOTICE 'v_sql: %',v_sql;

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
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.test_dataset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean) OWNER TO omnidba;

--
-- Name: trig_update_tsvector_from_check_tags(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION trig_update_tsvector_from_check_tags() RETURNS trigger
    AS $$
DECLARE
BEGIN
    UPDATE stratcon.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,metric_name,metric_type)
    where sid=NEW.sid;
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.trig_update_tsvector_from_check_tags() OWNER TO reconnoiter;

--
-- Name: trig_update_tsvector_from_metric_summary(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION trig_update_tsvector_from_metric_summary() RETURNS trigger
    AS $$
DECLARE
 BEGIN
 IF TG_OP != 'INSERT' THEN
   IF (NEW.metric_name <> OLD.metric_name) THEN
           UPDATE stratcon.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,NEW.metric_type)
             where sid=NEW.sid and metric_name=NEW.metric_name and metric_type = NEW.metric_type;
   END IF;    
 ELSE 
    UPDATE stratcon.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,NEW.metric_type)
            where sid=NEW.sid and metric_name=NEW.metric_name and metric_type = NEW.metric_type;
 END IF;  
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.trig_update_tsvector_from_metric_summary() OWNER TO reconnoiter;

--
-- Name: trig_update_tsvector_from_metric_tags(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION trig_update_tsvector_from_metric_tags() RETURNS trigger
    AS $$
DECLARE
BEGIN
    UPDATE stratcon.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,metric_type)
    where sid=NEW.sid and metric_name=NEW.metric_name;
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.trig_update_tsvector_from_metric_tags() OWNER TO reconnoiter;

--
-- Name: trig_update_tsvector_from_mv_dock(); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION trig_update_tsvector_from_mv_dock() RETURNS trigger
    AS $$
DECLARE
BEGIN
    UPDATE stratcon.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(sid, metric_name, metric_type) WHERE sid = NEW.sid;
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.trig_update_tsvector_from_mv_dock() OWNER TO reconnoiter;

--
-- Name: update_config(inet, text, timestamp with time zone, xml); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION update_config(v_remote_address_in inet, v_node_type_in text, v_whence_in timestamp with time zone, v_config_in xml) RETURNS void
    AS $$
DECLARE
    v_config xml;
BEGIN
    select config into v_config from stratcon.current_node_config
     where remote_address = v_remote_address_in
       and node_type = v_node_type_in;
    IF FOUND THEN
        IF v_config::text = v_config_in::text THEN
            RETURN;
        END IF;
        delete from stratcon.current_node_config
              where remote_address = v_remote_address_in
                and node_type = v_node_type_in;
    END IF;
    insert into stratcon.current_node_config
                (remote_address, node_type, whence, config)
         values (v_remote_address_in, v_node_type_in, v_whence_in, v_config_in);
    insert into stratcon.current_node_config_changelog
                (remote_address, node_type, whence, config)
         values (v_remote_address_in, v_node_type_in, v_whence_in, v_config_in);
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.update_config(v_remote_address_in inet, v_node_type_in text, v_whence_in timestamp with time zone, v_config_in xml) OWNER TO reconnoiter;

--
-- Name: window_robust_derive(timestamp with time zone); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION window_robust_derive(in_start_time timestamp with time zone) RETURNS SETOF rollup_matrix_numeric_5m
    AS $$
declare
  rec stratcon.rollup_matrix_numeric_5m%rowtype;
  r record;
  rise numeric;
  last_row_whence timestamp;
  last_value numeric;
  run numeric;
begin

   rec.sid := null;
   rec.name := null;
   rise := 0;
   run := 0;
   rec.rollup_time = in_start_time;
   for r in SELECT sid, name, whence,
                   (whence > in_start_time - '5 minutes'::interval) as in_window,
                   value
              FROM stratcon.loading_dock_metric_numeric_archive
             WHERE whence <= in_start_time
               AND whence > in_start_time - ('5 minutes'::interval * 2)
          order BY sid,name,whence
  loop
  if (rec.sid is not null and rec.name is not null) and
     (rec.sid <> r.sid or rec.name <> r.name) then
     if rec.count_rows > 0 then
       rec.avg_value := rec.avg_value / rec.count_rows;
       if run is not null and run > 0 then
         rec.counter_dev := rise/run;
       end if;
       return next rec;
     end if;
     rec.avg_value := 0;
     rec.count_rows := 0;
     rec.counter_dev := null;
     rise := 0;
     run := 0;
     last_value := null;
     last_row_whence := null;
  end if;
  rec.sid := r.sid;
  rec.name := r.name;
  if r.in_window then
    if r.value is not null then
      rec.count_rows := rec.count_rows + 1;
      rec.avg_value := rec.avg_value + coalesce(r.value,0);
      if     last_row_whence is not null
         and last_value is not null
         and last_value <= r.value then
        rise := rise + (r.value - last_value);
        run := run + ((extract(epoch from r.whence) +
                       (extract(milliseconds from r.whence)::integer % 1000)/1000.0) -
                      (extract(epoch from last_row_whence) +
                       (extract(milliseconds from last_row_whence)::integer % 1000)/1000.0));
      end if;
    end if;
  end if;
  if r.value is not null then
    last_row_whence := r.whence;
    last_value := r.value;
  end if;
end loop;
return;
end;
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.window_robust_derive(in_start_time timestamp with time zone) OWNER TO reconnoiter;

--
-- Name: seq_sid; Type: SEQUENCE; Schema: stratcon; Owner: reconnoiter
--

CREATE SEQUENCE seq_sid
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;


ALTER TABLE stratcon.seq_sid OWNER TO reconnoiter;

--
-- Name: tasklocks_id_seq; Type: SEQUENCE; Schema: stratcon; Owner: reconnoiter
--

CREATE SEQUENCE tasklocks_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;


ALTER TABLE stratcon.tasklocks_id_seq OWNER TO reconnoiter;

--
-- Name: tasklocks_id_seq; Type: SEQUENCE OWNED BY; Schema: stratcon; Owner: reconnoiter
--

ALTER SEQUENCE tasklocks_id_seq OWNED BY tasklocks.id;


--
-- Name: id; Type: DEFAULT; Schema: stratcon; Owner: reconnoiter
--

ALTER TABLE tasklocks ALTER COLUMN id SET DEFAULT nextval('tasklocks_id_seq'::regclass);


SET search_path = prism, pg_catalog;

--
-- Name: graph_templates_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY graph_templates
    ADD CONSTRAINT graph_templates_pkey PRIMARY KEY (templateid);


--
-- Name: graph_templates_title_key; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY graph_templates
    ADD CONSTRAINT graph_templates_title_key UNIQUE (title);


--
-- Name: saved_graphs_dep_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_pkey PRIMARY KEY (graphid, sid, metric_name);


--
-- Name: saved_graphs_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY saved_graphs
    ADD CONSTRAINT saved_graphs_pkey PRIMARY KEY (graphid);


--
-- Name: saved_worksheets_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY saved_worksheets
    ADD CONSTRAINT saved_worksheets_pkey PRIMARY KEY (sheetid);


--
-- Name: unq_saved_graphs_title; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY saved_graphs
    ADD CONSTRAINT unq_saved_graphs_title UNIQUE (title);


SET search_path = stratcon, pg_catalog;

--
-- Name: check_tags_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY check_tags
    ADD CONSTRAINT check_tags_pkey PRIMARY KEY (sid);


--
-- Name: current_metric_text_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY current_metric_text
    ADD CONSTRAINT current_metric_text_pkey PRIMARY KEY (sid, name);


--
-- Name: current_node_config_changelog_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY current_node_config_changelog
    ADD CONSTRAINT current_node_config_changelog_pkey PRIMARY KEY (remote_address, node_type, whence);


--
-- Name: current_node_config_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY current_node_config
    ADD CONSTRAINT current_node_config_pkey PRIMARY KEY (remote_address, node_type);


--
-- Name: loading_dock_check_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_check_s
    ADD CONSTRAINT loading_dock_check_s_pkey PRIMARY KEY (sid, id, whence);


--
-- Name: loading_dock_metric_numeric_archive_whence_key; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_numeric_archive
    ADD CONSTRAINT loading_dock_metric_numeric_archive_whence_key UNIQUE (whence, sid, name);


--
-- Name: loading_dock_metric_numeric_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_numeric_s
    ADD CONSTRAINT loading_dock_metric_numeric_s_pkey PRIMARY KEY (whence, sid, name);


--
-- Name: loading_dock_metric_numeric_s_pkey_old; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_numeric_s_old
    ADD CONSTRAINT loading_dock_metric_numeric_s_pkey_old PRIMARY KEY (whence, sid, name);

ALTER TABLE loading_dock_metric_numeric_s_old CLUSTER ON loading_dock_metric_numeric_s_pkey_old;


--
-- Name: loading_dock_metric_text_archive_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_text_archive
    ADD CONSTRAINT loading_dock_metric_text_archive_pkey PRIMARY KEY (whence, sid, name);


--
-- Name: loading_dock_metric_text_s_change_log_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_text_s_change_log
    ADD CONSTRAINT loading_dock_metric_text_s_change_log_pkey PRIMARY KEY (whence, sid, name);


--
-- Name: loading_dock_metric_text_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_text_s
    ADD CONSTRAINT loading_dock_metric_text_s_pkey PRIMARY KEY (whence, sid, name);


--
-- Name: loading_dock_status_archive_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_status_archive
    ADD CONSTRAINT loading_dock_status_archive_pkey PRIMARY KEY (whence, sid);


--
-- Name: loading_dock_status_s_change_log_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_status_s_change_log
    ADD CONSTRAINT loading_dock_status_s_change_log_pkey PRIMARY KEY (sid, whence);


--
-- Name: loading_dock_status_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_status_s
    ADD CONSTRAINT loading_dock_status_s_pkey PRIMARY KEY (sid, whence);


--
-- Name: log_whence_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY log_whence_s
    ADD CONSTRAINT log_whence_s_pkey PRIMARY KEY (whence, "interval");


--
-- Name: map_uuid_to_sid_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY map_uuid_to_sid
    ADD CONSTRAINT map_uuid_to_sid_pkey PRIMARY KEY (id);


--
-- Name: metric_name_summary_pk; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_name_summary
    ADD CONSTRAINT metric_name_summary_pk UNIQUE (sid, metric_name, metric_type);


--
-- Name: metric_tags_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_tags
    ADD CONSTRAINT metric_tags_pkey PRIMARY KEY (sid, metric_name);


--
-- Name: mv_loading_dock_check_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY mv_loading_dock_check_s
    ADD CONSTRAINT mv_loading_dock_check_s_pkey PRIMARY KEY (sid);


--
-- Name: rollup_matrix_numeric_12hours_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY rollup_matrix_numeric_12hours
    ADD CONSTRAINT rollup_matrix_numeric_12hours_pkey PRIMARY KEY (rollup_time, sid, name);

ALTER TABLE rollup_matrix_numeric_12hours CLUSTER ON rollup_matrix_numeric_12hours_pkey;


--
-- Name: rollup_matrix_numeric_20m_new_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY rollup_matrix_numeric_20m
    ADD CONSTRAINT rollup_matrix_numeric_20m_new_pkey PRIMARY KEY (rollup_time, sid, name);

ALTER TABLE rollup_matrix_numeric_20m CLUSTER ON rollup_matrix_numeric_20m_new_pkey;


--
-- Name: rollup_matrix_numeric_5m_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY rollup_matrix_numeric_5m
    ADD CONSTRAINT rollup_matrix_numeric_5m_pkey PRIMARY KEY (rollup_time, sid, name);

ALTER TABLE rollup_matrix_numeric_5m CLUSTER ON rollup_matrix_numeric_5m_pkey;


--
-- Name: rollup_matrix_numeric_60m_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY rollup_matrix_numeric_60m
    ADD CONSTRAINT rollup_matrix_numeric_60m_pkey PRIMARY KEY (rollup_time, sid, name);

ALTER TABLE rollup_matrix_numeric_60m CLUSTER ON rollup_matrix_numeric_60m_pkey;


--
-- Name: rollup_matrix_numeric_6hours_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY rollup_matrix_numeric_6hours
    ADD CONSTRAINT rollup_matrix_numeric_6hours_pkey PRIMARY KEY (rollup_time, sid, name);

ALTER TABLE rollup_matrix_numeric_6hours CLUSTER ON rollup_matrix_numeric_6hours_pkey;


--
-- Name: tasklocks_name_key; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY tasklocks
    ADD CONSTRAINT tasklocks_name_key UNIQUE (name);


--
-- Name: tasklocks_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY tasklocks
    ADD CONSTRAINT tasklocks_pkey PRIMARY KEY (id);


SET search_path = prism, pg_catalog;

--
-- Name: idx_saved_graphs_ts_search_all; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_saved_graphs_ts_search_all ON saved_graphs USING btree (ts_search_all);


--
-- Name: saved_graphs_dep_sid_name_type; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX saved_graphs_dep_sid_name_type ON saved_graphs_dep USING btree (sid, metric_name, metric_type);


--
-- Name: unq_saved_graphs_genesis; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX unq_saved_graphs_genesis ON saved_graphs USING btree (genesis);


SET search_path = stratcon, pg_catalog;

--
-- Name: idx_metric_name_summary_ts_search_all; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_metric_name_summary_ts_search_all ON metric_name_summary USING gin (ts_search_all);


--
-- Name: idx_mv_loading_dock_check_s_module; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_mv_loading_dock_check_s_module ON mv_loading_dock_check_s USING btree (module);


--
-- Name: idx_mv_loading_dock_check_s_name; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_mv_loading_dock_check_s_name ON mv_loading_dock_check_s USING btree (name);


--
-- Name: idx_mv_loading_dock_check_s_target; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_mv_loading_dock_check_s_target ON mv_loading_dock_check_s USING btree (target);


--
-- Name: idx_rollup_matrix_numeric_20m_rollup_time; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_rollup_matrix_numeric_20m_rollup_time ON rollup_matrix_numeric_20m USING btree (date_hour(rollup_time));


--
-- Name: map_uuid_to_sid_idx; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE UNIQUE INDEX map_uuid_to_sid_idx ON map_uuid_to_sid USING btree (sid);


--
-- Name: unq_mv_loading_dock_check_s_id; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE UNIQUE INDEX unq_mv_loading_dock_check_s_id ON mv_loading_dock_check_s USING btree (id);


SET search_path = prism, pg_catalog;

--
-- Name: check_name_saved_graphs; Type: TRIGGER; Schema: prism; Owner: reconnoiter
--

CREATE TRIGGER check_name_saved_graphs
    BEFORE INSERT OR UPDATE ON saved_graphs
    FOR EACH ROW
    EXECUTE PROCEDURE check_name_saved_graphs();


--
-- Name: trig_update_tsvector_saved_graphs; Type: TRIGGER; Schema: prism; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_saved_graphs
    BEFORE INSERT OR UPDATE ON saved_graphs
    FOR EACH ROW
    EXECUTE PROCEDURE trig_update_tsvector_saved_graphs();


SET search_path = stratcon, pg_catalog;

--
-- Name: loading_dock_metric_numeric_archive_empty; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_numeric_archive_empty
    BEFORE INSERT OR UPDATE ON loading_dock_metric_numeric_archive
    FOR EACH ROW
    EXECUTE PROCEDURE parent_empty();


--
-- Name: loading_dock_metric_numeric_s_whence_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_numeric_s_whence_log
    AFTER INSERT ON loading_dock_metric_numeric_s_old
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_numeric_s_whence_log();


--
-- Name: loading_dock_metric_numeric_s_whence_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_numeric_s_whence_log
    AFTER INSERT ON loading_dock_metric_numeric_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_numeric_s_whence_log();


--
-- Name: loading_dock_metric_numeric_s_whence_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_numeric_s_whence_log
    AFTER INSERT ON loading_dock_metric_numeric_archive
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_numeric_s_whence_log();


--
-- Name: loading_dock_metric_text_archive_empty; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_text_archive_empty
    BEFORE INSERT OR UPDATE ON loading_dock_metric_text_archive
    FOR EACH ROW
    EXECUTE PROCEDURE parent_empty();


--
-- Name: loading_dock_metric_text_s_change_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_text_s_change_log
    AFTER INSERT ON loading_dock_metric_text_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_text_s_change_log();


--
-- Name: loading_dock_metric_text_s_change_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_text_s_change_log
    AFTER INSERT ON loading_dock_metric_text_archive
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_text_s_change_log();


--
-- Name: loading_dock_status_archive_empty; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_status_archive_empty
    BEFORE INSERT OR UPDATE ON loading_dock_status_archive
    FOR EACH ROW
    EXECUTE PROCEDURE parent_empty();


--
-- Name: loading_dock_status_s_change_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_status_s_change_log
    AFTER INSERT ON loading_dock_status_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_status_s_change_log();


--
-- Name: loading_dock_status_s_change_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_status_s_change_log
    AFTER INSERT ON loading_dock_status_archive
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_status_s_change_log();


--
-- Name: mv_loading_dock_check_s; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER mv_loading_dock_check_s
    AFTER INSERT ON loading_dock_check_s
    FOR EACH ROW
    EXECUTE PROCEDURE mv_loading_dock_check_s();


--
-- Name: trig_update_tsvector_from_check_tags; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_from_check_tags
    AFTER INSERT OR UPDATE ON check_tags
    FOR EACH ROW
    EXECUTE PROCEDURE trig_update_tsvector_from_check_tags();


--
-- Name: trig_update_tsvector_from_metric_summary; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_from_metric_summary
    AFTER INSERT OR UPDATE ON metric_name_summary
    FOR EACH ROW
    EXECUTE PROCEDURE trig_update_tsvector_from_metric_summary();


--
-- Name: trig_update_tsvector_from_metric_tags; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_from_metric_tags
    AFTER INSERT OR UPDATE ON metric_tags
    FOR EACH ROW
    EXECUTE PROCEDURE trig_update_tsvector_from_metric_tags();


--
-- Name: trig_update_tsvector_from_mv_dock; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_from_mv_dock
    AFTER INSERT OR UPDATE ON mv_loading_dock_check_s
    FOR EACH ROW
    EXECUTE PROCEDURE trig_update_tsvector_from_mv_dock();


SET search_path = prism, pg_catalog;

--
-- Name: saved_graphs_dep_graphid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_graphid_fkey FOREIGN KEY (graphid) REFERENCES saved_graphs(graphid) ON DELETE CASCADE;


--
-- Name: saved_graphs_dep_sid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_sid_fkey FOREIGN KEY (sid, metric_name, metric_type) REFERENCES stratcon.metric_name_summary(sid, metric_name, metric_type);


--
-- Name: saved_worksheets_dep_graphid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY saved_worksheets_dep
    ADD CONSTRAINT saved_worksheets_dep_graphid_fkey FOREIGN KEY (graphid) REFERENCES saved_graphs(graphid) ON DELETE CASCADE;


--
-- Name: saved_worksheets_dep_sheetid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY saved_worksheets_dep
    ADD CONSTRAINT saved_worksheets_dep_sheetid_fkey FOREIGN KEY (sheetid) REFERENCES saved_worksheets(sheetid);


SET search_path = stratcon, pg_catalog;

--
-- Name: check_tags_sid_fkey; Type: FK CONSTRAINT; Schema: stratcon; Owner: reconnoiter
--

ALTER TABLE ONLY check_tags
    ADD CONSTRAINT check_tags_sid_fkey FOREIGN KEY (sid) REFERENCES map_uuid_to_sid(sid);


--
-- Name: metric_tags_sid_fkey; Type: FK CONSTRAINT; Schema: stratcon; Owner: reconnoiter
--

ALTER TABLE ONLY metric_tags
    ADD CONSTRAINT metric_tags_sid_fkey FOREIGN KEY (sid) REFERENCES map_uuid_to_sid(sid);


--
-- Name: prism; Type: ACL; Schema: -; Owner: prism
--

REVOKE ALL ON SCHEMA prism FROM PUBLIC;
REVOKE ALL ON SCHEMA prism FROM prism;
GRANT ALL ON SCHEMA prism TO prism;
GRANT ALL ON SCHEMA prism TO stratcon;


--
-- Name: stratcon; Type: ACL; Schema: -; Owner: stratcon
--

REVOKE ALL ON SCHEMA stratcon FROM PUBLIC;
REVOKE ALL ON SCHEMA stratcon FROM stratcon;
GRANT ALL ON SCHEMA stratcon TO stratcon;
GRANT USAGE ON SCHEMA stratcon TO prism;


SET search_path = prism, pg_catalog;

--
-- Name: graph_templates; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE graph_templates FROM PUBLIC;
REVOKE ALL ON TABLE graph_templates FROM reconnoiter;
GRANT ALL ON TABLE graph_templates TO reconnoiter;
GRANT ALL ON TABLE graph_templates TO prism;


--
-- Name: saved_graphs; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE saved_graphs FROM PUBLIC;
REVOKE ALL ON TABLE saved_graphs FROM reconnoiter;
GRANT ALL ON TABLE saved_graphs TO reconnoiter;
GRANT ALL ON TABLE saved_graphs TO prism;


--
-- Name: saved_graphs_dep; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE saved_graphs_dep FROM PUBLIC;
REVOKE ALL ON TABLE saved_graphs_dep FROM reconnoiter;
GRANT ALL ON TABLE saved_graphs_dep TO reconnoiter;
GRANT ALL ON TABLE saved_graphs_dep TO prism;


--
-- Name: saved_worksheets; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE saved_worksheets FROM PUBLIC;
REVOKE ALL ON TABLE saved_worksheets FROM reconnoiter;
GRANT ALL ON TABLE saved_worksheets TO reconnoiter;
GRANT ALL ON TABLE saved_worksheets TO prism;


--
-- Name: saved_worksheets_dep; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE saved_worksheets_dep FROM PUBLIC;
REVOKE ALL ON TABLE saved_worksheets_dep FROM reconnoiter;
GRANT ALL ON TABLE saved_worksheets_dep TO reconnoiter;
GRANT ALL ON TABLE saved_worksheets_dep TO prism;


SET search_path = stratcon, pg_catalog;

--
-- Name: check_tags; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE check_tags FROM PUBLIC;
REVOKE ALL ON TABLE check_tags FROM reconnoiter;
GRANT ALL ON TABLE check_tags TO reconnoiter;
GRANT ALL ON TABLE check_tags TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE check_tags TO stratcon;


--
-- Name: current_metric_text; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE current_metric_text FROM PUBLIC;
REVOKE ALL ON TABLE current_metric_text FROM reconnoiter;
GRANT ALL ON TABLE current_metric_text TO reconnoiter;
GRANT SELECT ON TABLE current_metric_text TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE current_metric_text TO stratcon;


--
-- Name: current_node_config; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE current_node_config FROM PUBLIC;
REVOKE ALL ON TABLE current_node_config FROM reconnoiter;
GRANT ALL ON TABLE current_node_config TO reconnoiter;
GRANT SELECT ON TABLE current_node_config TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE current_node_config TO stratcon;


--
-- Name: current_node_config_changelog; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE current_node_config_changelog FROM PUBLIC;
REVOKE ALL ON TABLE current_node_config_changelog FROM reconnoiter;
GRANT ALL ON TABLE current_node_config_changelog TO reconnoiter;
GRANT SELECT ON TABLE current_node_config_changelog TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE current_node_config_changelog TO stratcon;


--
-- Name: loading_dock_check_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_check_s FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_check_s FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_check_s TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_check_s TO stratcon;
GRANT SELECT ON TABLE loading_dock_check_s TO prism;


--
-- Name: loading_dock_metric_numeric_archive; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_numeric_archive FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_numeric_archive FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_numeric_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_numeric_archive TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_numeric_archive TO prism;


--
-- Name: loading_dock_metric_numeric_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_numeric_s FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_numeric_s FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_numeric_s TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_numeric_s TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_numeric_s TO prism;


--
-- Name: loading_dock_metric_numeric_s_old; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_numeric_s_old FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_numeric_s_old FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_numeric_s_old TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_numeric_s_old TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_numeric_s_old TO prism;


--
-- Name: loading_dock_metric_text_archive; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_text_archive FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_text_archive FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_text_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_text_archive TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_text_archive TO prism;


--
-- Name: loading_dock_metric_text_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_text_s FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_text_s FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_text_s TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_text_s TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_text_s TO prism;


--
-- Name: loading_dock_metric_text_s_change_log; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_text_s_change_log FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_text_s_change_log FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_text_s_change_log TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_text_s_change_log TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_text_s_change_log TO prism;


--
-- Name: loading_dock_status_archive; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_status_archive FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_status_archive FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_status_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_status_archive TO stratcon;
GRANT SELECT ON TABLE loading_dock_status_archive TO prism;


--
-- Name: loading_dock_status_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_status_s FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_status_s FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_status_s TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_status_s TO stratcon;
GRANT SELECT ON TABLE loading_dock_status_s TO prism;


--
-- Name: loading_dock_status_s_change_log; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_status_s_change_log FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_status_s_change_log FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_status_s_change_log TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_status_s_change_log TO stratcon;
GRANT SELECT ON TABLE loading_dock_status_s_change_log TO prism;


--
-- Name: log_whence_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE log_whence_s FROM PUBLIC;
REVOKE ALL ON TABLE log_whence_s FROM reconnoiter;
GRANT ALL ON TABLE log_whence_s TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE log_whence_s TO stratcon;
GRANT SELECT ON TABLE log_whence_s TO prism;


--
-- Name: map_uuid_to_sid; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE map_uuid_to_sid FROM PUBLIC;
REVOKE ALL ON TABLE map_uuid_to_sid FROM reconnoiter;
GRANT ALL ON TABLE map_uuid_to_sid TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE map_uuid_to_sid TO stratcon;
GRANT SELECT ON TABLE map_uuid_to_sid TO prism;


--
-- Name: metric_name_summary; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_name_summary FROM PUBLIC;
REVOKE ALL ON TABLE metric_name_summary FROM reconnoiter;
GRANT ALL ON TABLE metric_name_summary TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_name_summary TO stratcon;
GRANT SELECT,UPDATE ON TABLE metric_name_summary TO prism;


--
-- Name: metric_tags; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_tags FROM PUBLIC;
REVOKE ALL ON TABLE metric_tags FROM reconnoiter;
GRANT ALL ON TABLE metric_tags TO reconnoiter;
GRANT ALL ON TABLE metric_tags TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_tags TO stratcon;


--
-- Name: mv_loading_dock_check_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE mv_loading_dock_check_s FROM PUBLIC;
REVOKE ALL ON TABLE mv_loading_dock_check_s FROM reconnoiter;
GRANT ALL ON TABLE mv_loading_dock_check_s TO reconnoiter;
GRANT SELECT ON TABLE mv_loading_dock_check_s TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE mv_loading_dock_check_s TO stratcon;


--
-- Name: rollup_matrix_numeric_12hours; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE rollup_matrix_numeric_12hours FROM PUBLIC;
REVOKE ALL ON TABLE rollup_matrix_numeric_12hours FROM reconnoiter;
GRANT ALL ON TABLE rollup_matrix_numeric_12hours TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE rollup_matrix_numeric_12hours TO stratcon;
GRANT SELECT ON TABLE rollup_matrix_numeric_12hours TO prism;


--
-- Name: rollup_matrix_numeric_20m; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE rollup_matrix_numeric_20m FROM PUBLIC;
REVOKE ALL ON TABLE rollup_matrix_numeric_20m FROM reconnoiter;
GRANT ALL ON TABLE rollup_matrix_numeric_20m TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE rollup_matrix_numeric_20m TO stratcon;
GRANT SELECT ON TABLE rollup_matrix_numeric_20m TO prism;


--
-- Name: rollup_matrix_numeric_5m; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE rollup_matrix_numeric_5m FROM PUBLIC;
REVOKE ALL ON TABLE rollup_matrix_numeric_5m FROM reconnoiter;
GRANT ALL ON TABLE rollup_matrix_numeric_5m TO reconnoiter;
GRANT SELECT,INSERT,DELETE ON TABLE rollup_matrix_numeric_5m TO stratcon;
GRANT SELECT ON TABLE rollup_matrix_numeric_5m TO prism;


--
-- Name: rollup_matrix_numeric_60m; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE rollup_matrix_numeric_60m FROM PUBLIC;
REVOKE ALL ON TABLE rollup_matrix_numeric_60m FROM reconnoiter;
GRANT ALL ON TABLE rollup_matrix_numeric_60m TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE rollup_matrix_numeric_60m TO stratcon;
GRANT SELECT ON TABLE rollup_matrix_numeric_60m TO prism;


--
-- Name: rollup_matrix_numeric_6hours; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE rollup_matrix_numeric_6hours FROM PUBLIC;
REVOKE ALL ON TABLE rollup_matrix_numeric_6hours FROM reconnoiter;
GRANT ALL ON TABLE rollup_matrix_numeric_6hours TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE rollup_matrix_numeric_6hours TO stratcon;
GRANT SELECT ON TABLE rollup_matrix_numeric_6hours TO prism;


--
-- Name: rollup_runner; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE rollup_runner FROM PUBLIC;
REVOKE ALL ON TABLE rollup_runner FROM reconnoiter;
GRANT ALL ON TABLE rollup_runner TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE rollup_runner TO stratcon;
GRANT SELECT ON TABLE rollup_runner TO prism;


--
-- Name: tasklocks; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE tasklocks FROM PUBLIC;
REVOKE ALL ON TABLE tasklocks FROM reconnoiter;
GRANT ALL ON TABLE tasklocks TO reconnoiter;
GRANT SELECT,INSERT ON TABLE tasklocks TO stratcon;


--
-- Name: seq_sid; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON SEQUENCE seq_sid FROM PUBLIC;
REVOKE ALL ON SEQUENCE seq_sid FROM reconnoiter;
GRANT ALL ON SEQUENCE seq_sid TO reconnoiter;
GRANT ALL ON SEQUENCE seq_sid TO stratcon;


--
-- Name: tasklocks_id_seq; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON SEQUENCE tasklocks_id_seq FROM PUBLIC;
REVOKE ALL ON SEQUENCE tasklocks_id_seq FROM reconnoiter;
GRANT ALL ON SEQUENCE tasklocks_id_seq TO reconnoiter;
GRANT SELECT,UPDATE ON SEQUENCE tasklocks_id_seq TO stratcon;


--
-- PostgreSQL database dump complete
--

