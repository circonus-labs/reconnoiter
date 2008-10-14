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

--
-- Name: plpgsql; Type: PROCEDURAL LANGUAGE; Schema: -; Owner: omniti
--

CREATE PROCEDURAL LANGUAGE plpgsql;


ALTER PROCEDURAL LANGUAGE plpgsql OWNER TO omniti;

SET search_path = prism, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

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
    graph_tags text[]
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

SET search_path = public, pg_catalog;

SET default_with_oids = true;

--
-- Name: pga_diagrams; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_diagrams (
    diagramname character varying(64) NOT NULL,
    diagramtables text,
    diagramlinks text
);


ALTER TABLE public.pga_diagrams OWNER TO reconnoiter;

--
-- Name: pga_forms; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_forms (
    formname character varying(64) NOT NULL,
    formsource text
);


ALTER TABLE public.pga_forms OWNER TO reconnoiter;

--
-- Name: pga_graphs; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_graphs (
    graphname character varying(64) NOT NULL,
    graphsource text,
    graphcode text
);


ALTER TABLE public.pga_graphs OWNER TO reconnoiter;

--
-- Name: pga_images; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_images (
    imagename character varying(64) NOT NULL,
    imagesource text
);


ALTER TABLE public.pga_images OWNER TO reconnoiter;

--
-- Name: pga_layout; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_layout (
    tablename character varying(64) NOT NULL,
    nrcols smallint,
    colnames text,
    colwidth text
);


ALTER TABLE public.pga_layout OWNER TO reconnoiter;

--
-- Name: pga_queries; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_queries (
    queryname character varying(64) NOT NULL,
    querytype character(1),
    querycommand text,
    querytables text,
    querylinks text,
    queryresults text,
    querycomments text
);


ALTER TABLE public.pga_queries OWNER TO reconnoiter;

--
-- Name: pga_reports; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_reports (
    reportname character varying(64) NOT NULL,
    reportsource text,
    reportbody text,
    reportprocs text,
    reportoptions text
);


ALTER TABLE public.pga_reports OWNER TO reconnoiter;

--
-- Name: pga_scripts; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE pga_scripts (
    scriptname character varying(64) NOT NULL,
    scriptsource text
);


ALTER TABLE public.pga_scripts OWNER TO reconnoiter;

SET default_with_oids = false;

--
-- Name: varnish_huh; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE varnish_huh (
    sid integer,
    whence timestamp with time zone,
    name text,
    value numeric
);


ALTER TABLE public.varnish_huh OWNER TO reconnoiter;

--
-- Name: varnish_huh2; Type: TABLE; Schema: public; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE varnish_huh2 (
    sid integer,
    whence timestamp with time zone,
    name text,
    value numeric
);


ALTER TABLE public.varnish_huh2 OWNER TO reconnoiter;

SET search_path = stratcon, pg_catalog;

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
-- Name: loading_dock_metric_numeric_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_numeric_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value numeric
);


ALTER TABLE stratcon.loading_dock_metric_numeric_s OWNER TO reconnoiter;

