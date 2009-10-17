-- formerly metric_tags 
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
-- Name: metric_tag; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_tag (
    sid integer NOT NULL,
    metric_name text NOT NULL,
    tags_array text[]
);


ALTER TABLE noit.metric_tag OWNER TO reconnoiter;

--
-- Name: metric_tag_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_tag
    ADD CONSTRAINT metric_tag_pkey PRIMARY KEY (sid, metric_name);


--
-- Name: trig_update_tsvector_from_metric_tag; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER update_metric_name_summary
    AFTER INSERT OR UPDATE ON metric_tag
    FOR EACH ROW
    EXECUTE PROCEDURE update_mns_via_metric_tag();


--
-- Name: metric_tag_sid_fkey; Type: FK CONSTRAINT; Schema: noit; Owner: reconnoiter
-- NOTE: this FK reference is only valid on single database installations 
--

ALTER TABLE ONLY metric_tag
    ADD CONSTRAINT metric_tag_sid_fkey FOREIGN KEY (sid) REFERENCES stratcon.map_uuid_to_sid(sid);


--
-- Name: metric_tag; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_tag FROM PUBLIC;
REVOKE ALL ON TABLE metric_tag FROM reconnoiter;
GRANT ALL ON TABLE metric_tag TO reconnoiter;
GRANT ALL ON TABLE metric_tag TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_tag TO stratcon;


--
-- PostgreSQL database dump complete
--

