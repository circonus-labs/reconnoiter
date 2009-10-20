--
-- Name: graph_templates; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE prism.graph_templates (
    templateid uuid NOT NULL,
    title text NOT NULL,
    json text NOT NULL
);


ALTER TABLE prism.graph_templates OWNER TO reconnoiter;

--
-- Name: graph_templates_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY prism.graph_templates
    ADD CONSTRAINT graph_templates_pkey PRIMARY KEY (templateid);


--
-- Name: graph_templates_title_key; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY prism.graph_templates
    ADD CONSTRAINT graph_templates_title_key UNIQUE (title);


--
-- Name: graph_templates; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE prism.graph_templates FROM PUBLIC;
REVOKE ALL ON TABLE prism.graph_templates FROM reconnoiter;
GRANT ALL ON TABLE prism.graph_templates TO reconnoiter;
GRANT ALL ON TABLE prism.graph_templates TO prism;


