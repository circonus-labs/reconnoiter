-- formerly rollup_matrix_numeric_4hour 
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
-- Name: metric_numeric_rollup_4hour; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_numeric_rollup_4hour (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer[],
    avg_value numeric[],
    counter_dev numeric[],
    CONSTRAINT metric_numeric_rollup_4hour_rollup_time_check CHECK (((date_part('hour'::text, timezone('UTC'::text, rollup_time)) = ANY (ARRAY[(0)::double precision, (4)::double precision, (8)::double precision, (12)::double precision, (16)::double precision, (20)::double precision])) AND (date_part('minute'::text, timezone('utc'::text, rollup_time)) = (0)::double precision)))
);


ALTER TABLE metric_numeric_rollup_4hour OWNER TO reconnoiter;

--
-- Name: metric_numeric_rollup_4hour_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_numeric_rollup_4hour
    ADD CONSTRAINT metric_numeric_rollup_4hour_pkey PRIMARY KEY (rollup_time, sid, name);

--
-- Name: metric_numeric_rollup_4hour; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_numeric_rollup_4hour FROM PUBLIC;
REVOKE ALL ON TABLE metric_numeric_rollup_4hour FROM reconnoiter;
GRANT ALL ON TABLE metric_numeric_rollup_4hour TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_numeric_rollup_4hour TO stratcon;
GRANT SELECT ON TABLE metric_numeric_rollup_4hour TO prism;


--
-- PostgreSQL database dump complete
--

