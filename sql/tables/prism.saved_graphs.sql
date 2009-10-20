--
-- Name: saved_graphs; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE prism.saved_graphs (
    graphid uuid NOT NULL,
    json text NOT NULL,
    saved boolean DEFAULT false NOT NULL,
    title text,
    last_update timestamp without time zone NOT NULL,
    ts_search_all tsvector,
    graph_tags text[],
    genesis text
);


ALTER TABLE prism.saved_graphs OWNER TO reconnoiter;

--
-- Name: saved_graphs_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY prism.saved_graphs
    ADD CONSTRAINT saved_graphs_pkey PRIMARY KEY (graphid);


--
-- Name: unq_saved_graphs_title; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY prism.saved_graphs
    ADD CONSTRAINT unq_saved_graphs_title UNIQUE (title);


--
-- Name: idx_saved_graphs_ts_search_all; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX idx_saved_graphs_ts_search_all ON prism.saved_graphs USING btree (ts_search_all);


--
-- Name: unq_saved_graphs_genesis; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX unq_saved_graphs_genesis ON prism.saved_graphs USING btree (genesis);


--
-- Name: check_name_saved_graphs; Type: TRIGGER; Schema: prism; Owner: reconnoiter
--

CREATE TRIGGER check_name_saved_graphs
    BEFORE INSERT OR UPDATE ON prism.saved_graphs
    FOR EACH ROW
    EXECUTE PROCEDURE prism.check_name_saved_graphs();


--
-- Name: trig_update_tsvector_saved_graphs; Type: TRIGGER; Schema: prism; Owner: reconnoiter
--

CREATE TRIGGER trig_update_tsvector_saved_graphs
    BEFORE INSERT OR UPDATE ON prism.saved_graphs
    FOR EACH ROW
    EXECUTE PROCEDURE prism.trig_update_tsvector_saved_graphs();


--
-- Name: saved_graphs; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE prism.saved_graphs FROM PUBLIC;
REVOKE ALL ON TABLE prism.saved_graphs FROM reconnoiter;
GRANT ALL ON TABLE prism.saved_graphs TO reconnoiter;
GRANT ALL ON TABLE prism.saved_graphs TO prism;

