-- formerly loading_dock_status_s_changelog 

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
-- Name: check_status_changelog; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE check_status_changelog (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text
);


ALTER TABLE noit.check_status_changelog OWNER TO reconnoiter;

--
-- Name: check_status_changelog_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY check_status_changelog
    ADD CONSTRAINT check_status_changelog_pkey PRIMARY KEY (sid, whence);


--
-- Name: check_status_changelog; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE check_status_changelog FROM PUBLIC;
REVOKE ALL ON TABLE check_status_changelog FROM reconnoiter;
GRANT ALL ON TABLE check_status_changelog TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE check_status_changelog TO stratcon;
GRANT SELECT ON TABLE check_status_changelog TO prism;


--
-- PostgreSQL database dump complete
--

