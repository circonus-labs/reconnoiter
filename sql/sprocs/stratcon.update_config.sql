--
-- Name: update_config(inet, text, timestamp with time zone, xml); Type: FUNCTION; Schema: stratcon; Owner: reconnoiter
--

CREATE FUNCTION stratcon.update_config(v_remote_address_in inet, v_node_type_in text, v_whence_in timestamp with time zone, v_config_in xml) RETURNS void
    AS $$
DECLARE
    v_config xml;
BEGIN
    select config into v_config from stratcon.current_node_config
     where remote_address = v_remote_address_in
       and node_type = v_node_type_in;
    IF FOUND THEN
        IF v_config::text = v_config_in::text THEN
            RETURN;
        END IF;
        delete from stratcon.current_node_config
              where remote_address = v_remote_address_in
                and node_type = v_node_type_in;
    END IF;
    insert into stratcon.current_node_config
                (remote_address, node_type, whence, config)
         values (v_remote_address_in, v_node_type_in, v_whence_in, v_config_in);
    insert into stratcon.current_node_config_changelog
                (remote_address, node_type, whence, config)
         values (v_remote_address_in, v_node_type_in, v_whence_in, v_config_in);
END
$$
    LANGUAGE plpgsql
SECURITY DEFINER
;

ALTER FUNCTION stratcon.update_config(v_remote_address_in inet, v_node_type_in text, v_whence_in timestamp with time zone, v_config_in xml) OWNER TO reconnoiter;

GRANT EXECUTE ON FUNCTION stratcon.update_config(inet, text, timestamp with time zone, xml) TO stratcon;