--
-- Name: loading_dock_metric_text_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_metric_text_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


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
-- Name: loading_dock_status_s; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE loading_dock_status_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text
);


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
    metric_type character varying(22),
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
    counter_dev numeric
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
    counter_dev numeric
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
-- Name: add_tags(integer, text, text, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION add_tags(in_sid integer, in_metric_name text, in_metric_type text, in_tags text) RETURNS void
    AS $$
DECLARE
v_sid integer; 
v_metric_name text;
v_metric_typle varchar(20);
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
 BEGIN
     v_tags_array:= string_to_array(in_tags,'');
     SELECT sid into p_sid 
      FROM stratcon.metric_tags 
      WHERE sid=in_sid AND metric_name=in_metric_name AND metric_type=in_metric_type;
     IF NOT FOUND THEN
          SELECT sid,metric_name,metric_type INTO v_sid, v_metric_name,v_metric_typle 
             FROM stratcon.metric_name_summary  
             WHERE sid=in_sid AND metric_name=in_metric_name AND metric_type=in_metric_type;   
          IF NOT FOUND THEN
               RAISE EXCEPTION 'Metric does not exist in metric_name_summary table';
          ELSE 
         INSERT INTO stratcon.metric_tags (sid,metric_name,metric_type,tags_array) values(v_sid, v_metric_name,v_metric_typle,v_tags_array);
      END IF;
     ELSE
       SELECT tags_array INTO p_tags_array 
          FROM stratcon.metric_tags 
          WHERE sid=in_sid AND metric_name=in_metric_name AND metric_type=in_metric_type;
             new_tags_array:= array_append(p_tags_array, in_tags);
           UPDATE  stratcon.metric_tags SET tags_array= new_tags_array WHERE sid=in_sid AND metric_name=in_metric_name AND metric_type=in_metric_type;          
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.add_tags(in_sid integer, in_metric_name text, in_metric_type text, in_tags text) OWNER TO reconnoiter;

--
-- Name: check_name_saved_graphs(); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION check_name_saved_graphs() RETURNS trigger
    AS $$
DECLARE
BEGIN
    IF  NEW.saved IS true AND NEW.title IS null THEN
    RAISE EXCEPTION 'You must name graph to save';
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
-- Name: remove_tags(integer, text, text, text); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION remove_tags(in_sid integer, in_metric_name text, in_metric_type text, in_tags text) RETURNS void
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
      WHERE sid=in_sid AND metric_name=in_metric_name AND metric_type=in_metric_type;
     IF NOT FOUND THEN
          
               RAISE EXCEPTION 'Metric tags does not found to be removed';
          
     ELSE
         FOR i IN array_lower(p_tags_array, 1)..array_upper(p_tags_array, 1) LOOP
         IF NOT p_tags_array[i] =any(v_tags_array) THEN
            new_tags_array = array_append(new_tags_array, p_tags_array[i]);
          END IF;
         END LOOP;
       
           UPDATE  stratcon.metric_tags SET tags_array= new_tags_array WHERE sid=in_sid AND metric_name=in_metric_name AND metric_type=in_metric_type;          
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.remove_tags(in_sid integer, in_metric_name text, in_metric_type text, in_tags text) OWNER TO reconnoiter;

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
-- Name: trig_before_tsvector_saved_graphs(); Type: FUNCTION; Schema: prism; Owner: postgres
--

CREATE FUNCTION trig_before_tsvector_saved_graphs() RETURNS trigger
    AS $$
DECLARE
 BEGIN
   NEW.ts_search_all:= to_tsvector(NEW.title);
     RETURN NEW;
 END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.trig_before_tsvector_saved_graphs() OWNER TO postgres;

--
-- Name: trig_update_tsvector_saved_graphs(); Type: FUNCTION; Schema: prism; Owner: reconnoiter
--

CREATE FUNCTION trig_update_tsvector_saved_graphs() RETURNS trigger
    AS $$
DECLARE
 BEGIN
   IF (NEW.graph_tags <> OLD.graph_tags OR NEW.title <> OLD.title) THEN
           UPDATE prism.saved_graphs SET ts_search_all=prism.saved_graphs_tsvector(NEW.graphid) where graphid=NEW.graphid;
   END IF;    
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.trig_update_tsvector_saved_graphs() OWNER TO reconnoiter;

SET search_path = public, pg_catalog;

--
-- Name: date_hour(timestamp with time zone); Type: FUNCTION; Schema: public; Owner: reconnoiter
--

CREATE FUNCTION date_hour(timestamp with time zone) RETURNS timestamp with time zone
    AS $_$
 SELECT date_trunc('hour',$1);
$_$
    LANGUAGE sql IMMUTABLE STRICT;


ALTER FUNCTION public.date_hour(timestamp with time zone) OWNER TO reconnoiter;

SET search_path = stratcon, pg_catalog;

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

             SELECT value FROM  stratcon.loading_dock_metric_text_s WHERE sid = NEW.sid AND name = NEW.name
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
     WHERE sid=in_sid and metric_name=in_metric_name and metric_type=in_metric_type;
    IF NOT FOUND THEN
        ref_tags:=' ';
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
                                ref_tags);
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
  DELETE FROM stratcon.loading_dock_metric_text_s WHERE sid=v_del_sid AND name=v_del_metric_name;
     GET DIAGNOSTICS deleted_t = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
           RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_text_s : %',deleted_t;
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
   DELETE FROM stratcon.loading_dock_metric_numeric_s WHERE sid=v_del_sid AND name=v_del_metric_name;
   GET DIAGNOSTICS deleted_n = ROW_COUNT;
     IF v_debug = 'DEBUG' THEN
         RAISE NOTICE 'DELELTED ROWS FROM loading_dock_metric_numeric_s : %',deleted_n;
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

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_12hours';

