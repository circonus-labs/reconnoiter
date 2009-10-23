CREATE TABLE stratcon.current_node_config_changelog (
    remote_address inet NOT NULL,
    node_type text NOT NULL,
    whence timestamp with time zone NOT NULL,
    config xml NOT NULL
);


ALTER TABLE stratcon.current_node_config_changelog OWNER TO reconnoiter;

--
-- Name: current_node_config_changelog_pkey; Type: CONSTRAINT; Schema: stratcon; Owner: reconnoiter; Tablespace:
--

ALTER TABLE ONLY stratcon.current_node_config_changelog
    ADD CONSTRAINT current_node_config_changelog_pkey PRIMARY KEY (remote_address, node_type, whence);

--
-- Name: current_node_config_changelog; Type: ACL; Schema: stratcon; Owner: reconnoiter
--

REVOKE ALL ON TABLE stratcon.current_node_config_changelog FROM PUBLIC;
REVOKE ALL ON TABLE stratcon.current_node_config_changelog FROM reconnoiter;
GRANT ALL ON TABLE stratcon.current_node_config_changelog TO reconnoiter;
GRANT SELECT ON TABLE stratcon.current_node_config_changelog TO prism;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE stratcon.current_node_config_changelog TO stratcon;

