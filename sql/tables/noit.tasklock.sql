-- formerly tasklocks 
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
-- Name: tasklock; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE tasklock (
    id integer NOT NULL,
    name text NOT NULL
);


ALTER TABLE noit.tasklock OWNER TO reconnoiter;

--
-- Name: tasklock_name_key; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY tasklock
    ADD CONSTRAINT tasklock_name_key UNIQUE (name);


--
-- Name: tasklock_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY tasklock
    ADD CONSTRAINT tasklock_pkey PRIMARY KEY (id);


--
-- Name: tasklock; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE tasklock FROM PUBLIC;
REVOKE ALL ON TABLE tasklock FROM reconnoiter;
GRANT ALL ON TABLE tasklock TO reconnoiter;
GRANT SELECT,INSERT ON TABLE tasklock TO noit;

--
-- PostgreSQL database dump complete
--

