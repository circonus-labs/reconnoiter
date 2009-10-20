CREATE OR REPLACE FUNCTION prism.remove_tags(in_sid integer, in_metric_name text, in_tags text) RETURNS void
    AS $$
DECLARE
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
i int;
 BEGIN
   v_tags_array:= string_to_array(in_tags,'');
     SELECT sid,tags_array into p_sid ,p_tags_array
      FROM stratcon.metric_tags
      WHERE sid=in_sid AND metric_name=in_metric_name;
     IF NOT FOUND THEN

               RAISE EXCEPTION 'Metric tags does not found to be removed';

     ELSE
         FOR i IN array_lower(p_tags_array, 1)..array_upper(p_tags_array, 1) LOOP
         IF NOT p_tags_array[i] =any(v_tags_array) THEN
            new_tags_array = array_append(new_tags_array, p_tags_array[i]);
          END IF;
         END LOOP;

           UPDATE  stratcon.metric_tags SET tags_array= new_tags_array WHERE sid=in_sid AND metric_name=in_metric_name;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION prism.remove_tags(in_sid integer, in_tags text) RETURNS void
    AS $$
DECLARE
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
i int;
 BEGIN
   v_tags_array:= string_to_array(in_tags,'');
     SELECT sid,tags_array into p_sid ,p_tags_array
      FROM stratcon.check_tags
      WHERE sid=in_sid;
     IF NOT FOUND THEN

               RAISE EXCEPTION 'Check tags does not found to be removed';

     ELSE
         FOR i IN array_lower(p_tags_array, 1)..array_upper(p_tags_array, 1) LOOP
         IF NOT p_tags_array[i] =any(v_tags_array) THEN
            new_tags_array = array_append(new_tags_array, p_tags_array[i]);
          END IF;
         END LOOP;

           UPDATE  stratcon.check_tags SET tags_array= new_tags_array WHERE sid=in_sid;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.remove_tags(in_sid integer, in_metric_name text, in_tags text) OWNER TO reconnoiter;


ALTER FUNCTION prism.remove_tags(in_sid integer, in_tags text) OWNER TO reconnoiter;
