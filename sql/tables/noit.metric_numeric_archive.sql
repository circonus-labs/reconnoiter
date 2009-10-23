-- formerly loading_dock_metric_numeric_archive 
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
-- Name: metric_numeric_archive; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_numeric_archive (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value numeric
);


ALTER TABLE noit.metric_numeric_archive OWNER TO reconnoiter;

--
-- Name: metric_numeric_archive_whence_key; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_numeric_archive
    ADD CONSTRAINT metric_numeric_archive_pkey PRIMARY KEY (whence, sid, name);


--
-- Name: mark_metric_numeric_rollup_buffer; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER mark_metric_numeric_rollup_buffer
    AFTER INSERT ON metric_numeric_archive
    FOR EACH ROW
    EXECUTE PROCEDURE mark_metric_numeric_rollup_buffer();


--
-- Name: metric_numeric_archive; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_numeric_archive FROM PUBLIC;
REVOKE ALL ON TABLE metric_numeric_archive FROM reconnoiter;
GRANT ALL ON TABLE metric_numeric_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_numeric_archive TO stratcon;
GRANT SELECT ON TABLE metric_numeric_archive TO prism;


--
-- PostgreSQL database dump complete
--

