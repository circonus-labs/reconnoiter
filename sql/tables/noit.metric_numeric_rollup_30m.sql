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
-- Name: metric_numeric_rollup_30m; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_numeric_rollup_30m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer[],
    avg_value numeric[],
    counter_dev numeric[]
);


ALTER TABLE noit.metric_numeric_rollup_30m OWNER TO reconnoiter;

--
-- Name: metric_numeric_rollup_30m_new_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_numeric_rollup_30m
    ADD CONSTRAINT metric_numeric_rollup_30m_pkey PRIMARY KEY (rollup_time, sid, name);


--
-- Name: metric_numeric_rollup_30m; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_numeric_rollup_30m FROM PUBLIC;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_numeric_rollup_30m TO stratcon;
GRANT SELECT ON TABLE metric_numeric_rollup_30m TO prism;


--
-- PostgreSQL database dump complete
--

