-- formerly mv_loading_dock_check_s 

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
-- Name: check_currently; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE check_currently (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp with time zone NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL
);


ALTER TABLE noit.check_currently OWNER TO reconnoiter;

--
-- Name: check_currently_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY check_currently
    ADD CONSTRAINT check_currently_pkey PRIMARY KEY (sid);

--
-- Name: check_currently_uidx; Type: INDEX; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE UNIQUE INDEX ucheck_currently_id_uidx ON check_currently USING btree (id);


--
-- Name: trig_update_tsvector_from_mv_dock; Type: TRIGGER; Schema: noit; Owner: reconnoiter
--

CREATE TRIGGER update_metric_summary_fulltext 
    AFTER INSERT OR UPDATE ON check_currently
    FOR EACH ROW
    EXECUTE PROCEDURE update_metric_summary_fulltext();


--
-- Name: check_currently; Type: ACL; Schema: noit; Owner: reconnoiter
--

REVOKE ALL ON TABLE check_currently FROM PUBLIC;
REVOKE ALL ON TABLE check_currently FROM reconnoiter;
GRANT ALL ON TABLE check_currently TO reconnoiter;
GRANT SELECT ON TABLE check_currently TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE check_currently TO stratcon;


--
-- PostgreSQL database dump complete
--

