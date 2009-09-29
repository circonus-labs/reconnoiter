-- formerly loading_dock_check_s

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
-- Name: check_archive; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE check_archive (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp with time zone NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL
);


ALTER TABLE noit.check_archive OWNER TO reconnoiter;

--
-- Name: check_archive_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY check_archive
    ADD CONSTRAINT check_archive_pkey PRIMARY KEY (sid, id, whence);


--
-- Name: mv_check_archive; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER check_archive_log_changes
    AFTER INSERT ON check_archive
    FOR EACH ROW
    EXECUTE PROCEDURE check_archive_log_changes();


--
-- Name: check_archive; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE check_archive FROM PUBLIC;
REVOKE ALL ON TABLE check_archive FROM reconnoiter;
GRANT ALL ON TABLE check_archive TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE check_archive TO stratcon;
GRANT SELECT ON TABLE check_archive TO prism;


--
-- PostgreSQL database dump complete
--

