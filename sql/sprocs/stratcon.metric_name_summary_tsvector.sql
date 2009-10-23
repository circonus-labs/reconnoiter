-- Function: stratcon.metric_name_summary_tsvector(integer, text, text)

CREATE OR REPLACE FUNCTION stratcon.metric_name_summary_tsvector(in_sid integer, in_metric_name text, in_metric_type text)
  RETURNS tsvector AS
$BODY$DECLARE
ref_sid integer;
ref_module text;
ref_name text;
ref_target text;
ref_tags text;
ref_ctags text;
ref_hostname text;
ref_metric_name text;
ref_alias text;
v_fts_data tsvector;
BEGIN
    SELECT sid,module,name,target
      INTO ref_sid,ref_module,ref_name,ref_target
      FROM  noit.check_archive where sid=in_sid;
    IF NOT FOUND THEN
        RETURN NULL;
    END IF;

    SELECT COALESCE(array_to_string(tags_array, ' '), ' ') INTO ref_tags
      FROM noit.metric_tag
     WHERE sid=in_sid and metric_name=in_metric_name;
    IF NOT FOUND THEN
        ref_tags:=' ';
    END IF;

    SELECT COALESCE(array_to_string(tags_array, ' '), ' ') INTO ref_ctags
      FROM noit.check_tag
     WHERE sid=in_sid;
    IF NOT FOUND THEN
        ref_ctags:=' ';
    END IF;

    SELECT value INTO ref_hostname
      FROM noit.metric_text_currently mt
      JOIN noit.check_archive s USING(sid)
     WHERE module='dns' AND s.name='in-addr.arpa' AND target = ref_target;

    SELECT mt.value INTO ref_alias
      FROM noit.metric_text_currently mt
      JOIN noit.check_archive s USING(sid)
     WHERE s.module='snmp' AND mt.name='alias' AND s.sid=in_sid;

    ref_hostname := coalesce(replace(ref_hostname, '.', ' '), ' ');
    ref_metric_name := regexp_replace(in_metric_name, E'[_\`/.\\134]', ' ', 'g');
    ref_alias := coalesce(regexp_replace(ref_alias, E'[_\`/.\\134]', ' ', 'g'), ' ');

    v_fts_data=to_tsvector(ref_metric_name || ' ' ||
                                ref_module || ' ' ||
                                ref_name || ' ' ||
                                ref_target || ' ' ||
                                ref_hostname || ' ' ||
                                ref_alias || ' ' ||
                                ref_tags || ' ' ||
                                ref_ctags);
    RETURN v_fts_data;
END$BODY$
  LANGUAGE 'plpgsql' SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION stratcon.metric_name_summary_tsvector(integer, text, text) TO stratcon;

