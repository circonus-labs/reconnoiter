-- Name: metric_numeric_rollup_config; Type: TABLE; Schema: noit; Owner: reconnoiter; Tablespace: 
--

CREATE TABLE noit.metric_numeric_rollup_config (
    rollup text NOT NULL,
    seconds integer NOT NULL,
    dependent_on text,
    span integer NOT NULL
);


ALTER TABLE noit.metric_numeric_rollup_config OWNER TO reconnoiter;

--
-- Data for Name: metric_numeric_rollup_config; Type: TABLE DATA; Schema: noit; Owner: reconnoiter
--

INSERT INTO noit.metric_numeric_rollup_config VALUES ('5m', 300, NULL, 86400);
INSERT INTO noit.metric_numeric_rollup_config VALUES ('20m', 1200, '5m', 604800);
INSERT INTO noit.metric_numeric_rollup_config VALUES ('30m', 1800, '5m', 1209600);
INSERT INTO noit.metric_numeric_rollup_config VALUES ('1h', 3600, '30m', 2592000);
INSERT INTO noit.metric_numeric_rollup_config VALUES ('4h', 14400, '1h', 7862400);
INSERT INTO noit.metric_numeric_rollup_config VALUES ('1d', 86400, '4h', 31536000);

--
-- Name: metric_numeric_rollup_config_pkey; Type: CONSTRAINT; Schema: noit; Owner: reconnoiter; Tablespace: 
--

ALTER TABLE ONLY noit.metric_numeric_rollup_config
    ADD CONSTRAINT metric_numeric_rollup_config_pkey PRIMARY KEY (rollup);


--
-- Name: metric_numeric_rollup_config_dependent_on_fkey; Type: FK CONSTRAINT; Schema: noit; Owner: reconnoiter
--

ALTER TABLE ONLY noit.metric_numeric_rollup_config
    ADD CONSTRAINT metric_numeric_rollup_config_dependent_on_fkey FOREIGN KEY (dependent_on) REFERENCES noit.metric_numeric_rollup_config(rollup);


--
-- PostgreSQL database dump complete
--
