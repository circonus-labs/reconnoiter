CREATE OR REPLACE FUNCTION prism.add_graph_tags(in_graphid uuid, in_tags text) RETURNS void
    AS $$
  DECLARE
   v_graphid uuid;
   v_graph_tags text[];
   new_tags_array text[];
   BEGIN
       SELECT graphid,graph_tags into v_graphid,v_graph_tags
         FROM prism.saved_graphs 
           WHERE graphid =in_graphid; 
     IF NOT FOUND THEN
                 RAISE EXCEPTION 'GraphID does not exist in saved graphs table.';
            ELSE 
             new_tags_array:= array_append(v_graph_tags, in_tags);
            UPDATE  prism.saved_graphs SET graph_tags = new_tags_array WHERE graphid=in_graphid;          
      END IF;
    RETURN;
  END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.add_graph_tags(in_graphid uuid, in_tags text) OWNER TO reconnoiter;
