-- formerly loading_dock_metric_text_archive 

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
-- Name: metric_text_archive; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_text_archive (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


ALTER TABLE noit.metric_text_archive OWNER TO reconnoiter;

--
-- Name: metric_text_archive_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_text_archive
    ADD CONSTRAINT metric_text_archive_pkey PRIMARY KEY (whence, sid, name);

--
-- Name: loading_dock_metric_text_s_change_log; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER metric_text_archive_log_changes
    AFTER INSERT ON metric_text_archive
    FOR EACH ROW
    EXECUTE PROCEDURE metric_text_archive_log_changes();


--
-- Name: metric_text_archive; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_text_archive FROM PUBLIC;
REVOKE ALL ON TABLE metric_text_archive FROM reconnoiter;
GRANT ALL ON TABLE metric_text_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_text_archive TO stratcon;
GRANT SELECT ON TABLE metric_text_archive TO prism;


--
-- PostgreSQL database dump complete
--
