-- formerly rollup_matrix_numeric_1day 
--
-- PostgreSQL database dump
--

SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

SET search_path = noit, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: metric_numeric_rollup_1day; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_numeric_rollup_1day (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer[],
    avg_value numeric[],
    counter_dev numeric[],
    CONSTRAINT metric_numeric_rollup_1day_rollup_time_check CHECK (((date_part('hour'::text, timezone('UTC'::text, rollup_time)) = ANY (ARRAY[(0)::double precision])) AND (date_part('minute'::text, timezone('utc'::text, rollup_time)) = (0)::double precision)))
);


ALTER TABLE noit.metric_numeric_rollup_1day OWNER TO reconnoiter;

--
-- Name: metric_numeric_rollup_1day_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_numeric_rollup_1day
    ADD CONSTRAINT metric_numeric_rollup_1day_pkey PRIMARY KEY (rollup_time, sid, name);


--
-- Name: metric_numeric_rollup_1day; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_numeric_rollup_1day FROM PUBLIC;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_numeric_rollup_1day TO stratcon;
GRANT SELECT ON TABLE metric_numeric_rollup_1day TO prism;


--
-- PostgreSQL database dump complete
--

