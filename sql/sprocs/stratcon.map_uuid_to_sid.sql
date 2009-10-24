CREATE OR REPLACE FUNCTION stratcon.map_uuid_to_sid(in v_uuid uuid, in v_noit text, out o_sid int, out o_noit text, out o_storage_node_id int, out o_fqdn text, out o_dsn text) 
RETURNS RECORD 
AS $$
BEGIN
    -- we don't currently do anything with the noit name, 
    -- but I think we had a reason to use it

    SELECT sid, storage_node_id, noit, fqdn, dsn
      FROM stratcon.map_uuid_to_sid LEFT JOIN stratcon.storage_node USING (storage_node_id)
     WHERE id = v_uuid INTO o_sid, o_storage_node_id, o_noit, o_fqdn, o_dsn; 
    IF NOT FOUND THEN

	SELECT nextval('stratcon.map_uuid_to_sid_sid_seq') INTO o_sid;  
	SELECT * FROM stratcon.choose_storage_node_for_sid(v_uuid, v_noit) INTO o_storage_node_id, o_fqdn, o_dsn; 

        INSERT INTO stratcon.map_uuid_to_sid(id,sid,noit,storage_node_id) VALUES (v_uuid, o_sid, v_noit, o_storage_node_id); 

    END IF;

END
$$ LANGUAGE plpgsql 
SECURITY DEFINER
; 

GRANT EXECUTE ON FUNCTION stratcon.map_uuid_to_sid(uuid,text) TO stratcon;

