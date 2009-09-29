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
-- Name: storage_node; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE storage_node (
    storage_node_id serial primary key, 
    total_storage bigint not null,
    used_storage bigint not null,
    ip text not null, 
    fqdn text not null,
    dsn text not null,
    last_updated timestamp not null default now()
);


ALTER TABLE stratcon.storage_node OWNER TO reconnoiter;


--
-- Name: storage_node; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE storage_node FROM PUBLIC;
REVOKE ALL ON TABLE storage_node FROM reconnoiter;
GRANT ALL ON TABLE storage_node TO reconnoiter;
GRANT SELECT ON TABLE storage_node TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE storage_node TO stratcon;


--
-- PostgreSQL database dump complete
--

