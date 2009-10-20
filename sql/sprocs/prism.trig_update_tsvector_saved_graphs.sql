CREATE OR REPLACE FUNCTION prism.trig_update_tsvector_saved_graphs() RETURNS trigger
    AS $$
DECLARE
ref_title text;
 BEGIN
 IF TG_OP = 'UPDATE' THEN
              NEW.ts_search_all=prism.saved_graphs_tsvector(NEW.graphid); 
  ELSE
      ref_title:=coalesce(replace(NEW.title, '.', ' '), ' ');
     NEW.ts_search_all =to_tsvector(ref_title); 
  END IF;  
   RETURN NEW;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.trig_update_tsvector_saved_graphs() OWNER TO reconnoiter;
