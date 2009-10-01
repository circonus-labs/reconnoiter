CREATE OR REPLACE FUNCTION stratcon.choose_storage_node_for_sid(out o_storage_node_id int, out o_dsn text) 
RETURNS RECORD 
AS $$
BEGIN
    SELECT storage_node_id, dsn from stratcon.storage_node order by random() limit 1 INTO o_storage_node_id, o_dsn; 
END
$$ LANGUAGE plpgsql 
SECURITY DEFINER
; 

GRANT EXECUTE ON FUNCTION stratcon.choose_storage_node_for_sid() TO stratcon;
