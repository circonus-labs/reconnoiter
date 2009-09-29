-- formerly check_tags

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
-- Name: check_tag; Type: TABLE; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE check_tag (
    sid integer NOT NULL,
    tags_array text[]
);


ALTER TABLE stratcon.check_tag OWNER TO reconnoiter;

--
-- Name: check_tag_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY check_tag
    ADD CONSTRAINT check_tag_pkey PRIMARY KEY (sid);


--
-- Name: update_metric_name_summary_fulltext; Type: TRIGGER; Schema: stratcon; Owner: reconnoiter
--

CREATE TRIGGER update_metric_name_summary_fulltext 
    AFTER INSERT OR UPDATE ON check_tag
    FOR EACH ROW
    EXECUTE PROCEDURE update_mns_via_check_tag();


--
-- Name: check_tag_sid_fkey; Type: FK CONSTRAINT; Schema: stratcon; Owner: reconnoiter
--

ALTER TABLE ONLY check_tag
    ADD CONSTRAINT check_tag_sid_fkey FOREIGN KEY (sid) REFERENCES map_uuid_to_sid(sid);


--
-- Name: check_tag; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE check_tag FROM PUBLIC;
REVOKE ALL ON TABLE check_tag FROM reconnoiter;
GRANT ALL ON TABLE check_tag TO reconnoiter;
GRANT ALL ON TABLE check_tag TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE check_tag TO stratcon;


--
-- PostgreSQL database dump complete
--

