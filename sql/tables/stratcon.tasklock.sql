--
-- PostgreSQL database dump
--

SET client_encoding = 'UTF8';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

SET search_path = stratcon, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: tasklock_id_seq; Type: SEQUENCE; Schema: stratcon; Owner: reconnoiter
--

CREATE SEQUENCE tasklock_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

ALTER TABLE stratcon.tasklock_id_seq OWNER TO reconnoiter;

--
-- Name: tasklock_id_seq; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON SEQUENCE tasklock_id_seq FROM PUBLIC;
REVOKE ALL ON SEQUENCE tasklock_id_seq FROM reconnoiter;
GRANT ALL ON SEQUENCE tasklock_id_seq TO reconnoiter;
GRANT SELECT,UPDATE ON SEQUENCE tasklock_id_seq TO stratcon;


--
-- PostgreSQL database dump complete
--

