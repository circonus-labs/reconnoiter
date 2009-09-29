-- formerly metric_name_summary (the same!) 

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
-- Name: metric_name_summary; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_name_summary (
    sid integer NOT NULL,
    metric_name text NOT NULL,
    metric_type text,
    active boolean DEFAULT true,
    fts_data tsvector
);


ALTER TABLE noit.metric_name_summary OWNER TO reconnoiter;

--
-- Name: metric_name_summary_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_name_summary
    ADD CONSTRAINT metric_name_summary_pkey UNIQUE (sid, metric_name, metric_type);


--
-- Name: metric_name_summary_fts_data_idx; Type: INDEX; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX metric_name_summary_fts_data_idx ON metric_name_summary USING gin (ts_search_all);


--
-- Name: trig_update_tsvector_from_metric_summary; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER update_metric_name_summary
    BEFORE INSERT OR UPDATE ON metric_name_summary
    FOR EACH ROW
    EXECUTE PROCEDURE update_mns_via_self();


--
-- Name: metric_name_summary; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_name_summary FROM PUBLIC;
REVOKE ALL ON TABLE metric_name_summary FROM reconnoiter;
GRANT ALL ON TABLE metric_name_summary TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_name_summary TO stratcon;
GRANT SELECT,UPDATE ON TABLE metric_name_summary TO prism;


--
-- PostgreSQL database dump complete
--

