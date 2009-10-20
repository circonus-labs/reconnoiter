--
-- Name: saved_worksheets_dep; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE prism.saved_worksheets_dep (
    sheetid uuid NOT NULL,
    ordering integer NOT NULL,
    graphid uuid NOT NULL
);


ALTER TABLE prism.saved_worksheets_dep OWNER TO reconnoiter;

--
-- Name: saved_worksheets_dep_graphid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY prism.saved_worksheets_dep
    ADD CONSTRAINT saved_worksheets_dep_graphid_fkey FOREIGN KEY (graphid) REFERENCES prism.saved_graphs(graphid) ON DELETE CASCADE;


--
-- Name: saved_worksheets_dep_sheetid_fkey; Type: FK CONSTRAINT; Schema: prism; Owner: reconnoiter
--

ALTER TABLE ONLY prism.saved_worksheets_dep
    ADD CONSTRAINT saved_worksheets_dep_sheetid_fkey FOREIGN KEY (sheetid) REFERENCES prism.saved_worksheets(sheetid);


--
-- Name: saved_worksheets_dep; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE prism.saved_worksheets_dep FROM PUBLIC;
REVOKE ALL ON TABLE prism.saved_worksheets_dep FROM reconnoiter;
GRANT ALL ON TABLE prism.saved_worksheets_dep TO reconnoiter;
GRANT ALL ON TABLE prism.saved_worksheets_dep TO prism;