RETURN;
EXCEPTION
    WHEN RAISE_EXCEPTION THEN
      UPDATE stratcon.rollup_runner set runner = '' where rollup_table = 'rollup_matrix_numeric_12hours';
      RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
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
                       SUM(count_rows) as count_rows ,(SUM(avg_value*count_rows)/SUM(count_rows)) as avg_value,
                       (SUM(counter_dev*count_rows)/SUM(count_rows)) as counter_dev
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
 
  UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
  
RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_20m';
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
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

    select n.sid, n.name, n.rollup_time, n.count_rows, n.avg_value,
           case when n.avg_value - l.avg_value >= 0
                then (n.avg_value - l.avg_value)/300.0
                else null end as counter_dev
      from (SELECT sid, name, v_min_whence as rollup_time,
                   COUNT(1) as count_rows, avg(value) as avg_value
              FROM stratcon.loading_dock_metric_numeric_s
             WHERE whence <= v_min_whence AND whence > v_min_whence -'5 minutes'::interval
          GROUP BY rollup_time,sid,name) as n
 left join stratcon.rollup_matrix_numeric_5m as l
        on (n.sid=l.sid and n.name=l.name and
            n.rollup_time - '5 minute'::interval = l.rollup_time)
 
       LOOP
    
        
        INSERT INTO stratcon.rollup_matrix_numeric_5m
         (sid,name,rollup_time,count_rows,avg_value,counter_dev) VALUES 
         (rec.sid,rec.name,rec.rollup_time,rec.count_rows,rec.avg_value,rec.counter_dev);
        
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

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_60m';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
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

UPDATE stratcon.rollup_runner SET RUNNER = '' WHERE ROLLUP_TABLE= 'rollup_matrix_numeric_6hours';

RETURN;

EXCEPTION
    WHEN RAISE_EXCEPTION THEN
       RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
       RAISE NOTICE '%', SQLERRM;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION stratcon.rollup_matrix_numeric_6hours() OWNER TO reconnoiter;

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
    UPDATE stratcon.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,NEW.metric_type) 
    where sid=NEW.sid and metric_name=NEW.metric_name and metric_type = NEW.metric_type ;
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
-- Name: seq_sid; Type: SEQUENCE; Schema: stratcon; Owner: reconnoiter
--

CREATE SEQUENCE seq_sid
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;


ALTER TABLE stratcon.seq_sid OWNER TO reconnoiter;

SET search_path = prism, pg_catalog;

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


SET search_path = public, pg_catalog;

--
-- Name: pga_diagrams_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_diagrams
    ADD CONSTRAINT pga_diagrams_pkey PRIMARY KEY (diagramname);


--
-- Name: pga_forms_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_forms
    ADD CONSTRAINT pga_forms_pkey PRIMARY KEY (formname);


--
-- Name: pga_graphs_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_graphs
    ADD CONSTRAINT pga_graphs_pkey PRIMARY KEY (graphname);


--
-- Name: pga_images_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_images
    ADD CONSTRAINT pga_images_pkey PRIMARY KEY (imagename);


--
-- Name: pga_layout_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_layout
    ADD CONSTRAINT pga_layout_pkey PRIMARY KEY (tablename);


--
-- Name: pga_queries_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_queries
    ADD CONSTRAINT pga_queries_pkey PRIMARY KEY (queryname);


--
-- Name: pga_reports_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_reports
    ADD CONSTRAINT pga_reports_pkey PRIMARY KEY (reportname);


--
-- Name: pga_scripts_pkey; Type: CONSTRAINT; Schema: public; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY pga_scripts
    ADD CONSTRAINT pga_scripts_pkey PRIMARY KEY (scriptname);


SET search_path = stratcon, pg_catalog;

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
-- Name: loading_dock_metric_numeric_s_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY loading_dock_metric_numeric_s
    ADD CONSTRAINT loading_dock_metric_numeric_s_pkey PRIMARY KEY (whence, sid, name);

