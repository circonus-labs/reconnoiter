CREATE OR REPLACE FUNCTION prism.check_name_saved_graphs() RETURNS trigger
    AS $$
DECLARE
BEGIN
    IF  NEW.saved IS true AND NEW.title IS null THEN
    RAISE EXCEPTION 'You must name graph to save.';
    END IF;
 RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.check_name_saved_graphs() OWNER TO reconnoiter;
