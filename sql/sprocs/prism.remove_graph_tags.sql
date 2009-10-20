CREATE OR REPLACE FUNCTION prism.remove_graph_tags(in_graphid uuid, in_tags text) RETURNS void
    AS $$
DECLARE
v_graphid uuid;
v_graph_tags text[];
new_tags_array text[];
i int;
 BEGIN
    SELECT graphid,graph_tags into v_graphid,v_graph_tags
            FROM prism.saved_graphs 
              WHERE graphid =in_graphid; 
     IF NOT FOUND THEN
                    RAISE EXCEPTION 'GraphID does not exist in saved graphs table.';
     ELSE 
        FOR i IN array_lower(v_graph_tags, 1)..array_upper(v_graph_tags, 1) LOOP
           IF NOT v_graph_tags[i] =any(v_graph_tags) THEN
              new_tags_array = array_append(new_tags_array, v_graph_tags[i]);
           END IF;
        END LOOP;
        UPDATE  prism.saved_graphs SET graph_tags = new_tags_array WHERE graphid=in_graphid;           
     END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.remove_graph_tags(in_graphid uuid, in_tags text) OWNER TO reconnoiter;
