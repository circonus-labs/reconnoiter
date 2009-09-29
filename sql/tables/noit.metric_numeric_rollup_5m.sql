-- formerly rollup_matrix_numberic_5m

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
-- Name: metric_numeric_rollup_5m; Type: TABLE; Schema: noit; Owner: postgres; Tablespace: 
--

CREATE TABLE metric_numeric_rollup_5m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer[],
    avg_value numeric[],
    counter_dev numeric[]
);


ALTER TABLE metric_numeric_rollup_5m OWNER TO postgres;

--
-- Name: fmetric_numeric_rollup_5m_pkey; Type: CONSTRAINT; Schema: noit; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY metric_numeric_rollup_5m
    ADD CONSTRAINT fmetric_numeric_rollup_5m_pkey PRIMARY KEY (rollup_time, sid, name);


--
-- Name: metric_numeric_rollup_5m; Type: ACL; Schema: noit; Owner: postgres
--

REVOKE ALL ON TABLE metric_numeric_rollup_5m FROM PUBLIC;
REVOKE ALL ON TABLE metric_numeric_rollup_5m FROM postgres;
GRANT ALL ON TABLE metric_numeric_rollup_5m TO postgres;
GRANT ALL ON TABLE metric_numeric_rollup_5m TO reconnoiter;
GRANT SELECT ON TABLE metric_numeric_rollup_5m TO prism;
GRANT ALL ON TABLE metric_numeric_rollup_5m TO stratcon;


--
-- PostgreSQL database dump complete
--

