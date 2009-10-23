-- formerly loading_dock_status_archive  

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
-- Name: check_status_archive; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE check_status_archive (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text
);


ALTER TABLE noit.check_status_archive OWNER TO reconnoiter;

--
-- Name: check_status_archive_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY check_status_archive
    ADD CONSTRAINT check_status_archive_pkey PRIMARY KEY (whence, sid);


--
-- Name: check_status_archive_log_changes ; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER check_status_archive_log_changes 
    AFTER INSERT ON check_status_archive
    FOR EACH ROW
    EXECUTE PROCEDURE check_status_archive_log_changes();


--
-- Name: check_status_archive; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE check_status_archive FROM PUBLIC;
REVOKE ALL ON TABLE check_status_archive FROM reconnoiter;
GRANT ALL ON TABLE check_status_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE check_status_archive TO stratcon;
GRANT SELECT ON TABLE check_status_archive TO prism;


--
-- PostgreSQL database dump complete
--

