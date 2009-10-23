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
-- Name: map_uuid_to_sid; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE map_uuid_to_sid (
    id uuid NOT NULL,
    sid serial NOT NULL,
    storage_node_id integer REFERENCES stratcon.storage_node
);


ALTER TABLE stratcon.map_uuid_to_sid OWNER TO reconnoiter;

--
-- Name: map_uuid_to_sid_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY map_uuid_to_sid
    ADD CONSTRAINT map_uuid_to_sid_pkey PRIMARY KEY (id);


--
-- Name: map_uuid_to_sid_idx; Type: INDEX; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE UNIQUE INDEX map_uuid_to_sid_idx ON map_uuid_to_sid USING btree (sid);


--
-- Name: map_uuid_to_sid; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE map_uuid_to_sid FROM PUBLIC;
REVOKE ALL ON TABLE map_uuid_to_sid FROM reconnoiter;
GRANT ALL ON TABLE map_uuid_to_sid TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE map_uuid_to_sid TO stratcon;
GRANT SELECT ON TABLE map_uuid_to_sid TO prism;


--
-- PostgreSQL database dump complete
--

