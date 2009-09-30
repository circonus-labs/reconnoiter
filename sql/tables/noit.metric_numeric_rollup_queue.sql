-- formerly log_whence_s  
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
-- Name: metric_numeric_rollup_queue; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_numeric_rollup_queue (
    whence timestamp with time zone NOT NULL,
    "interval" character text NOT NULL
);


ALTER TABLE noit.metric_numeric_rollup_queue OWNER TO reconnoiter;

--
-- Name: metric_numeric_rollup_queue_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_numeric_rollup_queue
    ADD CONSTRAINT metric_numeric_rollup_queue_pkey PRIMARY KEY (whence, "interval");


--
-- Name: metric_numeric_rollup_queue; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_numeric_rollup_queue FROM PUBLIC;
REVOKE ALL ON TABLE metric_numeric_rollup_queue FROM reconnoiter;
GRANT ALL ON TABLE metric_numeric_rollup_queue TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_numeric_rollup_queue TO stratcon;
GRANT SELECT ON TABLE metric_numeric_rollup_queue TO prism;


--
-- PostgreSQL database dump complete
--

