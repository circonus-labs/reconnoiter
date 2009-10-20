--
-- Name: saved_worksheets; Type: TABLE; Schema: prism; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE prism.saved_worksheets (
    sheetid uuid NOT NULL,
    title text,
    saved boolean DEFAULT false,
    ts_search_all tsvector,
    tags text[],
    last_update timestamp with time zone DEFAULT now() NOT NULL
);


ALTER TABLE prism.saved_worksheets OWNER TO reconnoiter;

--
-- Name: saved_worksheets_pkey; Type: CONSTRAINT; Schema: prism; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY prism.saved_worksheets
    ADD CONSTRAINT saved_worksheets_pkey PRIMARY KEY (sheetid);


--
-- Name: saved_worksheets; Type: ACL; Schema: prism; Owner: reconnoiter
--

REVOKE ALL ON TABLE prism.saved_worksheets FROM PUBLIC;
REVOKE ALL ON TABLE prism.saved_worksheets FROM reconnoiter;
GRANT ALL ON TABLE prism.saved_worksheets TO reconnoiter;
GRANT ALL ON TABLE prism.saved_worksheets TO prism;