ALTER TABLE loading_dock_metric_numeric_s CLUSTER ON loading_dock_metric_numeric_s_pkey;


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
    ADD CONSTRAINT map_uuid_to_sid_pkey PRIMARY KEY (id, sid);


--
-- Name: metric_name_summary_pk; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_name_summary
    ADD CONSTRAINT metric_name_summary_pk UNIQUE (sid, metric_name, metric_type);


--
-- Name: metric_tags_pk; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_tags
    ADD CONSTRAINT metric_tags_pk UNIQUE (sid, metric_name, metric_type);


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


SET search_path = prism, pg_catalog;

--
-- Name: idx_saved_graphs_ts_search_all; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_saved_graphs_ts_search_all ON saved_graphs USING btree (ts_search_all);


SET search_path = stratcon, pg_catalog;

--
-- Name: idx_metric_name_summary_ts_search_all; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_metric_name_summary_ts_search_all ON metric_name_summary USING btree (ts_search_all);


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
-- Name: trig_before_tsvector_saved_graphs; Type: TRIGGER; Schema: prism; Owner: reconnoiter
--

CREATE TRIGGER trig_before_tsvector_saved_graphs
    BEFORE INSERT ON saved_graphs
    FOR EACH ROW
    EXECUTE PROCEDURE trig_before_tsvector_saved_graphs();


--
-- Name: trig_update_tsvector_saved_graphs; Type: TRIGGER; Schema: prism; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_saved_graphs
    AFTER UPDATE ON saved_graphs
    FOR EACH ROW
    EXECUTE PROCEDURE trig_update_tsvector_saved_graphs();


SET search_path = stratcon, pg_catalog;

--
-- Name: loading_dock_metric_numeric_s_whence_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_numeric_s_whence_log
    AFTER INSERT ON loading_dock_metric_numeric_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_numeric_s_whence_log();


--
-- Name: loading_dock_metric_text_s_change_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_metric_text_s_change_log
    AFTER INSERT ON loading_dock_metric_text_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_text_s_change_log();


--
-- Name: loading_dock_status_s_change_log; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER loading_dock_status_s_change_log
    AFTER INSERT ON loading_dock_status_s
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
-- Name: graphid_fk; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY saved_graphs_dep
    ADD CONSTRAINT graphid_fk FOREIGN KEY (graphid) REFERENCES saved_graphs(graphid);


--
-- Name: saved_graphs_dep_sid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_sid_fkey FOREIGN KEY (sid, metric_name, metric_type) REFERENCES stratcon.metric_name_summary(sid, metric_name, metric_type);


--
-- Name: public; Type: ACL; Schema: -; Owner: postgres
--

REVOKE ALL ON SCHEMA public FROM PUBLIC;
REVOKE ALL ON SCHEMA public FROM postgres;
GRANT ALL ON SCHEMA public TO postgres;
GRANT ALL ON SCHEMA public TO PUBLIC;


--
-- Name: stratcon; Type: ACL; Schema: -; Owner: stratcon
--

REVOKE ALL ON SCHEMA stratcon FROM PUBLIC;
REVOKE ALL ON SCHEMA stratcon FROM stratcon;
GRANT ALL ON SCHEMA stratcon TO stratcon;
GRANT USAGE ON SCHEMA stratcon TO prism;


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


SET search_path = stratcon, pg_catalog;

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
-- Name: loading_dock_metric_numeric_s; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE loading_dock_metric_numeric_s FROM PUBLIC;
REVOKE ALL ON TABLE loading_dock_metric_numeric_s FROM reconnoiter;
GRANT ALL ON TABLE loading_dock_metric_numeric_s TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE loading_dock_metric_numeric_s TO stratcon;
GRANT SELECT ON TABLE loading_dock_metric_numeric_s TO prism;


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
-- Name: seq_sid; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON SEQUENCE seq_sid FROM PUBLIC;
REVOKE ALL ON SEQUENCE seq_sid FROM reconnoiter;
GRANT ALL ON SEQUENCE seq_sid TO reconnoiter;
GRANT ALL ON SEQUENCE seq_sid TO stratcon;


--
-- PostgreSQL database dump complete
--

