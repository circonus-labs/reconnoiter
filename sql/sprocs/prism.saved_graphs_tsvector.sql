CREATE OR REPLACE FUNCTION prism.saved_graphs_tsvector(in_graphid uuid) RETURNS tsvector
    AS $$DECLARE
ref_graphid uuid;
ref_graph_tags text;
ref_title text;
v_ts_search_all tsvector;
BEGIN
   SELECT graphid,COALESCE(array_to_string(graph_tags, ' '), ' '),title into ref_graphid,ref_graph_tags,ref_title
               FROM prism.saved_graphs 
              WHERE graphid =in_graphid;
    IF NOT FOUND THEN
        RETURN NULL;
    END IF;
    
    ref_title := coalesce(replace(ref_title, '.', ' '), ' ');
    ref_graph_tags := regexp_replace(ref_graph_tags, E'[_\`/.\\134]', ' ', 'g');
    
    v_ts_search_all=to_tsvector(ref_title || ' ' ||ref_graph_tags);
    
    RETURN v_ts_search_all;
END$$
    LANGUAGE plpgsql STRICT;


ALTER FUNCTION prism.saved_graphs_tsvector(in_graphid uuid) OWNER TO reconnoiter;
