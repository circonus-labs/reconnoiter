CREATE OR REPLACE FUNCTION prism.add_tags(in_sid integer, in_metric_name text, in_tags text) RETURNS void
    AS $$
DECLARE
v_sid integer;
v_metric_name text;
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
 BEGIN
     v_tags_array:= string_to_array(in_tags,'');
     SELECT sid into p_sid
      FROM stratcon.metric_tags
      WHERE sid=in_sid AND metric_name=in_metric_name;
     IF NOT FOUND THEN
          SELECT sid,metric_name INTO v_sid, v_metric_name
             FROM stratcon.metric_name_summary
             WHERE sid=in_sid AND metric_name=in_metric_name;
          IF NOT FOUND THEN
               RAISE EXCEPTION 'Metric does not exist in metric_name_summary table';
          ELSE
         INSERT INTO stratcon.metric_tags (sid,metric_name,tags_array) values(v_sid, v_metric_name,v_tags_array);
      END IF;
     ELSE
       SELECT tags_array INTO p_tags_array
          FROM stratcon.metric_tags
          WHERE sid=in_sid AND metric_name=in_metric_name;
             new_tags_array:= array_append(p_tags_array, in_tags);
           UPDATE  stratcon.metric_tags SET tags_array= new_tags_array WHERE sid=in_sid AND metric_name=in_metric_name;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION prism.add_tags(in_sid integer, in_tags text) RETURNS void
    AS $$
DECLARE
v_sid integer;
v_metric_name text;
v_tags_array text[];
p_sid integer;
p_tags_array text[];
new_tags_array text[];
 BEGIN
     v_tags_array:= string_to_array(in_tags,'');
     SELECT sid into p_sid
      FROM stratcon.check_tags
      WHERE sid=in_sid;
     IF NOT FOUND THEN
       INSERT INTO stratcon.check_tags (sid,tags_array) values(in_sid, v_tags_array);
     ELSE
       SELECT tags_array INTO p_tags_array
          FROM stratcon.check_tags
          WHERE sid=in_sid;
             new_tags_array:= array_append(p_tags_array, in_tags);
           UPDATE  stratcon.check_tags SET tags_array= new_tags_array WHERE sid=in_sid;
    END IF;
  RETURN;
END
$$
    LANGUAGE plpgsql;


ALTER FUNCTION prism.add_tags(in_sid integer, in_metric_name text, in_tags text) OWNER TO reconnoiter;


ALTER FUNCTION prism.add_tags(in_sid integer, in_tags text) OWNER TO reconnoiter;
