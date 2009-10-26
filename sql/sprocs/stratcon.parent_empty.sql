CREATE OR REPLACE FUNCTION stratcon.parent_empty() RETURNS TRIGGER
AS $$
BEGIN
    RAISE EXCEPTION 'Cannot insert into parent table';
    RETURN NULL;
END
$$ LANGUAGE plpgsql;

GRANT EXECUTE ON FUNCTION stratcon.parent_empty() TO public;

