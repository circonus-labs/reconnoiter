-- loading_dock_metric_text_s_change_log
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
-- Name: metric_text_changelog; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE metric_text_changelog (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text
);


ALTER TABLE noit.metric_text_changelog OWNER TO reconnoiter;

--
-- Name: metric_text_changelog_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY metric_text_changelog
    ADD CONSTRAINT metric_text_changelog_pkey PRIMARY KEY (whence, sid, name);


--
-- Name: xx; Type: INDEX; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX metric_text_changelog_sid_name_whence_idx ON metric_text_changelog USING btree (sid, name, whence);


--
-- Name: metric_text_changelog; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE metric_text_changelog FROM PUBLIC;
REVOKE ALL ON TABLE metric_text_changelog FROM reconnoiter;
GRANT ALL ON TABLE metric_text_changelog TO reconnoiter;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE metric_text_changelog TO stratcon;
GRANT SELECT ON TABLE metric_text_changelog TO prism;


--
-- PostgreSQL database dump complete
--

