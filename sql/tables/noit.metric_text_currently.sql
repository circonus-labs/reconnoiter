--formerly current_metric_text 
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
-- Name: metric_text_currently; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_text_currently (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


ALTER TABLE noit.metric_text_currently OWNER TO reconnoiter;

--
-- Name: metric_text_currently_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_text_currently
    ADD CONSTRAINT metric_text_currently_pkey PRIMARY KEY (sid, name);


--
-- Name: metric_text_currently; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_text_currently FROM PUBLIC;
REVOKE ALL ON TABLE metric_text_currently FROM reconnoiter;
GRANT ALL ON TABLE metric_text_currently TO reconnoiter;
GRANT SELECT ON TABLE metric_text_currently TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_text_currently TO stratcon;


--
-- PostgreSQL database dump complete
--

