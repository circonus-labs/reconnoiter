CREATE OR REPLACE FUNCTION stratcon.choose_storage_node_for_sid(in v_uuid uuid, in v_noit text, out o_storage_node_id int, out o_fqdn text, out o_dsn text) 
RETURNS RECORD 
AS $$
BEGIN
    -- we could be smart with v_uuid and/or v_noit
    SELECT storage_node_id, fqdn, dsn from stratcon.storage_node order by random() limit 1 INTO o_storage_node_id, o_dsn; 
END
$$ LANGUAGE plpgsql 
SECURITY DEFINER
; 

GRANT EXECUTE ON FUNCTION stratcon.choose_storage_node_for_sid(uuid, text) TO stratcon;
