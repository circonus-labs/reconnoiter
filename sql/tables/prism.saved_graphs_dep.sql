--
-- Name: saved_graphs_dep; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE prism.saved_graphs_dep (
    graphid uuid NOT NULL,
    sid integer NOT NULL,
    metric_name text NOT NULL,
    metric_type character varying(22)
);


ALTER TABLE prism.saved_graphs_dep OWNER TO reconnoiter;

--
-- Name: saved_graphs_dep_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY prism.saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_pkey PRIMARY KEY (graphid, sid, metric_name);


--
-- Name: saved_graphs_dep_sid_name_type; Type: INDEX; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE INDEX saved_graphs_dep_sid_name_type ON prism.saved_graphs_dep USING btree (sid, metric_name, metric_type);


--
-- Name: saved_graphs_dep_graphid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY prism.saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_graphid_fkey FOREIGN KEY (graphid) REFERENCES prism.saved_graphs(graphid) ON DELETE CASCADE;


--
-- Name: saved_graphs_dep_sid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY prism.saved_graphs_dep
    ADD CONSTRAINT saved_graphs_dep_sid_fkey FOREIGN KEY (sid, metric_name, metric_type) REFERENCES noit.metric_name_summary(sid, metric_name, metric_type);


--
-- Name: saved_graphs_dep; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE prism.saved_graphs_dep FROM PUBLIC;
REVOKE ALL ON TABLE prism.saved_graphs_dep FROM reconnoiter;
GRANT ALL ON TABLE prism.saved_graphs_dep TO reconnoiter;
GRANT ALL ON TABLE prism.saved_graphs_dep TO prism;


