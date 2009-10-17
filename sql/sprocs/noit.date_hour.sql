-- Name: noit.date_hour(timestamp with time zone); Type: FUNCTION; Schema: noit; 

CREATE FUNCTION noit.date_hour(timestamp with time zone) RETURNS timestamp with time zone
AS $_$
SELECT date_trunc('hour',$1);
$_$
LANGUAGE sql IMMUTABLE STRICT;

