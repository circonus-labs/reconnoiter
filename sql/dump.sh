#!/bin/sh

PARTS=`psql reconnoiter postgres << EOF | egrep '_[0-9]'
\a
   select nspname||'.'||relname
     from pg_class as c join pg_namespace as n on(c.relnamespace = n.oid) 
    where relname ~ '_[0-9]{6}$';
EOF
`

for part in $PARTS
do
	EXCLUDE="$EXCLUDE --exclude-table=$part"
done
pg_dump -s -n stratcon $EXCLUDE -n prism -U postgres reconnoiter  > reconnoiter_ddl_dump.sql
