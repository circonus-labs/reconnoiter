set search_path = noit,pg_catalog;

CREATE OR REPLACE FUNCTION stratcon.metric_name_summary_compile_fts_data
(in_sid integer, in_metric_name text, in_metric_type text)
RETURNS tsvector AS
$BODY$
DECLARE
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
    SELECT sid, module, name, target FROM check_archive WHERE sid=in_sid INTO ref_sid,ref_module,ref_name,ref_target;
    IF NOT FOUND THEN
        RETURN NULL;
    END IF;

    SELECT COALESCE(array_to_string(tags_array, ' '), ' ') FROM metric_tag WHERE sid=in_sid and metric_name=in_metric_name INTO ref_tags;
    IF NOT FOUND THEN
        ref_tags := ' ';
    END IF;

    SELECT COALESCE(array_to_string(tags_array, ' '), ' ') FROM check_tag WHERE sid=in_sid INTO ref_ctags;
    IF NOT FOUND THEN
        ref_ctags := ' ';
    END IF;

    SELECT value FROM metric_text_currently mt JOIN check_archive s USING(sid) 
        WHERE module='dns' AND s.name='in-addr.arpa' AND target = ref_target INTO ref_hostname;
      
    SELECT mt.value FROM metric_text_currently mt JOIN check_archive s USING(sid)
        WHERE s.module='snmp' AND mt.name='alias' AND s.sid=in_sid INTO ref_alias;

    ref_hostname := coalesce(replace(ref_hostname, '.', ' '), ' ');
    ref_metric_name := regexp_replace(in_metric_name, E'[_\`/.\\134]', ' ', 'g');
    ref_alias := coalesce(regexp_replace(ref_alias, E'[_\`/.\\134]', ' ', 'g'), ' ');

    v_fts_data := to_tsvector(ref_metric_name || ' ' ||
                                ref_module || ' ' ||
                                ref_name || ' ' ||
                                ref_target || ' ' ||
                                ref_hostname || ' ' ||
                                ref_alias || ' ' ||
                                ref_tags || ' ' ||
                                ref_ctags);
    RETURN v_fts_data;
END
$BODY$
LANGUAGE 'plpgsql' 
SECURITY DEFINER;
 
