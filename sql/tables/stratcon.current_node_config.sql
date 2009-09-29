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
-- Name: current_node_config; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE current_node_config (
    remote_address inet NOT NULL,
    node_type text NOT NULL,
    whence timestamp with time zone NOT NULL,
    config xml NOT NULL
);


ALTER TABLE stratcon.current_node_config OWNER TO reconnoiter;

--
-- Name: current_node_config_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY current_node_config
    ADD CONSTRAINT current_node_config_pkey PRIMARY KEY (remote_address, node_type);


--
-- Name: current_node_config; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE current_node_config FROM PUBLIC;
REVOKE ALL ON TABLE current_node_config FROM reconnoiter;
GRANT ALL ON TABLE current_node_config TO reconnoiter;
GRANT SELECT ON TABLE current_node_config TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE current_node_config TO stratcon;


--
-- PostgreSQL database dump complete
--

